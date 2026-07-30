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

#include "ValueMapping.hpp"

#include "CmdLineOptions.hpp"
#include "Error.hpp"
#include "FunctionAnnotation.hpp"
#include "LlvmCompat.hpp"
#include "PseudoInst.hpp"
#include "TcgEmit.hpp"
#include "TcgType.hpp"
#include "ValueMapping.hpp"

#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallBitVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>

#define DEBUG_TYPE "map-tcg-ops"

//
// Map TCG Operations
//
// Forward pass over the IR, emitting expressions to TCG using the provided
// `TempAllocationData` value mapping.
//

using namespace llvm;

// Wrapper class around a TcgV to cast it to/from 32-/64-bit
class TcgSizeAdapter {
    TcgEmitter &TE;
    const TcgV Orig;
    TcgV Adapted;

  public:
    TcgSizeAdapter(TcgEmitter &TE, const TcgV Orig) : TE(TE), Orig(Orig) {}

    const TcgV get(ValueSize Size) {
        const ValueSize OrigSize = Orig.intSize();
        if (Orig.Kind == IrImmediate or
            (OrigSize.TcgBitWidth == Size.TcgBitWidth)) {
            return Orig;
        } else if (Adapted.Kind == IrInvalid) {
            initAdapted(Size.TcgBitWidth);
        }
        return Adapted;
    }

  private:
    void initAdapted(AllowedTcgSize Size) {
        assert(Adapted.Kind == IrInvalid);
        const ValueSize OrigSize = Orig.intSize();
        assert(OrigSize.TcgBitWidth != Size);
        Adapted = TcgV::makeTemp({Size, OrigSize.LlvmBitWidth}, Orig.Kind);
        TE.defineNewTemp(Adapted);
        if (Size == 32) {
            TE.genExtrlI64I32(Adapted, Orig);
        } else {
            TE.genExtuI32I64(Adapted, Orig);
        }
    }
};

enum class LabelKind : uint8_t {
    Tcg,
    If,
    Else,
    Merge,
};

struct LabelInfo {
    LabelKind Kind;
    TcgV Label;
};

class Mapper {
    TcgEmitter &TE;
    const TempAllocationData &TAD;

    llvm::DenseMap<const BasicBlock *, LabelInfo> Labels;
    // Keep track of whether a TcgV has been defined already, or not
    SmallBitVector HasBeenDefined;

  public:
    Mapper(TcgEmitter &TE, const TempAllocationData &TAD) : TE(TE), TAD(TAD) {
        // Default to size of previously mapped TcgVs
        HasBeenDefined.resize(TAD.Map.size());
    }

    bool hasBeenDefined(const TcgV &V) { return HasBeenDefined[V.Id]; }

    void define(const TcgV &V) { HasBeenDefined.set(V.Id); }

    LabelInfo mapBbAndEmit(BasicBlock *BB, LabelKind Kind = LabelKind::Tcg) {
        auto It = Labels.find(BB);
        if (It == Labels.end()) {
            TcgV Label = TcgV::makeLabel();
            if (Kind == LabelKind::Tcg) {
                TE.defineNewTemp(Label);
            }
            return Labels.try_emplace(BB, LabelInfo{Kind, Label}).first->second;
        }
        return It->second;
    }

    TcgV mapTcgLabel(BasicBlock *BB) {
        const LabelInfo LI = mapBbAndEmit(BB);
        assert(LI.Kind == LabelKind::Tcg);
        return LI.Label;
    }

    const TcgV defineValue(const Value *V, bool ForceNondef = false) {
        // `Value` should have already been mapped by previous passes.
        auto It = TAD.Map.find(V);
        assert(It != TAD.Map.end());
        const TcgV Tcg = It->second;

        // Using that `TcgV` ids are sequential for a function and start
        // from 0.
        // TODO: Might be nice to just track a maximum from previous passes.
        if (Tcg.Id >= HasBeenDefined.size()) {
            HasBeenDefined.resize(Tcg.Id + 1);
        }
        if (!HasBeenDefined[Tcg.Id]) {
            if (!isa<Argument>(V) and
                (!TAD.hasReturnValue() or Tcg != TAD.ReturnValue) and
                !Tcg.ConstantExpression and Tcg.Kind != IrImmediate) {
                if (!ForceNondef) {
                    HasBeenDefined.set(Tcg.Id);
                    TE.defineNewTemp(Tcg);
                }
            }
        }

        return Tcg;
    }
};

static void ensureSignBitIsSet(TcgEmitter &TE, const TcgV &Tcg) {
    const ValueSize Size = Tcg.intSize();
    if (Tcg.llvmBitWidth() == Tcg.tcgBitWidth() or Tcg.Kind != IrValue) {
        return;
    }
    TE.genExtract(
        true, Tcg, Tcg, TcgV::makeImmediate("0", Size),
        TcgV::makeImmediate(Twine((int)Size.LlvmBitWidth).str(), Size));
}

static const TcgV mapCallReturnValue(Mapper &Mapper, CallInst *Call,
                                     bool CallToDecl = false) {
    // Only map return value if it has > 0 uses.  Destination values of call
    // instructions are the only ones which LLVM will not remove if unused.
    if (Call->getType()->isVoidTy() or Call->getNumUses() == 0) {
        return {};
    }
    return Mapper.defineValue(Call, CallToDecl);
}

static Instruction::BinaryOps mapPseudoInstToOpcode(PseudoInst Inst) {
    switch (Inst) {
    case VecAddScalar:
    case VecAddStore:
    case VecAddScalarStore:
        return Instruction::Add;
    case VecSubScalar:
    case VecSubStore:
    case VecSubScalarStore:
        return Instruction::Sub;
    case VecMulScalar:
    case VecMulStore:
    case VecMulScalarStore:
        return Instruction::Mul;
    case VecXorScalar:
    case VecXorStore:
    case VecXorScalarStore:
        return Instruction::Xor;
    case VecOrScalar:
    case VecOrStore:
    case VecOrScalarStore:
        return Instruction::Or;
    case VecAndScalar:
    case VecAndStore:
    case VecAndScalarStore:
        return Instruction::And;
    case VecShlScalar:
    case VecShlStore:
    case VecShlScalarStore:
        return Instruction::Shl;
    case VecLShrScalar:
    case VecLShrStore:
    case VecLShrScalarStore:
        return Instruction::LShr;
    case VecAShrScalar:
    case VecAShrStore:
    case VecAShrScalarStore:
        return Instruction::AShr;
    default:
        abort();
    }
}

static bool translatePseudoInstCall(CEmitter &CE, TcgEmitter &TE,
                                    CallInst *Call, PseudoInst PInst,
                                    Mapper &Mapper,
                                    const TcgGlobalMap &TcgGlobals,
                                    const TcgV &Ret,
                                    ArrayRef<TcgV> Args) {
    switch (PInst) {
    case IdentityMap: {
        // Nothing to do
    } break;
    case GetPC: {
        TE.genCallHelper("helper_getpc", {Ret});
    } break;
    case PtrAdd: {
        assert(Args[0].Kind == IrPtr or Args[0].Kind == IrPtrToOffset);
        if (Args[0].Kind == IrPtr) {
            TE.genAddPtr(Ret, Args[0], Args[1]);
        } else {
            assert(Args[1].Kind == IrValue);
            TE.genValueToPtr(Ret, Args[1]);
            TE.genAddPtr(Ret, Ret, Args[0]);
            TE.genAddPtr(Ret, Ret, TE.getGlobalEnv());
        }
    } break;
    case Brcond: {
        bool Fallthrough =
            cast<ConstantInt>(Call->getOperand(0))->getZExtValue();
        auto LlvmPred = static_cast<ICmpInst::Predicate>(
            cast<ConstantInt>(Call->getOperand(1))->getZExtValue());
        TE.genBrcond(LlvmPred, Args[2], Args[3], Args[4]);
        if (!Fallthrough) {
            TE.genBr(Args[5]);
        }
    } break;
    case Movcond: {
        auto LlvmPred = static_cast<ICmpInst::Predicate>(
            cast<ConstantInt>(Call->getOperand(0))->getZExtValue());
        if (CmpInst::isSigned(LlvmPred)) {
            // Since comprasions are made on TCG registers which contains
            // smaller logical LLVM values, make sure to correctly sign extend
            // smaller values for comparisons.
            ensureSignBitIsSet(TE, Args[1]);
            ensureSignBitIsSet(TE, Args[2]);
        }
        TE.genMovcond(LlvmPred, Ret, Args[1], Args[2], Args[3], Args[4]);
    } break;
    case VecSplat: {
        TE.genVecSplat(Ret, Args[0]);
    } break;
    case VecConstant: {
        TE.genVecArrSplat(Ret, Args[0]);
    } break;
    case VecNot: {
        TE.genVecNot(Ret, Args[0]);
    } break;
    case VecNotStore: {
        TE.genVecNot(Args[0], Args[1]);
    } break;
    case VecAddScalar:
    case VecSubScalar:
    case VecMulScalar:
    case VecXorScalar:
    case VecOrScalar:
    case VecAndScalar:
    case VecShlScalar:
    case VecLShrScalar:
    case VecAShrScalar: {
        auto Opcode = mapPseudoInstToOpcode(PInst);
        TE.genVecBinOp(Opcode, Ret, Args[0], Args[1]);
    } break;
    case VecAddStore:
    case VecSubStore:
    case VecMulStore:
    case VecXorStore:
    case VecOrStore:
    case VecAndStore:
    case VecShlStore:
    case VecLShrStore:
    case VecAShrStore:
    case VecAddScalarStore:
    case VecSubScalarStore:
    case VecMulScalarStore:
    case VecXorScalarStore:
    case VecOrScalarStore:
    case VecAndScalarStore:
    case VecShlScalarStore:
    case VecLShrScalarStore:
    case VecAShrScalarStore: {
        auto Opcode = mapPseudoInstToOpcode(PInst);
        TE.genVecBinOp(Opcode, Args[0], Args[1], Args[2]);
    } break;
    case VecSignedSatAddStore: {
        TE.genVecSignedSatAdd(Args[0], Args[1], Args[2]);
    } break;
    case VecSignedSatSubStore: {
        TE.genVecSignedSatSub(Args[0], Args[1], Args[2]);
    } break;
    case VecSelectStore: {
        TE.genVecBitsel(Args[0], Args[1], Args[2], Args[3]);
    } break;
    case VecAbsStore: {
        TE.genAbs(Args[0], Args[1]);
    } break;
    case VecSignedMaxStore: {
        TE.genVecSignedMax(Args[0], Args[1], Args[2]);
    } break;
    case VecUnsignedMaxStore: {
        TE.genVecUnsignedMax(Args[0], Args[1], Args[2]);
    } break;
    case VecSignedMinStore: {
        TE.genVecSignedMin(Args[0], Args[1], Args[2]);
    } break;
    case VecUnsignedMinStore: {
        TE.genVecUnsignedMin(Args[0], Args[1], Args[2]);
    } break;
    case VecTruncStore: {
        uint8_t DstElementBits =
            cast<ConstantInt>(Call->getOperand(0))->getZExtValue();
        TE.genVecTrunc(DstElementBits, Args[1], Args[2]);
    } break;
    case VecCompare: {
        auto LlvmPred = static_cast<ICmpInst::Predicate>(
            cast<ConstantInt>(Call->getOperand(0))->getZExtValue());
        TE.genVecCmp(Ret, LlvmPred, Args[1], Args[2]);
    } break;
    case VecWideCondBitsel: {
        TE.genVecBitsel(Ret, Args[0], Args[1], Args[2]);
        break;
    } break;
    case VecWideCondBitselStore: {
        TE.genVecBitsel(Args[0], Args[1], Args[2], Args[3]);
        break;
    } break;
    case GuestLoad: {
        uint8_t Sign = cast<ConstantInt>(Call->getOperand(1))->getZExtValue();
        uint8_t Size = cast<ConstantInt>(Call->getOperand(2))->getZExtValue();
        uint8_t Endianness =
            cast<ConstantInt>(Call->getOperand(3))->getZExtValue();
        TE.genGuestLoad(Ret, TE.materialize(Args[0]),
                        TE.getMemOp(Size, Endianness, Sign));
    } break;
    case GuestStore: {
        uint8_t Size = cast<ConstantInt>(Call->getOperand(2))->getZExtValue();
        uint8_t Endianness =
            cast<ConstantInt>(Call->getOperand(3))->getZExtValue();
        TE.genGuestStore(Args[0], Args[1], TE.getMemOp(Size, Endianness, 0));
    } break;
    case Exception: {
        // Map and adapt arguments to the call
        SmallVector<TcgV, 8> IArgs;
        for (auto Arg : Args) {
            IArgs.push_back(TE.materialize(Arg));
        }
        TE.genCallHelper("helper_raise_exception", IArgs.begin(), IArgs.end());
    } break;
    default:
        // unmapped pseudo inst
        return false;
    }
    return true;
}

static bool translateIntrinsicCall(TcgEmitter &TE, CallInst *Call, Function *F,
                                   const TcgV &Ret, ArrayRef<TcgV> Args,
                                   Mapper &Mapper) {
    switch (F->getIntrinsicID()) {
    case Intrinsic::abs: {
        TE.genAbs(Ret, Args[0]);
    } break;
    case Intrinsic::smax: {
        TE.genVecSignedMax(Ret, Args[0], Args[1]);
    } break;
    case Intrinsic::smin: {
        TE.genVecSignedMin(Ret, Args[0], Args[1]);
    } break;
    case Intrinsic::umax: {
        TE.genVecUnsignedMax(Ret, Args[0], Args[1]);
    } break;
    case Intrinsic::umin: {
        TE.genVecUnsignedMin(Ret, Args[0], Args[1]);
    } break;
    case Intrinsic::sadd_sat: {
        TE.genVecSignedSatAdd(Ret, Args[0], Args[1]);
    } break;
    case Intrinsic::ssub_sat: {
        TE.genVecSignedSatSub(Ret, Args[0], Args[1]);
    } break;
    case Intrinsic::usub_sat: {
        if (Args[0].Kind == IrPtrToOffset) {
            TE.genVecUnsignedSatSub(Ret, Args[0], Args[1]);
        } else {
            TE.genUnsignedSatSub(Ret, Args[0], Args[1]);
        }
    } break;
    case Intrinsic::ctlz: {
        if (Args[0].Kind == IrPtrToOffset) {
            // no gvec equivalent to clzi
            return false;
        }
        TE.genCountLeadingZeros(Ret, Args[0]);
    } break;
    case Intrinsic::cttz: {
        if (Args[0].Kind == IrPtrToOffset) {
            // no gvec equivalent to ctti
            return false;
        }
        TE.genCountTrailingZeros(Ret, Args[0]);
    } break;
    case Intrinsic::ctpop: {
        if (Args[0].Kind == IrPtrToOffset) {
            // no gvec equivalent to ctpop
            return false;
        }
        TE.genCountOnes(Ret, Args[0]);
    } break;
    case Intrinsic::bswap: {
        TE.genByteswap(Ret, Args[0]);
    } break;
    case Intrinsic::fshl: {
        TE.genFunnelShl(Ret, Args[0], Args[1], Args[2]);
    } break;
    case Intrinsic::bitreverse: {
        TE.genBitreverse(Ret, Args[0]);
    } break;
    case Intrinsic::memcpy: {
        TE.genVecMemcpy(Args[0], Args[1], Args[2]);
    } break;
    case Intrinsic::memset: {
        TE.genVecMemset(Args[0], Args[1], Args[2]);
    } break;
    default:
        // Unhandled LLVM intrinsic
        return false;
    }
    return true;
}

static Error
translateCall(const TcgGlobalMap &TcgGlobals,
              const AnnotationMapTy &AnnotationMap,
              const SmallPtrSet<Function *, 16> &HasTranslatedFunction,
              TcgEmitter &TE, CEmitter &CE, Mapper &Mapper, CallInst *Call) {
    Function *F = Call->getCalledFunction();
    if (!F) {
        return mkError("Indirect function calls not handled: ", Call);
    }

    assert(F->hasName());
    StringRef Name{F->getName()};

    // Filter out calls we don't care about. Note we don't have to manually deal
    // with debug instructions after LLVM 18.
    if (Name == "__assert_fail" or Name == "g_assertion_message_expr" or
        Call->isDebugOrPseudoInst() or
        (F->isIntrinsic() and
         (F->getIntrinsicID() == Intrinsic::lifetime_start or
          F->getIntrinsicID() == Intrinsic::lifetime_end))) {
        return Error::success();
    }

    const TcgV Ret = mapCallReturnValue(Mapper, Call);
    SmallVector<TcgV, 6> Args;
    for (unsigned i = 0; i < Call->arg_size(); ++i) {
        if (auto BB = dyn_cast<BasicBlock>(Call->getArgOperand(i))) {
            Args.push_back(Mapper.mapTcgLabel(BB));
        } else {
            Args.push_back(Mapper.defineValue(Call->getArgOperand(i)));
        }
    }

    // Function names sometimes contain embedded type information to
    // handle polymorphic arguments, for instance
    //
    //   llvm.memcpy.p0i8.p0i8.i64
    //
    // specifying the source and desination pointer types as i8* and
    // the size argument as an i64.
    //
    // Find the index for the first '.' before the types are
    // specified
    //
    //   llvm.memcpy.p0i8.p0i8.i64
    //              ^- index of this '.'
    size_t IndexBeforeTypes = StringRef::npos;
    for (size_t i = Name.size() - 1; i > 0; --i) {
        const char c = Name[i];
        bool ValidType = (c >= '0' and c <= '9') or c == 'i' or c == 'p' or
                         c == 'a' or c == 'v' or c == 'x';
        if (c == '.') {
            IndexBeforeTypes = i;
        } else if (!ValidType) {
            break;
        }
    }
    const StringRef StrippedName = Name.substr(0, IndexBeforeTypes);

    // TODO:
    //
    // Calls to [s]extract*() need some cleanup, it's not super obious what
    // happens, but if the length and offset arguments are immediates we can
    // instead map the call directly to a TCG equivalent, otherwise we continue
    // down the chain of `if`s until we end up calling emitted code.
    //

    if (F->isIntrinsic()) {
        if (!translateIntrinsicCall(TE, Call, F, Ret, Args, Mapper)) {
            return mkError("Unable to map intrinsic: ", Call);
        }
    } else if (PseudoInst PInst = getPseudoInstFromCall(Call);
               PInst != InvalidPseudoInst) {
        if (!translatePseudoInstCall(CE, TE, Call, PInst, Mapper, TcgGlobals,
                                     Ret, Args)) {
            return mkError("Unable to map pseudo inst: ", Call);
        }
    } else if (StrippedName == "extract32" and Args[1].Kind == IrImmediate and
               Args[2].Kind == IrImmediate) {
        TE.genExtract(false, Ret, Args[0], Args[1], Args[2]);
    } else if (StrippedName == "extract64" and Args[1].Kind == IrImmediate and
               Args[2].Kind == IrImmediate) {
        TE.genExtract(false, Ret, Args[0], Args[1], Args[2]);
    } else if (StrippedName == "sextract32" and Args[1].Kind == IrImmediate and
               Args[2].Kind == IrImmediate) {
        TE.genExtract(true, Ret, Args[0], Args[1], Args[2]);
    } else if (StrippedName == "sextract64" and Args[1].Kind == IrImmediate) {
        TE.genExtract(true, Ret, Args[0],
                      TcgV::makeImmediate("0", Ret.intSize()), Args[1]);
    } else if (StrippedName == "deposit32" and Args[2].Kind == IrImmediate and
               Args[3].Kind == IrImmediate) {
        TE.genDeposit(Ret, Args[0], Args[1], Args[2], Args[3]);
    } else if (StrippedName == "deposit64" and Args[2].Kind == IrImmediate and
               Args[3].Kind == IrImmediate) {
        TE.genDeposit(Ret, Args[0], Args[1], Args[2], Args[3]);
    } else if (compat::isFunctionQemuHelper(Name)) {
        // Map and adapt arguments to the call
        SmallVector<TcgV, 8> IArgs;
        for (auto Arg : Args) {
            IArgs.push_back(TE.materialize(Arg));
        }
        TE.genCallHelper(Name, IArgs.begin(), IArgs.end());
    } else {

        if (!AllowDeclCall and F->isDeclaration()) {
            return mkError("call to declaration: ", Call);
        }

        if (!F->isDeclaration() and
            HasTranslatedFunction.find(F) == HasTranslatedFunction.end()) {
            return mkError("call to function which failed to translate: ",
                           Call);
        }

        StringRef Name = F->getName();
        Name.consume_front("helper_");

        Annotations Ann{};
        if (auto It = AnnotationMap.find(F); It != AnnotationMap.end()) {
            // TODO: This is an unnecessary copy, we currently
            // use that a default constructed `Annotations` will
            // return 0 for all argument flags without
            // allocating any extra space.
            Ann = It->second;
        }

        SmallVector<TcgV, 6> TcgArgs;
        if (ForwardContext) {
            TcgArgs.push_back(TE.getDisasContext());
        }
        if (!F->isDeclaration() and Ret.Kind != IrInvalid) {
            TcgArgs.push_back(Ret);
        }
        for (size_t I = 0; I < Args.size(); ++I) {
            if (Ann.isSet(I, ArgumentAnnotation::Immediate)) {
                TcgArgs.push_back(Args[I]);
            } else {
                TcgArgs.push_back(TE.materialize(Args[I]));
            }
        }

        if (!F->isDeclaration()) {
            StackTwine<64> Str = Twine("emit_") + Name;
            TE.genCallCFunc(Str, {}, TcgArgs.begin(), TcgArgs.end());
        } else {
            TE.genCallCFunc(Name, Ret, TcgArgs.begin(), TcgArgs.end());
        }
    }

    return Error::success();
}

Error mapTcgOperations(const LinearBlocks &Blocks,
                       const TcgGlobalMap &TcgGlobals,
                       const AnnotationMapTy &AnnotationMap,
                       const SmallPtrSet<Function *, 16> &HasTranslatedFunction,
                       const TempAllocationData &TAD, TcgEmitter &TE,
                       CEmitter &CE) {
    Mapper Mapper(TE, TAD);
    for (BasicBlock *BB : Blocks) {
        // Set label if not first basic block
        if (BB != Blocks[0]) {
            const TcgV Label = Mapper.mapTcgLabel(BB);
            TE.genSetLabel(Label);
        }

        // Emit TCG generators for the current BB
        for (Instruction &I : *BB) {
            if (TAD.Map.lookup(&I).Kind == IrImmediate or
                TAD.Map.lookup(&I).ConstantExpression) {
                continue;
            }

            switch (I.getOpcode()) {
            case Instruction::Alloca: {
                auto Alloca = cast<AllocaInst>(&I);
                Mapper.defineValue(Alloca);
            } break;
            case Instruction::Br: {
                auto Branch = cast<BranchInst>(&I);
                if (Branch->isConditional()) {
                    assert(Branch->getNumSuccessors() == 2);
                    const TcgV Condition =
                        Mapper.defineValue(Branch->getCondition());
                    const TcgV CCondition = TE.materialize(Condition);
                    const TcgV True =
                        Mapper.mapTcgLabel(Branch->getSuccessor(0));
                    const TcgV False =
                        Mapper.mapTcgLabel(Branch->getSuccessor(1));

                    // Jump if condition is != 0
                    auto Zero = TcgV::makeImmediate("0", CCondition.intSize());
                    TE.genBrcond(CmpInst::Predicate::ICMP_NE, CCondition, Zero,
                                 True);
                    TE.genBr(False);
                } else {
                    const TcgV Label =
                        Mapper.mapTcgLabel(Branch->getSuccessor(0));
                    TE.genBr(Label);
                }
            } break;
            case Instruction::SExt: {
                auto SExt = cast<SExtInst>(&I);

                const TcgV Src = Mapper.defineValue(SExt->getOperand(0));
                const TcgV Dst = Mapper.defineValue(&I);
                if (Src.Kind == IrPtrToOffset) {
                    TE.genVecSext(Dst.vecSize().ElementBitWidth, Dst, Src);
                } else {
                    const ValueSize SrcSize = Src.intSize();
                    const ValueSize DstSize = Dst.intSize();
                    if (DstSize.LlvmBitWidth < 32) {
                        return mkError("sext to unsupported size: ", &I);
                    }
                    if (SrcSize.LlvmBitWidth > 1 and
                        SrcSize.LlvmBitWidth < 32) {
                        auto ASrc = TcgSizeAdapter(TE, Src);
                        TE.genExts(Dst, ASrc.get(DstSize));
                    } else if (SrcSize.LlvmBitWidth == 1 and
                               DstSize.TcgBitWidth == 32) {
                        TE.genMov(Dst, Src);
                    } else {
                        TE.genExtI32I64(Dst, Src);
                    }
                }
            } break;
            case Instruction::ZExt: {
                auto ZExt = cast<ZExtInst>(&I);

                const TcgV Src = Mapper.defineValue(ZExt->getOperand(0));
                const TcgV Dst = Mapper.defineValue(&I);
                if (Dst.Kind == IrValue) {
                    const ValueSize SrcSize = Src.intSize();
                    const ValueSize DstSize = Dst.intSize();
                    if (SrcSize.TcgBitWidth == DstSize.TcgBitWidth) {
                        TE.genMov(Dst, Src);
                    } else if (SrcSize.TcgBitWidth > DstSize.TcgBitWidth and
                               SrcSize.LlvmBitWidth == 1) {
                        // Paradoxically we may need to emit an extract
                        // instruction for when a zero extension is requested.
                        // This is to account for the fact that "booleans" in
                        // tcg can be both 64- and 32-bit. So for instance zext
                        // i1 -> i32, here i1 may actually be 64-bit.
                        TE.genExtrlI64I32(Dst, Src);
                    } else {
                        TE.genExtuI32I64(Dst, Src);
                    }
                } else if (Dst.Kind == IrPtrToOffset) {
                    TE.genVecZext(Dst.vecSize().ElementBitWidth, Dst, Src);
                } else {
                    return mkError("Invalid TcgSize!");
                }
            } break;
            case Instruction::Trunc: {
                auto Trunc = cast<TruncInst>(&I);

                const TcgV Src = Mapper.defineValue(Trunc->getOperand(0));
                const TcgV Dst = Mapper.defineValue(&I);
                if (Dst.Kind == IrValue) {
                    const ValueSize SrcSize = Src.intSize();
                    const ValueSize DstSize = Dst.intSize();
                    if (SrcSize.TcgBitWidth == 64) {
                        if (DstSize.LlvmBitWidth == 32) {
                            // 64 -> 32
                            TE.genExtrlI64I32(Dst, Src);
                        } else {
                            // 64 -> 16,8,1
                            // TODO:Simplify
                            auto Offset = TcgV::makeImmediate("0", DstSize);
                            auto Size = TcgV::makeImmediate(
                                Twine((int)Dst.llvmBitWidth()).str(), DstSize);
                            auto Temp = TcgV::makeTemp({T64, I64}, IrValue);
                            TE.defineNewTemp(Temp);
                            TE.genExtract(false, Temp, Src, Offset, Size);
                            TE.genExtrlI64I32(Dst, Temp);
                        }
                    } else {
                        // 32 -> 16,8,1
                        // 16 -> 8,1
                        //  8 -> 1
                        auto Offset = TcgV::makeImmediate("0", DstSize);
                        auto Size = TcgV::makeImmediate(
                            Twine((int)Dst.llvmBitWidth()).str(), DstSize);
                        TE.genExtract(false, Dst, Src, Offset, Size);
                    }
                } else if (Dst.Kind == IrPtrToOffset) {
                    TE.genVecTrunc(Dst.vecSize().ElementBitWidth, Dst, Src);
                } else {
                    abort();
                }
            } break;
            case Instruction::Add:
            case Instruction::And:
            case Instruction::AShr:
            case Instruction::LShr:
            case Instruction::Mul:
            case Instruction::UDiv:
            case Instruction::SDiv:
            case Instruction::Or:
            case Instruction::Shl:
            case Instruction::Sub:
            case Instruction::Xor: {
                auto Bin = cast<BinaryOperator>(&I);
                // Check we are working on integers
                TcgV Op1 = Mapper.defineValue(Bin->getOperand(0));
                TcgV Op2 = Mapper.defineValue(Bin->getOperand(1));
                const TcgV Res = Mapper.defineValue(Bin);

                // Swap operands if the first op. is an immediate
                // and the operator is commutative
                if (Op1.Kind == IrImmediate and Op2.Kind != IrImmediate and
                    Bin->isCommutative()) {
                    std::swap(Op1, Op2);
                }

                if (Res.Kind == IrValue) {
                    // Adapt sizes to account for boolean values where
                    // `LlvmSize` is 1, and `TcgSize` is either 32 or 64.
                    //
                    // Also materialize both arguments to skip trivial
                    // sanity checks found in `tcg_gen_[op]i*()` since e.g.
                    // LLVM wouldn't leave an add instruction with a 0
                    // operand.  This is also important to handle situations
                    // such as
                    //
                    //   TCGv_i32 tmp = tcg_temp_new_i32();
                    //   tcg_gen_shli_i32(tmp, src, arg);
                    //   tcg_gen_movcond_i32(TCG_COND_GTU, res
                    //                       tcg_constant_i32(arg),
                    //                       tcg_constant_i32(31),
                    //                       tcg_constant_i32(0),
                    //                       tmp);
                    //
                    // where LLVM might produce an "unsafe" operation and
                    // only guard it afterwards with a conditional move.
                    //
                    // TODO: Emitting
                    //
                    //    if (arg > 31) {
                    //        tcg_gen_movi(res, 0);
                    //    } else {
                    //        tcg_gen_[op]i*(res, src, arg);
                    //    }
                    //
                    // is feasible with some help from `canoncializeIR()`.
                    TcgSizeAdapter AOp1(TE, TE.materialize(Op1));
                    TcgSizeAdapter AOp2(TE, TE.materialize(Op2));

                    TE.genBinOp(Res, Bin->getOpcode(), AOp1.get(Res.intSize()),
                                AOp2.get(Res.intSize()));
                } else if (Res.Kind == IrPtrToOffset) {
                    TE.genVecBinOp(Bin->getOpcode(), Res, Op1, Op2);
                }
            } break;
            case Instruction::Call: {
                auto Call = cast<CallInst>(&I);
                auto Err =
                    translateCall(TcgGlobals, AnnotationMap,
                                  HasTranslatedFunction, TE, CE, Mapper, Call);
                if (Err) {
                    return Err;
                }
            } break;
            case Instruction::ICmp: {
                auto *ICmp = cast<ICmpInst>(&I);
                const TcgV Op1 = Mapper.defineValue(I.getOperand(0));
                const TcgV Op2 = Mapper.defineValue(I.getOperand(1));
                const TcgV Res = Mapper.defineValue(ICmp);

                ICmpInst::Predicate LlvmPred = ICmp->getPredicate();

                if (Op1.Kind == IrPtrToOffset) {
                    TE.genVecCmp(Res, LlvmPred, Op1, Op2);
                } else {
                    auto IOp1 = TE.materialize(Op1);
                    if (ICmp->isSigned()) {
                        ensureSignBitIsSet(TE, IOp1);
                        ensureSignBitIsSet(TE, Op2);
                    }
                    TE.genSetcond(LlvmPred, Res, IOp1, Op2);
                }
            } break;
            case Instruction::Select: {
                auto Select = cast<SelectInst>(&I);
                const TcgV Res = Mapper.defineValue(Select);

                if (Res.Kind == IrPtr) {
                    return mkError(
                        "Select statements for pointer types not supported: ",
                        Select);
                }
                const TcgV Cond = Mapper.defineValue(Select->getCondition());
                const TcgV True = Mapper.defineValue(Select->getTrueValue());
                const TcgV False = Mapper.defineValue(Select->getFalseValue());

                if (Res.Kind == IrPtrToOffset) {
                    TE.genVecBitsel(Res, Cond, True, False);
                } else if (Cond.Kind == IrImmediate) {
                    assert(Res.Kind != IrImmediate);
                    const TcgV MTrue = TE.materialize(True);
                    const TcgV MFalse = TE.materialize(False);
                    TE.genMov(Res, CE.ternary(Cond, MTrue, MFalse));
                } else {
                    const TcgV Zero = TcgV::makeImmediate("0", Res.intSize());
                    TcgSizeAdapter ACond(TE, Cond);
                    TcgSizeAdapter ATrue(TE, True);
                    TcgSizeAdapter AFalse(TE, False);
                    if (True.Kind == IrImmediate or False.Kind == IrImmediate) {
                        auto CTrue = TE.materialize(ATrue.get(Res.intSize()));
                        auto CFalse = TE.materialize(AFalse.get(Res.intSize()));
                        auto CCond = ACond.get(CTrue.intSize());

                        TE.genMovcond(CmpInst::Predicate::ICMP_NE, Res, CCond,
                                      Zero, CTrue, CFalse);
                    } else {
                        TE.genMovcond(CmpInst::Predicate::ICMP_NE, Res,
                                      ACond.get(Res.intSize()), Zero,
                                      ATrue.get(Res.intSize()),
                                      AFalse.get(Res.intSize()));
                    }
                }
            } break;
            case Instruction::Ret: {
                auto Ret = cast<ReturnInst>(&I);
                if (Ret->getNumOperands() == 0) {
                    break;
                }
                assert(TAD.hasReturnValue());
                const TcgV Tcg = TAD.Map.lookup(Ret->getReturnValue());
                // Even if `SkipReturnMov` is set we need to emit a mov for
                // constant expressions and immediates.
                if (Tcg.Kind == IrImmediate or Tcg.ConstantExpression or
                    (TAD.flags & SkipReturnMov) == 0) {
                    TE.genMov(TAD.ReturnValue, Tcg);
                }
            } break;
            case Instruction::Load: {
                auto *Load = cast<LoadInst>(&I);
                auto *LlvmPtr = Load->getPointerOperand();
                const TcgV Ptr = Mapper.defineValue(LlvmPtr);
                const TcgV Res = Mapper.defineValue(Load);

                switch (Ptr.Kind) {
                case IrPtr: {
                    auto Zero = TcgV::makeImmediate("0", Res.intSize());
                    TE.genHostLoad(Res, Ptr, Zero);
                } break;
                case IrImmediate: {
                    // Add pointer dereference to immediate address
                    TE.genMov(Res, CE.deref(Ptr, Res.intSize()));
                } break;
                case IrValue: {
                    TE.genMov(Res, Ptr);
                } break;
                case IrPtrToOffset: {
                    TE.genHostLoadFromVec(Res, Ptr);
                } break;
                default:
                    return mkError("Load from unsupported TcgV type");
                };
            } break;
            case Instruction::Store: {
                auto *Store = cast<StoreInst>(&I);
                auto *LlvmPtr = Store->getPointerOperand();
                const TcgV Val = Mapper.defineValue(Store->getValueOperand());
                const TcgV Ptr = Mapper.defineValue(LlvmPtr);
                if (Ptr.Kind == IrValue) {
                    // TODO: Is this path still needed?
                    switch (Val.Kind) {
                    case IrImmediate:
                    case IrValue: {
                        TE.genMov(Ptr, Val);
                    } break;
                    default:
                        return mkError("Store from unsupported TcgV type");
                    };
                } else if (Ptr.Kind == IrPtr) {
                    TE.genHostStore(Ptr, TE.materialize(Val));
                } else if (Ptr.Kind == IrPtrToOffset) {
                    // Stores to IrPtrToOffset are ignored, they are an artifact
                    // of IrPtrToOffset arguments being pointers. Stores to
                    // results are instead taken care of by whatever instruction
                    // generated the result.
                    // TODO: This is no longer true, double check
                } else {
                    return mkError("Store to unsupported TcgV kind: ", Store);
                }
            } break;
            case Instruction::Unreachable: {
                // TODO: Need to make sure unreachables are optimized out
                // earlier.
            } break;
            case Instruction::Switch: {
                auto Switch = cast<SwitchInst>(&I);
                // Operands to switch instructions alternate between
                // case values and the corresponding label:
                //   Operands: { Cond, DefaultLabel, Case0, Label0, Case1,
                //   Label1, ... }
                const TcgV Val = Mapper.defineValue(Switch->getOperand(0));
                const TcgV Default =
                    Mapper.mapTcgLabel(cast<BasicBlock>(Switch->getOperand(1)));
                for (uint32_t i = 2; i < Switch->getNumOperands(); i += 2) {
                    const TcgV BranchVal =
                        Mapper.defineValue(Switch->getOperand(i));
                    const TcgV Branch = Mapper.mapTcgLabel(
                        cast<BasicBlock>(Switch->getOperand(i + 1)));
                    TE.genBrcond(CmpInst::Predicate::ICMP_EQ, Val, BranchVal,
                                 Branch);
                }
                TE.genBr(Default);
            } break;
            default: {
                return mkError("Instruction not yet implemented: ", &I);
            }
            }
        }
    }

    return Error::success();
}
