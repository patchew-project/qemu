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

#include "TransformGEPs.hpp"
#include "DebugInfo.hpp"
#include "LlvmCompat.hpp"
#include "Error.hpp"
#include "PseudoInst.hpp"

#include <llvm/ADT/SmallSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/iterator_range.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Debug.h>

#define DEBUG_TYPE "transform-geps"

using namespace llvm;

// collectIndices will, given a getelementptr (GEP) instruction, construct an
// array of GepIndex structs keeping track of the total offset into the struct
// along with some access information.  For instance,
//
//   struct SubS {
//      uint8_t a;
//      uint8_t b;
//      uint8_t c;
//   };
//
//   struct S {
//      uint64_t i;
//      struct SubS sub[3];
//   };
//
//   void f(struct S *s, int idx) {
//      S->sub[idx].a = ...
//      S->sub[idx].b = ...
//      S->sub[idx].c = ...
//   }
//
// would correspond to the following GEPs
//
//   getelementptr %struct.S, %struct.S* %s, i64 0, i32 1, %idx, i32 0
//   getelementptr %struct.S, %struct.S* %s, i64 0, i32 1, %idx, i32 1
//   getelementptr %struct.S, %struct.S* %s, i64 0, i32 1, %idx, i32 2
//
// or the following GepIndex's
//
//   GepIndex{Size=0,false}, GepIndex{Size=8,false}, GepIndex{Size=4,true},
//   GepIndex{Size=0,false} GepIndex{Size=0,false}, GepIndex{Size=8,false},
//   GepIndex{Size=4,true}, GepIndex{Size=1,false} GepIndex{Size=0,false},
//   GepIndex{Size=8,false}, GepIndex{Size=4,true}, GepIndex{Size=2,false}
//

struct GepIndex {
    Value *V;
    uint64_t Size;
    bool IsArrayAccess = false;
};

using GepIndices = SmallVector<GepIndex, 2>;

struct GlobalAccessInfo {
    uint64_t Offset;
    StringRef BaseTypeName;
    Value *LastArrayIndex;
};

using GlobalAccessMap = DenseMap<Value *, GlobalAccessInfo>;

static Expected<GepIndices> collectIndices(const DataLayout &DL,
                                           GEPOperator *Gep) {
    Type *PtrOpTy = Gep->getPointerOperandType();
    if (!PtrOpTy->isPointerTy()) {
        return mkError("GEPs on vectors are not handled!");
    }
    Type *InternalTy = Type::getIntNTy(Gep->getContext(), 64);
    auto *One = ConstantInt::get(InternalTy, 1u);

    GepIndices Result;
    Type *CurrentTy = PtrOpTy;

    // Handle initial pointer dereference
    auto Begin = Gep->idx_begin();
    {
        CurrentTy = Gep->getSourceElementType();
        const size_t FixedSize = compat::getTypeAllocSize(DL, CurrentTy);
        Result.push_back(GepIndex{*Begin, FixedSize});
        ++Begin;
    }

    for (auto &Arg : make_range(Begin, Gep->idx_end())) {
        switch (CurrentTy->getTypeID()) {
        case Type::ArrayTyID: {
            CurrentTy = cast<ArrayType>(CurrentTy)->getElementType();
            const size_t FixedSize = compat::getTypeAllocSize(DL, CurrentTy);
            Result.push_back(
                GepIndex{Arg.get(), FixedSize, /* IsArrayAccess= */ true});
        } break;
        case Type::StructTyID: {
            auto *StructTy = cast<StructType>(CurrentTy);
            auto *Constant = dyn_cast<ConstantInt>(Arg.get());
            if (Constant->getBitWidth() > DL.getPointerSizeInBits()) {
                return mkError(
                    "GEP to struct with unsupported index bit width!");
            }
            uint64_t ConstantValue = Constant->getZExtValue();
            uint64_t ElementOffset =
                DL.getStructLayout(StructTy)->getElementOffset(ConstantValue);
            CurrentTy = StructTy->getTypeAtIndex(ConstantValue);
            Result.push_back(GepIndex{One, ElementOffset});
        } break;
        default:
            return mkError("GEP unsupported index type: ");
        }
    }

    return Result;
}

// Takes indices associated with a getelementpointer instruction and expands
// it into pointer math.
static void replaceGEPWithPointerMath(Module &M, Instruction *ParentInst,
                                      GEPOperator *Gep,
                                      const GepIndices &Indices) {
    assert(Indices.size() > 0);
    IRBuilder<> Builder(ParentInst);
    Value *PtrOp = Gep->getPointerOperand();

    // Sum indices to get the total offset from the base pointer
    Value *PrevV = nullptr;
    for (auto &Index : Indices) {
        Value *Mul = Builder.CreateMul(
            Index.V, ConstantInt::get(Index.V->getType(), Index.Size));
        if (PrevV) {
            uint32_t BitWidthLeft =
                cast<IntegerType>(PrevV->getType())->getIntegerBitWidth();
            uint32_t BitWidthRight =
                cast<IntegerType>(Mul->getType())->getIntegerBitWidth();
            if (BitWidthLeft < BitWidthRight) {
                PrevV = Builder.CreateZExt(PrevV, Mul->getType());
            } else if (BitWidthLeft > BitWidthRight) {
                Mul = Builder.CreateZExt(Mul, PrevV->getType());
            }
            PrevV = Builder.CreateAdd(PrevV, Mul);
        } else {
            PrevV = Mul;
        }
    }

    Gep->replaceAllUsesWith(createPseudoInstCall(
        M, Builder, PtrAdd, Gep->getType(), {PtrOp, PrevV}));
}

// Takes indices associated with a getelementpointer instruction and expands
// it into pointer math.
static Value *replaceGEPWithGlobalAccess(Module &M, Instruction *ParentInst,
                                         GEPOperator *Gep, size_t TypeIndex,
                                         uint64_t BaseOffset,
                                         Value *ArrayIndex) {
    CallInst *Call;
    IRBuilder<> Builder(ParentInst);
    Type *IndexTy = Type::getIntNTy(M.getContext(), 64);
    auto *ConstBaseOffset = ConstantInt::get(IndexTy, BaseOffset);
    auto *ConstTypeIndex = ConstantInt::get(IndexTy, TypeIndex);
    if (ArrayIndex) {
        Call =
            createPseudoInstCall(M, Builder, AccessGlobalArray, Gep->getType(),
                                 {ConstTypeIndex, ConstBaseOffset, ArrayIndex});
    } else {
        Call =
            createPseudoInstCall(M, Builder, AccessGlobalValue, Gep->getType(),
                                 {ConstTypeIndex, ConstBaseOffset});
    }
    Gep->replaceAllUsesWith(Call);
    return cast<Value>(Call);
}

static bool transformGEP(Module &M, const TcgGlobalMap &TcgGlobals,
                         GlobalAccessMap &GAMap,
                         const StringMap<size_t> &TypeIndexMap,
                         const DebugInfoMapTy &DebugInfo,
                         const GepIndices &Indices, Instruction *ParentInst,
                         GEPOperator *Gep) {
    GlobalAccessInfo Info{};
    uint32_t NumArrayAccesses = 0;
    for (const GepIndex &Index : Indices) {
        if (Index.IsArrayAccess) {
            Info.LastArrayIndex = Index.V;
            ++NumArrayAccesses;
        } else if (auto *Const = dyn_cast<ConstantInt>(Index.V)) {
            Info.Offset += Const->getZExtValue() * Index.Size;
        }
    }

    Value *PtrOp = Gep->getPointerOperand();
    bool IsGlobalAccess = isa<Argument>(PtrOp);
    if (auto It = GAMap.find(PtrOp); It != GAMap.end()) {
        Info.Offset += It->second.Offset;
        Info.BaseTypeName = It->second.BaseTypeName;
        assert(!Info.LastArrayIndex or !It->second.LastArrayIndex);
        if (It->second.LastArrayIndex) {
            Info.LastArrayIndex = It->second.LastArrayIndex;
        }
        IsGlobalAccess = true;
    } else if (auto It = DebugInfo.find(PtrOp); It != DebugInfo.end()) {
        Info.BaseTypeName = It->second.BaseTypeName;
    }

    bool PtrHasMapping =
        !Info.BaseTypeName.empty() and TypeIndexMap.count(Info.BaseTypeName);

    LLVM_DEBUG({
        dbgs() << "For " << *Gep << "\n";
        dbgs() << "  has mapping: " << PtrHasMapping << "\n";
        dbgs() << "  is global access: " << IsGlobalAccess << "\n";
    });

    if (IsGlobalAccess and PtrHasMapping and NumArrayAccesses <= 1) {

        if (!isa<ConstantExpr>(Gep)) {
            bool HasOnlyGEPUsers = true;
            for (auto *U : cast<Instruction>(Gep)->users()) {
                if (!isa<GEPOperator>(U)) {
                    HasOnlyGEPUsers = false;
                    break;
                }
            }
            if (HasOnlyGEPUsers) {
                GAMap[cast<Value>(Gep)] = Info;
                return true;
            }
        }

        const size_t Index = TypeIndexMap.lookup(Info.BaseTypeName);

        // Array accesses, particularly those to an array at the beginning of a
        // mapped structs, e.g. `uint32_t v = env->gpr[40]`, might appear either
        // as
        //
        //   getelementptr struct.CPUArchState, ptr %0, i64 0, i64 40,
        //
        // or
        //
        //   getelementptr i8, ptr %0, i64 160.
        //
        // The former and simpler one being more common in older LLVM versions.
        // For the latter we need to manually compute the array index.
        for (auto &P : TcgGlobals[Index]) {
            const TcgGlobal Global = P.second;
            if ((Global.NumElements == 1 and Info.Offset == P.first) or
                (Info.Offset >= P.first and
                 Info.Offset < P.first + Global.NumElements * Global.Stride)) {
                Value *ArrayIndexV = Info.LastArrayIndex;
                if (Global.NumElements > 1 and !Info.LastArrayIndex) {
                    assert(Global.Stride > 0);
                    uint64_t ArrayIndex =
                        (Info.Offset - P.first) / Global.Stride;
                    Info.Offset = P.first;
                    ArrayIndexV = ConstantInt::get(
                        Type::getInt64Ty(M.getContext()), ArrayIndex);
                }

                LLVM_DEBUG(dbgs() << "  replacing with global access\n");
                Value *Access = replaceGEPWithGlobalAccess(
                    M, ParentInst, Gep, Index, Info.Offset, ArrayIndexV);
                GAMap[Access] = Info;
                return !isa<ConstantExpr>(Gep);
            }
        }
    }

    LLVM_DEBUG(dbgs() << "  replacing with pointer math\n");
    replaceGEPWithPointerMath(M, ParentInst, Gep, Indices);
    return !isa<ConstantExpr>(Gep);
}

static GEPOperator *getGEPOperator(Instruction *I) {
    // If the instructions is directly a GEP, simply return it.
    auto *GEP = dyn_cast<GEPOperator>(I);
    if (GEP) {
        return GEP;
    }

    // Hard-code handling of GEPs that appear as an inline operand to loads
    // and stores.
    if (isa<LoadInst>(I)) {
        auto *Load = cast<LoadInst>(I);
        auto *ConstExpr = dyn_cast<ConstantExpr>(Load->getPointerOperand());
        if (ConstExpr) {
            return dyn_cast<GEPOperator>(ConstExpr);
        }
    } else if (isa<StoreInst>(I)) {
        auto *Store = dyn_cast<StoreInst>(I);
        auto *ConstExpr = dyn_cast<ConstantExpr>(Store->getPointerOperand());
        if (ConstExpr) {
            return dyn_cast<GEPOperator>(ConstExpr);
        }
    }

    return nullptr;
}

void transformGEPs(Module &M, Function &F, const TcgGlobalMap &TcgGlobals,
                   const StringMap<size_t> &TypeIndexMap,
                   const DebugInfoMapTy &DebugInfo) {
    SmallSet<Instruction *, 8> InstToErase;
    GlobalAccessMap GAMap;

    LLVM_DEBUG(dbgs() << "Transforming GEPs for:" << F.getName() << "\n");

    for (auto &I : instructions(F)) {
        GEPOperator *GEP = getGEPOperator(&I);
        if (!GEP) {
            continue;
        }

        Expected<GepIndices> Indices = collectIndices(M.getDataLayout(), GEP);
        if (!Indices) {
            dbgs() << "Failed collecting GEP indices for:\n\t" << I << "\n";
            dbgs() << "Reason: " << Indices.takeError();
            abort();
        }

        bool ShouldErase = transformGEP(M, TcgGlobals, GAMap, TypeIndexMap,
                                        DebugInfo, Indices.get(), &I, GEP);
        if (ShouldErase) {
            InstToErase.insert(&I);
        }
    }

    for (auto *I : InstToErase) {
        I->eraseFromParent();
    }
}
