//
//  Copyright(c) 2026 rev.ng Labs Srl. All Rights Reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#include "CanonicalizeIR.hpp"
#include "CmdLineOptions.hpp"
#include "LlvmCompat.hpp"
#include "PseudoInst.hpp"
#include "VectorLayout.hpp"

#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/RegionInfo.h>
#include <llvm/Analysis/VectorUtils.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Support/Casting.h>

#include <algorithm> // for std::max/min

using namespace llvm;
using namespace PatternMatch;

// Needed to track and remove instructions not handled by a subsequent dead code
// elimination, this applies to calls to pseudo instructions in particular.
using EraseInstVec = SmallVector<Instruction *, 16>;
using UsageCountMap = DenseMap<Value *, uint16_t>;
using ExitSet = SmallSet<BasicBlock *, 2>;

// Helper function to remove an instruction only if all uses have been removed.
// This way we can keep track instruction uses without having to modify the IR,
// or without having to iterate over all uses everytime we wish to remove an
// instruction.
static void addToEraseVectorIfUnused(EraseInstVec &InstToErase,
                                     UsageCountMap &UsageMap, Value *V) {
    auto *I = dyn_cast<Instruction>(V);
    if (!I) {
        return;
    }

    // Add V to map if not there
    if (UsageMap.count(V) == 0) {
        UsageMap[V] = V->getNumUses();
    }

    // Erase if count reaches zero
    if (--UsageMap[V] == 0) {
        InstToErase.push_back(I);
        UsageMap.erase(V);
    }
}

// Forward declarations of IR transformations used in canonicalizing the IR
static void upcastAshr(Instruction *I);
static void convertInsertShuffleToSplat(EraseInstVec &InstToErase,
                                        UsageCountMap &UsageMap, Module &M,
                                        Instruction *I);

static void
defineVectorConstants(Module &M, const VectorLayout &VL, Instruction *I,
                      DenseMap<Constant *, CallInst *> &Replacements);

static void simplifyVecBinOpWithSplat(EraseInstVec &InstToErase,
                                      UsageCountMap &UsageMap, Module &M,
                                      BinaryOperator *BinOp);

static void convertSelectICmp(Module &M, SelectInst *Select, ICmpInst *ICmp);

static void convertQemuLoadStoreToPseudoInst(Module &M, CallInst *Call,
                                             EraseInstVec &InstToErase,
                                             UsageCountMap &UsageMap);
static void convertExceptionCallsToPseudoInst(Module &M, CallInst *Call);
static void convertReturnAddrToPseudoInst(Module &M, CallInst *Call);
static void convertImmediateSelectAccessGlobal(EraseInstVec &InstToErase,
                                               Module &M, CallInst *Call);
static void convertImmediateDeclCall(EraseInstVec &InstToErase, Module &M,
                                     CallInst *Call);
static void convertUserBranchInstructions(Module &M,
                                          DominatorTreeAnalysis::Result &DT,
                                          const ExitSet &Exits, CallInst *Call);
static void convertVecStoreToPseudoInst(EraseInstVec &InstToErase, Module &M,
                                        StoreInst *Store);

void canonicalizeIR(Module &M, ModuleAnalysisManager &MAM,
                    const VectorLayout &VL) {
    auto &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    const bool HasUserBranches = !UserPCRelBranchFunc.empty() and
                                 !UserPCRelBranchConditionalFunc.empty() and
                                 !UserPCRelBranchFallthroughFunc.empty();
    for (Function &F : M) {
        if (F.isDeclaration()) {
            continue;
        }

        EraseInstVec InstToErase;
        UsageCountMap UsageMap;
        ExitSet Exits;
        auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);

        // Collect basic blocks which exit the function, needed to support
        // user-supplied branch calls, and correctly handle conditional branches
        if (HasUserBranches) {
            for (auto &BB : F) {
                if (isa<ReturnInst>(BB.getTerminator())) {
                    Exits.insert(&BB);
                }
            }
        }

        SmallPtrSet<Constant *, 8> ReplacedVectorConstants;

        // Perform a first pass over all instructions in the function and apply
        // IR transformations sequentially.  NOTE: order matters here.
        for (Instruction &I : instructions(F)) {
            if (I.isArithmeticShift()) {
                upcastAshr(&I);
            }

            convertInsertShuffleToSplat(InstToErase, UsageMap, M, &I);

            // Depends on convertInsertShuffleToSplat for @VecSplat instructions
            if (auto *BinOp = dyn_cast<BinaryOperator>(&I)) {
                simplifyVecBinOpWithSplat(InstToErase, UsageMap, M, BinOp);
            }

            // Independent of above
            if (auto *ICmp = dyn_cast<ICmpInst>(&I)) {
                for (auto *U : ICmp->users()) {
                    auto *Select = dyn_cast<SelectInst>(U);
                    if (Select and Select->getCondition() == ICmp) {
                        convertSelectICmp(M, Select, ICmp);
                    }
                }
            }

            // Independent of above, can run at any point
            if (auto *Call = dyn_cast<CallInst>(&I)) {
                convertQemuLoadStoreToPseudoInst(M, Call, InstToErase,
                                                 UsageMap);
                convertExceptionCallsToPseudoInst(M, Call);
                convertReturnAddrToPseudoInst(M, Call);
                convertImmediateSelectAccessGlobal(InstToErase, M, Call);
                convertImmediateDeclCall(InstToErase, M, Call);
                if (HasUserBranches) {
                    convertUserBranchInstructions(M, DT, Exits, Call);
                }
            }

            // Depends on other vector conversions performed above, needs to
            // run last
            if (auto *Store = dyn_cast<StoreInst>(&I)) {
                convertVecStoreToPseudoInst(InstToErase, M, Store);
            }
        }

        // Finally clean up instructions we need to remove manually
        for (Instruction *I : InstToErase) {
            I->eraseFromParent();
        }

        DenseMap<Constant *, CallInst *> Replacements;
        for (Instruction &I : instructions(F)) {
            defineVectorConstants(M, VL, &I, Replacements);
        }
    }
}

static Value *upcastInt(IRBuilder<> &Builder, IntegerType *FinalIntTy,
                        Value *V) {
    if (auto *ConstInt = dyn_cast<ConstantInt>(V)) {
        return ConstantInt::get(FinalIntTy, ConstInt->getZExtValue());
    } else {
        return Builder.CreateSExt(V, FinalIntTy);
    }
}

// Convert
//
//   %2 = ashr i[8|16] %1, %0
//
// to
//
//   %2 = zext i[8|16] %1 to i32
//   %3 = zext i[8|16] %2 to i32
//   %2 = ashr i32 %2, %3
//
static void upcastAshr(Instruction *I) {
    // Only care about scalar shifts < on less than 32-bit integers
    auto *IntTy = dyn_cast<IntegerType>(I->getType());
    if (!IntTy or IntTy->getBitWidth() >= 32) {
        return;
    }

    IRBuilder<> Builder(I);

    Value *Op1 = I->getOperand(0);
    Value *Op2 = I->getOperand(1);
    auto *UpcastIntTy = Builder.getInt32Ty();
    Op1 = upcastInt(Builder, UpcastIntTy, Op1);
    Op2 = upcastInt(Builder, UpcastIntTy, Op2);

    auto *AShr = Builder.CreateAShr(Op1, Op2);
    auto *Trunc = Builder.CreateTrunc(AShr, I->getType());
    I->replaceAllUsesWith(Trunc);
}

// Convert vector intrinsics
//
//   %0 = insertelement ...
//   %1 = shuffle ...
//
// to
//
//   %0 = call @VecSplat.*
//
static void convertInsertShuffleToSplat(EraseInstVec &InstToErase,
                                        UsageCountMap &UsageMap, Module &M,
                                        Instruction *I) {
    Value *SplatV;
    if (match(I, compat_m_Shuffle(compat_m_InsertElt(m_Value(), m_Value(SplatV),
                                                     m_ZeroInt()),
                                  m_Value(), compat_m_ZeroMask()))) {
        auto *VecTy = cast<VectorType>(I->getType());
        IRBuilder<> Builder(I);
        I->replaceAllUsesWith(
            createPseudoInstCall(M, Builder, VecSplat, VecTy, {SplatV}));
        addToEraseVectorIfUnused(InstToErase, UsageMap, I->getOperand(0));
        InstToErase.push_back(I);
    }
}

static void
defineVectorConstants(Module &M, const VectorLayout &VL, Instruction *I,
                      DenseMap<Constant *, CallInst *> &Replacements) {
    for (size_t J = 0; J < I->getNumOperands(); ++J) {
        Value *Op = I->getOperand(J);
        auto *Const = dyn_cast<Constant>(Op);
        auto *VecTy = dyn_cast<VectorType>(Op->getType());
        if (!Const or !VecTy) {
            // Only care about non-splatted constant vectors, skip
            // everything else.
            continue;
        }

        if (Replacements.count(Const)) {
            I->setOperand(J, Replacements[Const]);
            continue;
        }

        if (Value *Splat = Const->getSplatValue()) {
            auto *VecTy = cast<VectorType>(Const->getType());
            IRBuilder<> Builder(I);
            CallInst *Call =
                createPseudoInstCall(M, Builder, VecSplat, VecTy, {Splat});
            I->setOperand(J, Call);
            Replacements[Const] = Call;
        } else {
            // Constant non-splatted vector, attempt to combine elements
            // to make it splattable.
            SmallVector<uint64_t, 16> Ints;

            // Copy over elements to a vector
            const unsigned ElementCount = compat::getVectorElementCount(VecTy);
            const unsigned ElementSize =
                VecTy->getElementType()->getIntegerBitWidth();

            for (unsigned I = 0; I < ElementCount; ++I) {
                Constant *Element = Const->getAggregateElement(I);
                uint64_t Value = Element->getUniqueInteger().getZExtValue();
                Ints.push_back(Value);
            }

            // When combining adjacent elements, the maximum size supported
            // by TCG is 64-bit.  MaxNumElements is the maximum amount of
            // elements to attempt to merge
            size_t PatternLen = 0;
            const unsigned MaxNumElements = 8 * sizeof(uint64_t) / ElementSize;
            for (unsigned N = MaxNumElements; N > 1; N /= 2) {
                // Attempt to combine N elements by checking if the first
                // N elements tile the vector.
                bool Match = true;
                for (unsigned J = 0; J < ElementCount; ++J) {
                    if (Ints[J % N] != Ints[J]) {
                        Match = false;
                        break;
                    }
                }
                // If tiling succeeded, break out
                if (Match) {
                    PatternLen = N;
                    break;
                }
            }

            if (PatternLen > 0) {
                // Managed to tile vector with splattable element, compute
                // final splattable value
                // TODO: Move to VectorLayout.hpp
                uint64_t Column = 0;
                for (unsigned I = 0; I < PatternLen; ++I) {
                    // Flips the indices if lane 0 is in the most-significant
                    // bits.
                    const size_t Index = VL.indexLane(PatternLen, 0, I);
                    Column |= Ints[Index] << I * ElementSize;
                }
                IRBuilder<> Builder(I);
                CallInst *Call = createPseudoInstCall(
                    M, Builder, VecSplat, VecTy, {Builder.getInt64(Column)});
                I->setOperand(J, Call);
                Replacements[Const] = Call;
            } else {
                // Tiling failed, fall back to emitting an array copy from
                // C to a gvec vector.
                // TODO: Move to VectorLayout.hpp
                IRBuilder<> Builder(I);
                SmallVector<Constant *, 8> Columns;
                const size_t VectorSize =
                    (ElementCount * (ElementSize / 8) + 7);
                const size_t ColumnCount = VectorSize / 8;
                Columns.resize(ColumnCount);
                for (size_t C = 0; C < ColumnCount; ++C) {
                    uint64_t Column = 0;
                    const size_t LanesPerColumn = 64 / ElementSize;
                    for (size_t L = 0; L < LanesPerColumn; ++L) {
                        const size_t Index = VL.indexLane(LanesPerColumn, C, L);
                        Column |= Ints[Index] << (ElementSize * L);
                    }
                    assert(VL.BlockBytes >= 8);
                    const size_t Size =
                        std::min(ColumnCount, VL.BlockBytes / 8);
                    const size_t Index = VL.indexBlock(Size, C);
                    Columns[Index] = Builder.getInt64(Column);
                }
                CallInst *Call =
                    createPseudoInstCall(M, Builder, VecConstant, VecTy,
                                         {ConstantVector::get(Columns)});
                I->setOperand(J, Call);
                Replacements[Const] = Call;
            }
        }
    }
}

// Convert
//
//   %1 = @VecSplat(%0)
//   %2 = <NxM> ... op <NxM> %1
//
// to
//
//   %2 = call @Vec[op]Scalar(..., %0)
//
// which more closely matches TCG gvec operations.
static void simplifyVecBinOpWithSplat(EraseInstVec &InstToErase,
                                      UsageCountMap &UsageMap, Module &M,
                                      BinaryOperator *BinOp) {
    Value *Lhs = BinOp->getOperand(0);
    Value *Rhs = BinOp->getOperand(1);
    if (!Lhs->getType()->isVectorTy() or !Rhs->getType()->isVectorTy()) {
        return;
    }

    // Get splat value from constant or @VecSplat call
    Value *SplatValue = nullptr;
    if (auto *Const = dyn_cast<Constant>(Rhs)) {
        SplatValue = Const->getSplatValue();
    } else if (auto *Call = dyn_cast<CallInst>(Rhs)) {
        if (getPseudoInstFromCall(Call) == VecSplat) {
            SplatValue = Call->getOperand(0);
        }
    }

    if (SplatValue == nullptr) {
        return;
    }

    auto *VecTy = cast<VectorType>(Lhs->getType());
    auto *ConstInt = dyn_cast<ConstantInt>(SplatValue);
    bool ConstIsNegOne = ConstInt and ConstInt->getSExtValue() == -1;
    bool IsNot = BinOp->getOpcode() == Instruction::Xor and ConstIsNegOne;
    if (IsNot) {
        IRBuilder<> Builder(BinOp);
        BinOp->replaceAllUsesWith(
            createPseudoInstCall(M, Builder, VecNot, VecTy, {Lhs}));
    } else {
        PseudoInst Inst;
        switch (BinOp->getOpcode()) {
        case Instruction::Add:
            Inst = VecAddScalar;
            break;
        case Instruction::Sub:
            Inst = VecSubScalar;
            break;
        case Instruction::Mul:
            Inst = VecMulScalar;
            break;
        case Instruction::Xor:
            Inst = VecXorScalar;
            break;
        case Instruction::Or:
            Inst = VecOrScalar;
            break;
        case Instruction::And:
            Inst = VecAndScalar;
            break;
        case Instruction::Shl:
            Inst = VecShlScalar;
            break;
        case Instruction::LShr:
            Inst = VecLShrScalar;
            break;
        case Instruction::AShr:
            Inst = VecAShrScalar;
            break;
        default:
            abort();
        }

        IRBuilder<> Builder(BinOp);
        // Scalar gvec shift operations uses 32-bit scalars, whereas arithmetic
        // operations uses 64-bit scalars.
        uint32_t SplatSize = SplatValue->getType()->getIntegerBitWidth();
        if (BinOp->isShift()) {
            if (SplatSize > 32) {
                SplatValue =
                    Builder.CreateTrunc(SplatValue, Builder.getInt32Ty());
            }
        } else {
            if (SplatSize < 64) {
                SplatValue =
                    Builder.CreateZExt(SplatValue, Builder.getInt64Ty());
            }
        }
        BinOp->replaceAllUsesWith(
            createPseudoInstCall(M, Builder, Inst, VecTy, {Lhs, SplatValue}));
    }

    InstToErase.push_back(BinOp);
    addToEraseVectorIfUnused(InstToErase, UsageMap, Rhs);
}

// Convert
//
//   %2 = icmp [sgt|ugt|slt|ult] %0, %1
//   %5 = select %2, %3, %4
//
// to
//
//   %5 = [s|u][max|min] %0, %1
//
// if possible.  Results in cleaner IR, particularly useful for vector
// instructions.
static bool convertSelectICmpToMinMax(Module &M, SelectInst *Select,
                                      ICmpInst *ICmp, ICmpInst::Predicate &Pred,
                                      Value *ICmpOp0, Value *ICmpOp1,
                                      Value *SelectOp0, Value *SelectOp1) {
    if (ICmpOp0 != SelectOp0 or ICmpOp1 != SelectOp1) {
        return false;
    }

    Intrinsic::ID Intrin;
    switch (Pred) {
    case ICmpInst::ICMP_SGT:
        Intrin = Intrinsic::smax;
        break;
    case ICmpInst::ICMP_UGT:
        Intrin = Intrinsic::umax;
        break;
    case ICmpInst::ICMP_SLT:
        Intrin = Intrinsic::smin;
        break;
    case ICmpInst::ICMP_ULT:
        Intrin = Intrinsic::umin;
        break;
    default:
        return false;
    }

    auto Ty = Select->getType();
    auto MaxMinF = compat::Intrinsic::getOrInsertDeclaration(&M, Intrin, {Ty});

    IRBuilder<> Builder(Select);
    auto Call = Builder.CreateCall(MaxMinF, {ICmpOp0, ICmpOp1});
    Select->replaceAllUsesWith(Call);

    return true;
}

// In LLVM, icmp on vectors returns a vector on i1s whereas TCGs gvec_cmp
// returns a vector of the element type of its operands.  This can result in
// some subtle bugs.  Convert
//
//   icmp -> call @VecCompare
//   select -> call @VecWideCondBitsel
//
static bool convertSelectICmpToVecBitsel(Module &M, SelectInst *Select,
                                         ICmpInst *ICmp,
                                         ICmpInst::Predicate &Pred,
                                         Value *ICmpOp0, Value *ICmpOp1,
                                         Value *SelectOp0, Value *SelectOp1) {
    auto *ICmpVecTy = dyn_cast<VectorType>(ICmpOp0->getType());
    auto *SelectVecTy = dyn_cast<VectorType>(Select->getType());
    if (!ICmpVecTy or !SelectVecTy) {
        return false;
    }

    Instruction *Cmp = ICmp;
    {
        IRBuilder<> Builder(Cmp);
        ICmpInst::Predicate Pred = ICmp->getPredicate();
        CallInst *Call = createPseudoInstCall(
            M, Builder, VecCompare, ICmpVecTy,
            {ConstantInt::get(Builder.getInt32Ty(), Pred), ICmpOp0, ICmpOp1});
        Cmp = Call;
    }

    unsigned SrcWidth = ICmpVecTy->getElementType()->getIntegerBitWidth();
    unsigned DstWidth = SelectVecTy->getElementType()->getIntegerBitWidth();

    IRBuilder<> Builder(Select);
    if (SrcWidth < DstWidth) {
        Cmp = cast<Instruction>(Builder.CreateSExt(Cmp, SelectVecTy));
    } else if (SrcWidth > DstWidth) {
        Cmp = cast<Instruction>(Builder.CreateTrunc(Cmp, SelectVecTy));
    }
    Select->replaceAllUsesWith(
        createPseudoInstCall(M, Builder, VecWideCondBitsel, SelectVecTy,
                             {Cmp, SelectOp0, SelectOp1}));

    return true;
}

// Convert
//
//   %2 = icmp [sgt|ugt|slt|ult] %0, %1
//   %5 = select %2, %3, %4
//
// to
//
//   5 = call @Movcond.[cond].*(%1, %0, %3, %4)
//
// to more closely match TCG semantics.
static bool convertSelectICmpToMovcond(Module &M, SelectInst *Select,
                                       ICmpInst *ICmp,
                                       ICmpInst::Predicate &Pred,
                                       Value *ICmpOp0, Value *ICmpOp1,
                                       Value *SelectOp0, Value *SelectOp1) {
    // We only handle integers, we have no movcond equivalent in gvec
    auto *IntTy = dyn_cast<IntegerType>(Select->getType());
    if (!IntTy) {
        return false;
    }

    // If the type of the comparison does not match the return type of the
    // select statement, we cannot do anything so skip
    if (ICmpOp0->getType() != IntTy) {
        return false;
    }

    IRBuilder<> Builder(Select);
    if (cast<IntegerType>(ICmpOp0->getType())->getBitWidth() <
        IntTy->getBitWidth()) {
        if (ICmp->isSigned(Pred)) {
            ICmpOp0 = Builder.CreateSExt(ICmpOp0, IntTy);
            ICmpOp1 = Builder.CreateSExt(ICmpOp1, IntTy);
        } else {
            ICmpOp0 = Builder.CreateZExt(ICmpOp0, IntTy);
            ICmpOp1 = Builder.CreateZExt(ICmpOp1, IntTy);
        }
    }

    // Create @Movcond.[slt|...].* function
    Select->replaceAllUsesWith(
        createPseudoInstCall(M, Builder, Movcond, IntTy,
                             {ConstantInt::get(IntTy, Pred), ICmpOp0, ICmpOp1,
                              SelectOp0, SelectOp1}));

    return true;
}

// Specialize
//
//   %2 = icmp [sgt|ugt|slt|ult] %0, %1
//   %5 = select %2, %3, %4
//
// to either maximum/minimum, vector operations matching TCG, or a conditional
// move that also matches TCG in sematics.
static void convertSelectICmp(Module &M, SelectInst *Select, ICmpInst *ICmp) {
    // Given
    //   %2 = icmp [sgt|ugt|slt|ult] %0, %1
    //   %5 = select %2, %3, %4
    assert(Select->getCondition() == ICmp);
    Value *ICmpOp0 = ICmp->getOperand(0);
    Value *ICmpOp1 = ICmp->getOperand(1);
    Value *SelectOp0 = Select->getTrueValue();
    Value *SelectOp1 = Select->getFalseValue();
    ICmpInst::Predicate Pred = ICmp->getPredicate();

    // First try to convert to min/max
    //   %5 = [s|u][max|min] %0, %1
    if (convertSelectICmpToMinMax(M, Select, ICmp, Pred, ICmpOp0, ICmpOp1,
                                  SelectOp0, SelectOp1)) {
        return;
    }

    // Secondly try convert icmp -> @VecCompare, select -> @VecWideCondBitsel
    if (convertSelectICmpToVecBitsel(M, Select, ICmp, Pred, ICmpOp0, ICmpOp1,
                                     SelectOp0, SelectOp1)) {
        return;
    }

    // If min/max and vector conversion failed we fallback to a movcond
    //   %5 = call @Movcond.[cond].*(%1, %0, %3, %4)
    convertSelectICmpToMovcond(M, Select, ICmp, Pred, ICmpOp0, ICmpOp1,
                               SelectOp0, SelectOp1);
}

// Convert QEMU guest loads/stores represented by calls such as
//
//   call cpu_ldsw_be*(),
//   call cpu_stq_le*(),
//
// and friends, to pseudo instructions
//
//   %5 = call @GuestLoad.*(%addr, %sign, %size, %endian);
//   %5 = call @GuestStore.*(%addr, %value, %size, %endian);
//
// Makes the backend agnostic to what instructions or calls are used to
// represent loads and stores.
static void convertQemuLoadStoreToPseudoInst(Module &M, CallInst *Call,
                                             EraseInstVec &InstToErase,
                                             UsageCountMap &UsageMap) {
    Function *F = Call->getCalledFunction();
    StringRef Name = F->getName();
    if (Name.consume_front("cpu_")) {
        bool IsLoad = Name.consume_front("ld");
        bool IsStore = !IsLoad and Name.consume_front("st");
        if (IsLoad or IsStore) {
            bool Signed = !Name.consume_front("u");

            uint8_t Size = 0;
            switch (Name[0]) {
            case 'b':
                Size = 1;
                break;
            case 'w':
                Size = 2;
                break;
            case 'l':
                Size = 4;
                break;
            case 'q':
                Size = 8;
                break;
            default:
                abort();
            }

            uint8_t Endianness = 0;
            if (Size > 1 and Size < 8) {
                Name = Name.drop_front(2);
                switch (Name[0]) {
                case 'l':
                    Endianness = 1;
                    break;
                case 'b':
                    Endianness = 2;
                    break;
                default:
                    // TODO: we need to parse the `MemOpIndex` in the second to
                    // last argument, for now just assume little endian, same
                    // goes for signedness.
                    Endianness = 1;
                }
            }

            IRBuilder<> Builder(Call);
            Value *AddrOp = Call->getArgOperand(1);
            IntegerType *FlagTy = Builder.getInt8Ty();
            Value *SizeOp = ConstantInt::get(FlagTy, Size);
            Value *EndianOp = ConstantInt::get(FlagTy, Endianness);
            CallInst *NewCall;
            if (IsLoad) {
                Value *SignOp = ConstantInt::get(FlagTy, Signed);
                IntegerType *RetTy = cast<IntegerType>(Call->getType());
                NewCall =
                    createPseudoInstCall(M, Builder, GuestLoad, RetTy,
                                         {AddrOp, SignOp, SizeOp, EndianOp});
            } else {
                Value *ValueOp = Call->getArgOperand(2);
                NewCall = createPseudoInstCall(
                    M, Builder, GuestStore, Builder.getVoidTy(),
                    {AddrOp, ValueOp, SizeOp, EndianOp});
            }
            Call->replaceAllUsesWith(NewCall);
            InstToErase.push_back(Call);
        }
    }
}

// Convert QEMU exception calls
//
//   call raise_exception_ra(...),
//   ...
//
// to a pseudo instruction
//
//   %5 = call @Exception.*(...);
//
// Makes the backend agnostic to what instructions or calls are used to
// represent exceptions, and the list of sources can be expanded here.
static void convertExceptionCallsToPseudoInst(Module &M, CallInst *Call) {
    Function *F = Call->getCalledFunction();
    if (F->getName() == "raise_exception_ra") {
        IRBuilder<> Builder(Call);
        Value *Op0 = Call->getArgOperand(0);
        Value *Op1 = Call->getArgOperand(1);
        Call->replaceAllUsesWith(createPseudoInstCall(
            M, Builder, Exception, Builder.getVoidTy(), {Op0, Op1}));
    }
}

// Convert QEMU exception calls
//
//   %0 = call @llvm.returnaddr(...),
//   %1 = ptrtoint %0
//   ...
//
// to a pseudo instruction
//
//   %0 = call @getpc(...);
//
static void convertReturnAddrToPseudoInst(Module &M, CallInst *Call) {
    Function *F = Call->getCalledFunction();
    if (!F->isIntrinsic() or F->getIntrinsicID() != Intrinsic::returnaddress) {
        return;
    }
    if (!Call->hasOneUse()) {
        return;
    }

    auto *PtrToInt = dyn_cast<PtrToIntInst>(*Call->user_begin());
    if (!PtrToInt) {
        return;
    }

    IRBuilder<> Builder(PtrToInt);
    PtrToInt->replaceAllUsesWith(
        createPseudoInstCall(M, Builder, GetPC, PtrToInt->getType(), {}));
}

// Convert QEMU exception calls
//
//   %0 = call @llvm.returnaddr(...),
//   %1 = ptrtoint %0
//   ...
//
// to a pseudo instruction
//
//   %0 = call @getpc(...);
//
static void convertImmediateSelectAccessGlobal(EraseInstVec &InstToErase,
                                               Module &M, CallInst *Call) {
    PseudoInst PI = getPseudoInstFromCall(Call);
    if (PI != AccessGlobalArray) {
        return;
    }

    if (!Call->hasOneUser()) {
        return;
    }

    auto *Load = dyn_cast<LoadInst>(*Call->user_begin());
    if (!Load) {
        return;
    }

    auto *Zext = dyn_cast<ZExtInst>(Call->getArgOperand(1));
    if (!Zext) {
        return;
    }

    auto *Select = dyn_cast<SelectInst>(Zext->getOperand(0));
    if (!Select) {
        return;
    }

    if (!Zext->hasOneUse() or !Select->hasOneUse()) {
        return;
    }

    Value *Cond = Select->getCondition();
    Value *Arg1 = Select->getTrueValue();
    Value *Arg2 = Select->getFalseValue();

    if (!isa<Argument>(Arg1) or !isa<Argument>(Arg2)) {
        return;
    }

    IRBuilder<> Builder(Select);
    Function *AccessGlobalFn = Call->getCalledFunction();
    Value *TypeIndex = Call->getArgOperand(0);
    Value *Offset = Call->getArgOperand(1);
    Value *Arg1Zext = Builder.CreateZExt(Arg1, Zext->getType());
    Value *Arg2Zext = Builder.CreateZExt(Arg2, Zext->getType());
    CallInst *Access1 =
        Builder.CreateCall(AccessGlobalFn, {TypeIndex, Offset, Arg1Zext});
    CallInst *Access2 =
        Builder.CreateCall(AccessGlobalFn, {TypeIndex, Offset, Arg2Zext});
    LoadInst *Load1 = Builder.CreateLoad(Load->getType(), Access1);
    LoadInst *Load2 = Builder.CreateLoad(Load->getType(), Access2);
    Value *NewSelect = Builder.CreateSelect(Cond, Load1, Load2);

    InstToErase.push_back(Load);
    InstToErase.push_back(Call);

    Load->replaceAllUsesWith(NewSelect);
}

struct DeclReplaceInfo {
    bool Valid = false;
    Value *Zext = nullptr;
    Value *Select = nullptr;
    Value *Cond = nullptr;
    Value *Arg1 = nullptr;
    Value *Arg2 = nullptr;
};

static DeclReplaceInfo isReplaceableDeclCall(Value *A) {
    auto *Zext = dyn_cast<ZExtInst>(A);
    if (!Zext) {
        return {};
    }

    auto *Select = dyn_cast<SelectInst>(Zext->getOperand(0));
    if (!Select) {
        return {};
    }

    if (!Zext->hasOneUse() or !Select->hasOneUse()) {
        return {};
    }

    Value *Cond = Select->getCondition();
    Value *Arg1 = Select->getTrueValue();
    Value *Arg2 = Select->getFalseValue();

    if (!isa<Argument>(Arg1) or !isa<Argument>(Arg2)) {
        return {};
    }

    return {true, Zext, Select, Cond, Arg1, Arg2};
}

static void convertImmediateDeclCall(EraseInstVec &InstToErase, Module &M,
                                     CallInst *Call) {
    if (!Call->getCalledFunction()->isDeclaration()) {
        return;
    }

    if (!Call->hasOneUser()) {
        return;
    }

    SmallVector<Value *, 8> Args1;
    SmallVector<Value *, 8> Args2;
    DeclReplaceInfo Info{};
    IRBuilder<> Builder(Call);
    for (auto &A : Call->args()) {
        if (!Info.Valid) {
            Info = isReplaceableDeclCall(A);
            if (Info.Valid) {
                Value *Arg1Zext =
                    Builder.CreateZExt(Info.Arg1, Info.Zext->getType());
                Value *Arg2Zext =
                    Builder.CreateZExt(Info.Arg2, Info.Zext->getType());
                Args1.push_back(Arg1Zext);
                Args2.push_back(Arg2Zext);
                continue;
            }
        }

        Args1.push_back(A);
        Args2.push_back(A);
    }

    if (!Info.Valid) {
        return;
    }

    Function *F = Call->getCalledFunction();
    CallInst *Access1 = Builder.CreateCall(F, Args1);
    CallInst *Access2 = Builder.CreateCall(F, Args2);
    Value *NewSelect = Builder.CreateSelect(Info.Cond, Access1, Access2);
    Access1->setDebugLoc(Call->getDebugLoc());
    Access2->setDebugLoc(Call->getDebugLoc());

    // InstToErase.push_back(Load);
    InstToErase.push_back(Call);
    InstToErase.push_back(cast<Instruction>(Info.Zext));

    Call->replaceAllUsesWith(NewSelect);
}

static void convertUserBranchInstructions(Module &M,
                                          DominatorTreeAnalysis::Result &DT,
                                          const ExitSet &Exits,
                                          CallInst *Call) {
    Function *F = Call->getCalledFunction();
    if (F->getName() != UserPCRelBranchFunc) {
        return;
    }

    // Make sure the user-supplied functions exist and have the correct
    // signature.
    Function *BranchF = M.getFunction(UserPCRelBranchFunc);
    Function *CondF = M.getFunction(UserPCRelBranchConditionalFunc);
    Function *FallF = M.getFunction(UserPCRelBranchFallthroughFunc);
    if (!CondF or !FallF or BranchF->getType() != CondF->getType() or
        FallF->getType()->getFunctionNumParams()) {
        return;
    }

    BasicBlock *BB = Call->getParent();
    bool DominatesAllExits = true;
    for (BasicBlock *E : Exits) {
        if (!DT.dominates(BB, E)) {
            DominatesAllExits = false;
        }
    }
    // If the call to the user-supplied pc-relative jump function dominates all
    // exit blocks, then leave it be, it's a single relative jump with one exit.
    if (DominatesAllExits) {
        return;
    }

    // Otherwise add a fallthrough operation to each exit block.
    for (BasicBlock *E : Exits) {
        IRBuilder<> Builder(E->getTerminator());
        Builder.CreateCall(FallF, {});
    }

    // And replace the current jump operation with a "conditinal" one, so
    // QEMU knows how to chain the generated code.
    Call->setCalledFunction(CondF);
}

//
// Following functions help with converting between different types of
// instructions to pseudo instructions, particularly ones that write
// to a pointer, aka the Vec*Store pseudo instructions
//

static PseudoInst instructionToStorePseudoInst(unsigned Opcode) {
    switch (Opcode) {
    case Instruction::Trunc:
        return VecTruncStore;
    case Instruction::ZExt:
        return VecZExtStore;
    case Instruction::SExt:
        return VecSExtStore;
    case Instruction::Select:
        return VecSelectStore;
    case Instruction::Add:
        return VecAddStore;
    case Instruction::Sub:
        return VecSubStore;
    case Instruction::Mul:
        return VecMulStore;
    case Instruction::Xor:
        return VecXorStore;
    case Instruction::Or:
        return VecOrStore;
    case Instruction::And:
        return VecAndStore;
    case Instruction::Shl:
        return VecShlStore;
    case Instruction::LShr:
        return VecLShrStore;
    case Instruction::AShr:
        return VecAShrStore;
    default:
        abort();
    }
}

static PseudoInst pseudoInstToStorePseudoInst(PseudoInst Inst) {
    switch (Inst) {
    case VecNot:
        return VecNotStore;
    case VecAddScalar:
        return VecAddScalarStore;
    case VecSubScalar:
        return VecSubScalarStore;
    case VecMulScalar:
        return VecMulScalarStore;
    case VecXorScalar:
        return VecXorScalarStore;
    case VecOrScalar:
        return VecOrScalarStore;
    case VecAndScalar:
        return VecAndScalarStore;
    case VecShlScalar:
        return VecShlScalarStore;
    case VecLShrScalar:
        return VecLShrScalarStore;
    case VecAShrScalar:
        return VecAShrScalarStore;
    case VecWideCondBitsel:
        return VecWideCondBitselStore;
    default:
        abort();
    }
}

static PseudoInst intrinsicToStorePseudoInst(unsigned IntrinsicID) {
    switch (IntrinsicID) {
    case Intrinsic::sadd_sat:
        return VecSignedSatAddStore;
    case Intrinsic::ssub_sat:
        return VecSignedSatSubStore;
    case Intrinsic::fshr:
        return VecFunnelShrStore;
    case Intrinsic::abs:
        return VecAbsStore;
    case Intrinsic::smax:
        return VecSignedMaxStore;
    case Intrinsic::umax:
        return VecUnsignedMaxStore;
    case Intrinsic::smin:
        return VecSignedMinStore;
    case Intrinsic::umin:
        return VecUnsignedMinStore;
    case Intrinsic::ctlz:
        return VecCtlzStore;
    case Intrinsic::cttz:
        return VecCttzStore;
    case Intrinsic::ctpop:
        return VecCtpopStore;
    default:
        abort();
    }
}

// For binary/unary ops on vectors where the result is stored to a
// pointer
//
//   %3 = <NxM> %1 [op] <NxM> %2
//   %4 = bitcast i8* %0 to <NxM>*
//   store <NxM> %3, <NxM>* %4
//
// to
//
//   call @Vec[Op]Store.*(%0, %1, %2)
//
// This deals with the duality of pointers and vectors, and
// simplifies the backend.  We previously kept a map on the
// side to propagate "vector"-ness from %3 to %4 via the store,
// no longer!
static void convertVecStoreToPseudoInst(EraseInstVec &InstToErase, Module &M,
                                        StoreInst *Store) {
    Value *ValueOp = Store->getValueOperand();
    Type *ValueTy = ValueOp->getType();
    if (!ValueTy->isVectorTy()) {
        return;
    }

    // Ensure store and binary op. are in the same basic
    // block since the op. is moved to the store.
    bool InSameBB =
        cast<Instruction>(ValueOp)->getParent() == Store->getParent();
    if (!InSameBB) {
        return;
    }

    SmallVector<Value *, 3> Args;
    Value *PtrOp = Store->getPointerOperand();
    if (auto *BinOp = dyn_cast<BinaryOperator>(ValueOp)) {
        Instruction *Inst = cast<Instruction>(ValueOp);
        PseudoInst NewInst = instructionToStorePseudoInst(BinOp->getOpcode());
        IRBuilder<> Builder(Store);
        // Add one to account for extra store pointer
        // argument of Vec*Store pseudo instructions.
        const uint8_t SharedArgCount = pseudoInstArgCount(NewInst) - 1;
        Args.push_back(PtrOp);
        for (unsigned I = 0; I < SharedArgCount; ++I) {
            Value *Op = Inst->getOperand(I);
            Args.push_back(Op);
        }
        createPseudoInstCall(M, Builder, NewInst, Builder.getVoidTy(), Args);
    } else if (auto *Call = dyn_cast<CallInst>(ValueOp)) {
        Function *F = Call->getCalledFunction();
        PseudoInst OldInst = getPseudoInstFromCall(Call);
        if (OldInst != InvalidPseudoInst) {
            // Map scalar vector pseudo instructions to
            // store variants
            PseudoInst NewInst = pseudoInstToStorePseudoInst(OldInst);
            IRBuilder<> Builder(Store);
            Args.push_back(PtrOp);
            for (Value *Op : Call->args()) {
                Args.push_back(Op);
            }
            createPseudoInstCall(M, Builder, NewInst, Builder.getVoidTy(),
                                 Args);
        } else if (F->isIntrinsic()) {
            Instruction *Inst = cast<Instruction>(ValueOp);
            PseudoInst NewInst =
                intrinsicToStorePseudoInst(F->getIntrinsicID());
            // Add one to account for extra store pointer
            // argument of Vec*Store pseudo instructions.
            const uint8_t SharedArgCount = pseudoInstArgCount(NewInst) - 1;
            IRBuilder<> Builder(Store);
            Args.push_back(PtrOp);
            for (unsigned I = 0; I < SharedArgCount; ++I) {
                Args.push_back(Inst->getOperand(I));
            }
            createPseudoInstCall(M, Builder, NewInst, Builder.getVoidTy(),
                                 Args);
        }
    } else if (auto *Load = dyn_cast<LoadInst>(ValueOp)) {
        auto *VecTy = cast<VectorType>(Load->getType());
        auto *IntTy = cast<IntegerType>(VecTy->getElementType());
        uint32_t LlvmSize = IntTy->getBitWidth();
        uint32_t VectorElements = compat::getVectorElementCount(VecTy);
        IRBuilder<> Builder(Store);
        auto *Size = Builder.getInt64(LlvmSize * VectorElements);
        Builder.CreateMemCpy(Store->getPointerOperand(),
                             Store->getPointerAlignment(M.getDataLayout()),
                             Load->getPointerOperand(),
                             Load->getPointerAlignment(M.getDataLayout()),
                             Size);
        // Remove load if possible, won't get cleaned up by DCE
        if (Load->hasOneUse()) {
            InstToErase.push_back(cast<Instruction>(Load));
        }
    } else {
        Instruction *Inst = cast<Instruction>(ValueOp);

        PseudoInst NewInst = instructionToStorePseudoInst(Inst->getOpcode());
        // Add one to account for extra store pointer
        // argument of Vec*Store pseudo instructions.
        const uint8_t SharedArgCount = pseudoInstArgCount(NewInst) - 2;

        assert(SharedArgCount > 0 and
               SharedArgCount <= (uint8_t)Inst->getNumOperands());
        IRBuilder<> Builder(Store);
        SmallVector<Value *, 8> Args;
        auto SizeTy = Type::getInt8Ty(M.getContext());
        Args.push_back(ConstantInt::get(
            SizeTy,
            cast<VectorType>(ValueTy)->getElementType()->getIntegerBitWidth()));
        Args.push_back(PtrOp);
        for (uint8_t I = 0; I < SharedArgCount; ++I) {
            Args.push_back(Inst->getOperand(I));
        }
        createPseudoInstCall(M, Builder, NewInst, Builder.getVoidTy(), Args);
    }

    // Remove store instruction, this ensures DCE
    // can cleanup the rest, we also remove ValueOp
    // here since it's a call and won't get cleaned
    // by DCE.
    if (!isa<LoadInst>(ValueOp)) {
        InstToErase.push_back(cast<Instruction>(ValueOp));
    }
    InstToErase.push_back(Store);
}
