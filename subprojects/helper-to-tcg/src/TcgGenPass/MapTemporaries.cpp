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

#include "DebugInfo.hpp"
#include "ValueMapping.hpp"

#include "CmdLineOptions.hpp"
#include "Error.hpp"
#include "LlvmCompat.hpp"
#include "PseudoInst.hpp"
#include "TcgEmit.hpp"
#include "TcgType.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/Cloning.h>

#define DEBUG_TYPE "map-temporaries"

using namespace llvm;

//
// TCG Register Allocation Pass
//
// Analysis over the IR that performs basic register allocation to assign
// identifiers representing TCGv's to all non-argument and non-constant values
// in a given function.
//
// Note: Input code is assumed to be loop free, which drastically simplifies
// the register allocation. This assumption is reasonable as we expect code
// with loops to be either unrolled or vectorized, and we currently don't emit
// for loops in C.
//

// Type to represent a list of free TcgV's that can be reused when
// we need a new temporary. Exists for the duration of a function,
// and is expected to be small <= 8 free TcgV's at any time.
//
// This justifies the type being an array, since iteration times to
// find a free element will be small.
using FreeListVector = SmallVector<TcgV, 8>;

// Finds the first `TcgV` in `FreeList` with a matching `TcgBitWidth` and
// `Kind`.
static TcgV findFreeTcgV(FreeListVector &FreeList, TcgSize Size, TcgKind Kind) {
    for (size_t i = 0; i < FreeList.size(); ++i) {
        if (Kind != FreeList[i].Kind) {
            continue;
        }
        bool Matches = false;
        switch (Kind) {
        case IrPtrToOffset:
            Matches = Size.Vec.bytes() == FreeList[i].Size.Vec.bytes();
            break;
        case IrValue:
        case IrImmediate:
            Matches = Size.Val.TcgBitWidth == FreeList[i].Size.Val.TcgBitWidth;
            break;
        case IrPtr:
            Matches = true;
            break;
        default:
            continue;
        }
        if (Matches) {
            TcgV Tcg = FreeList[i];
            // Swap-remove
            FreeList[i] = FreeList.back();
            FreeList.pop_back();
            return Tcg;
        }
    }
    return {};
}

static bool valueIsNonImmediateArg(TempAllocationData &TAD, const Value *V) {
    if (!isa<Argument>(V)) {
        return false;
    }
    // Assume arguments have already been mapped at this point.
    auto It = TAD.Map.find(V);
    assert(It != TAD.Map.end());
    return It->second.Kind != IrImmediate;
}

//
// Functions for mapping an LLVM Value to a TcgV
//

enum MapValueFlags {
    ForceNewValue = 1,
};

// Given an integer LLVM value assign it to a TcgV, either by creating a new
// one or finding a suitable one on the FreeList
static Expected<TcgV> mapInteger(TempAllocationData &TAD,
                                 const AnnotationMapTy &Annotations,
                                 const DebugInfoMapTy &DebugInfo,
                                 FreeListVector &FreeList, const Value *V,
                                 uint32_t flags) {
    auto *Ty = cast<IntegerType>(V->getType());
    auto Size = ValueSize::fromLlvmType(Ty);
    if (!Size) {
        return Size.takeError();
    }

    StringRef Name = getDebugVarName(DebugInfo, V);

    if ((flags & ForceNewValue) != 0) {
        auto Tcg = TcgV::makeTemp(*Size, IrValue);
        Tcg.Name = Name;
        return TAD.map(V, Tcg);
    } else {
        // Non-constant integer

        // TODO: This is rather hacky and should be widened in an earlier pass.
        if (auto *ICmp = dyn_cast<ICmpInst>(V)) {
            // `icmp` return `i1`s and are used as either 32-bit or 64-bit TCG
            // value in QEMU.  Assume the size from operands, otherwise all
            // returned values are 32 bits in size, causing a mismatch with TCG.
            assert(Size->LlvmBitWidth == 1);
            auto *IntTy0 =
                dyn_cast<IntegerType>(ICmp->getOperand(0)->getType());
            if (!IntTy0) {
                return mkError("Icmp on non-integer type");
            }
            auto Size0 = ValueSize::fromLlvmType(IntTy0);
            if (!Size0) {
                return Size.takeError();
            }
            Size->TcgBitWidth = Size0->TcgBitWidth;
        }

        TcgV Tcg = findFreeTcgV(FreeList, {.Val = *Size}, IrValue);
        if (Tcg.Kind != IrInvalid) {
            // Found a `TcgV` of the corresponding TCG size, update LLVM size.
            Tcg.Size.Val.LlvmBitWidth = Size->LlvmBitWidth;
            return TAD.map(V, Tcg);
        } else {
            // Otherwise, create a new value
            auto Tcg = TcgV::makeTemp(*Size, IrValue);
            Tcg.Name = Name;
            return TAD.map(V, Tcg);
        }
    }
}

// Given an vector LLVM value assign it to a `TcgV`, either by creating a new
// one or finding a suitable one on the `FreeList`.  Special care is taken to
// map individual elements of constant vectors.
static Expected<TcgV> mapVector(TempAllocationData &TAD,
                                const AnnotationMapTy &Annotations,
                                const DebugInfoMapTy &DebugInfo,
                                FreeListVector &FreeList, const Value *V,
                                VectorType *VecTy, uint32_t flags) {
    auto Size = VectorSize::fromLlvmType(VecTy);
    if (!Size) {
        return Size.takeError();
    }

    StringRef Name = getDebugVarName(DebugInfo, V);

    // TODO: Hacky, avoids comparison results being `<N x i1>` which doesn't map
    // nicely to TCG, obtain size from argument instead.
    if (auto *ICmp = dyn_cast<ICmpInst>(V)) {
        auto *VecTy = cast<VectorType>(ICmp->getOperand(0)->getType());
        Size = VectorSize::fromLlvmType(VecTy);
        if (!Size) {
            return Size.takeError();
        }
    }

    // Create or find a `TcgV`
    TcgV Tcg = findFreeTcgV(FreeList, {.Vec = *Size}, IrPtrToOffset);
    if (Tcg.Kind != IrInvalid) {
        Tcg.Size.Vec = *Size;
    } else {
        Tcg = TcgV::makeVector(*Size);
        Tcg.Name = Name;
    }

    return TAD.map(V, Tcg);
}

// Given an pointer LLVM value assign it to a TcgV, either by creating a new
// one or finding a suitable one on the FreeList.
static Expected<TcgV> mapPointer(TempAllocationData &TAD,
                                 const AnnotationMapTy &Annotations,
                                 const DebugInfoMapTy &DebugInfo,
                                 FreeListVector &FreeList, const Value *V,
                                 uint32_t flags) {
    // If the value has an associated name from the debug information, use it
    StringRef Name{};
    if (auto It = DebugInfo.find(V); It != DebugInfo.end()) {
        Name = It->second.VarName;
    }

    if (auto *Alloca = dyn_cast<AllocaInst>(V)) {
        // `alloca`s represent stack variables in LLVM IR and return
        // pointers, we can simply map them to `IrValue`s
        auto *IntTy = dyn_cast<IntegerType>(Alloca->getAllocatedType());
        if (!IntTy) {
            return mkError("alloca with unsupported type: ", V);
        }

        auto Size = ValueSize::fromLlvmType(IntTy);
        if (!Size) {
            return Size.takeError();
        }

        // find or create a new `IrValue`
        TcgV Tcg = findFreeTcgV(FreeList, {.Val = *Size}, IrValue);
        if (Tcg.Kind != IrInvalid) {
            return TAD.map(V, Tcg);
        } else {
            auto Tcg = TcgV::makeTemp(*Size, IrValue);
            Tcg.Name = Name;
            return TAD.map(V, Tcg);
        }
    } else {
        // Otherwise, find or create a new IrPtr of the target pointer size
        TcgV Tcg = findFreeTcgV(FreeList, {}, IrPtr);
        if (Tcg.Kind != IrInvalid) {
            return TAD.map(V, Tcg);
        } else {
            auto Tcg = TcgV::makeTemp({}, IrPtr);
            Tcg.Name = Name;
            return TAD.map(V, Tcg);
        }
    }

    return mkError("Unable to map constant ", V);
}

// Given a LLVM value, assigns a TcgV by type (integer, pointer, vector).  If
// the given value has already been mapped to a TcgV, return it.
static Expected<TcgV> mapValue(TempAllocationData &TAD,
                               const AnnotationMapTy &Annotations,
                               const DebugInfoMapTy &DebugInfo,
                               FreeListVector &FreeList, const Value *V,
                               uint32_t flags = 0) {
    // Return previously mapped value
    auto It = TAD.Map.find(V);
    if (It != TAD.Map.end()) {
        return It->second;
    }

    if (isa<UndefValue>(V)) {
        return mkError("Unable to map undefined value: ", V);
    }

    Type *Ty = V->getType();
    if (isa<IntegerType>(Ty)) {
        return mapInteger(TAD, Annotations, DebugInfo, FreeList, V, flags);
    } else if (isa<PointerType>(Ty)) {
        return mapPointer(TAD, Annotations, DebugInfo, FreeList, V, flags);
    } else if (isa<VectorType>(Ty)) {
        return mapVector(TAD, Annotations, DebugInfo, FreeList, V,
                         cast<VectorType>(Ty), flags);
    }

    return mkError("Unable to map value ", V);
}

static bool shouldSkipInstruction(const Instruction *const I,
                                  bool SkipReturnMov) {
    // Skip returns if we're skipping return mov's
    if (isa<ReturnInst>(I) and SkipReturnMov) {
        return true;
    }
    // Skip assertions
    auto Call = dyn_cast<CallInst>(I);
    if (!Call) {
        return false;
    }
    Function *F = Call->getCalledFunction();
    if (!F) {
        return false;
    }
    StringRef Name = F->getName();
    return (Name == "__assert_fail" or Name == "g_assertion_message_expr" or
            isa<DbgValueInst>(I) or isa<DbgLabelInst>(I) or
            isa<DbgDeclareInst>(I));
}

static bool shouldSkipValue(const Value *const V) {
    return (isa<GlobalValue>(V) or isa<ConstantExpr>(V) or isa<BasicBlock>(V));
}

// A mapping of the return TCG variable to the value `RetV` is valid
// if no use of argument occurs beetween `RetV`s definition and use, this is to
// avoid clobbers.  Iteration may start in the middle of a basic block.
static bool isRetMapValid(TempAllocationData &TAD,
                          LinearBlocks::const_reverse_iterator BeginBB,
                          LinearBlocks::const_reverse_iterator EndBB,
                          BasicBlock::const_reverse_iterator BeginInst,
                          BasicBlock::const_reverse_iterator EndInst,
                          const Value *RetV) {
    auto ItBB = BeginBB;
    auto ItInst = BeginInst;

    do {
        do {
            const Instruction &I = *ItInst;
            // Check if we found definition of `RetV`.
            if (cast<Value>(&I) == RetV) {
                return true;
            }
            // Check for use of non-immediate arguments.
            for (auto &V : getOperands(&I)) {
                if (valueIsNonImmediateArg(TAD, V)) {
                    return false;
                }
            }
        } while (++ItInst != EndInst);

        if (++ItBB != EndBB) {
            EndInst = (*ItBB)->rend();
            ItInst = (*ItBB)->rbegin();
        }
    } while (ItBB != EndBB);

    return false;
}

static bool valueIsReference(const Value *V) {
    if (auto *Call = dyn_cast<CallInst>(V)) {
        auto *F = Call->getCalledFunction();
        if (AllowDeclCall && F->isDeclaration()) {
            return true;
        }
    }
    return false;
}

static bool instructionClobbersArguments(const Instruction *I) {
    if (auto *Call = dyn_cast<CallInst>(I)) {
        auto *F = Call->getCalledFunction();
        if (AllowDeclCall && F->isDeclaration() and
            !compat::isFunctionQemuHelper(F->getName())) {
            return true;
        } else if (F->isIntrinsic() and
                   F->getIntrinsicID() == Intrinsic::usub_sat) {
            // TODO: usub_sat maps to tcg_gen_ussub_*() which clobbers
            // arguments, if this changes on QEMUs end get rid of this.
            return true;
        }
    }
    return false;
}

static void removeDefinedVariable(TempAllocationData &TAD,
                                  FreeListVector &FreeList,
                                  const Instruction *I) {
    auto It = TAD.Map.find(cast<Value>(I));
    if (!isa<Argument>(I) and It != TAD.Map.end() and
        !cast<Value>(I)->getType()->isVoidTy()) {
        TcgV &Tcg = It->second;
        switch (Tcg.Kind) {
        case IrValue:
        case IrPtr:
        case IrPtrToOffset:
            FreeList.push_back(Tcg);
            break;
        case IrImmediate:
            break;
        default:
            abort();
        }
    }
}

Error allocateTemporaries(const Function &F, const LinearBlocks &Blocks,
                          const AnnotationMapTy &Annotations,
                          const DebugInfoMapTy &DebugInfo, CEmitter &C,
                          const TcgGlobalMap &TcgGlobals,
                          TempAllocationData &TAD) {
    FreeListVector FreeList;

    LLVM_DEBUG(dbgs() << "Allocating temporaries for " << F.getName() << "\n");

    // The PrepareForOptPass removes all functions with non-int/void return
    // types, assert this assumption.
    Type *RetTy = F.getReturnType();
    assert(isa<IntegerType>(RetTy) or RetTy->isVoidTy());
    // Map integer return values
    if (auto IntTy = dyn_cast<IntegerType>(RetTy)) {
        auto Size = ValueSize::fromLlvmType(IntTy);
        if (!Size) {
            return Size.takeError();
        }
        TAD.flags |= ReturnsValue;
        TAD.ReturnValue = TcgV::makeTemp(*Size, IrValue);
    }

    // Skip mov's to return value if possible, results of previous
    // instructions might have been assigned the return value.
    //
    // This is possible if:
    //   1. The return value is not an argument.
    //   2. The return value is not a constant.
    //   3. No use of an argument has occured after the definition of the
    //      value being returned.
    {
        auto Begin = Blocks.rbegin();
        auto End = Blocks.rend();
        const Instruction &I = *(*Begin)->rbegin();

        auto Ret = dyn_cast<ReturnInst>(&I);
        if (Ret and Ret->getNumOperands() == 1) {
            Value *RetV = Ret->getReturnValue();
            bool ValidRetV = !isa<Argument>(RetV) and !isa<ConstantInt>(RetV);
            bool ValidMap = isRetMapValid(TAD, Begin, End, (*Begin)->rbegin(),
                                          (*Begin)->rend(), RetV);
            if (ValidRetV and ValidMap) {
                assert(TAD.hasReturnValue());
                TAD.Map.try_emplace(RetV, TAD.ReturnValue);
                TAD.flags |= SkipReturnMov;
            }
        }
    }

    // Iterate over instructions in reverse and try to allocate TCG
    // variables.
    //
    // The algorithm is very straight forward, we keep a FreeList of TCG
    // variables we can reuse.  Variables are allocated on first use and
    // "freed" on definition.
    //
    // We allow reuse of the return TCG variable in order to save one
    // variable and skip the return mov if possible.  Since source and
    // return variables can overlap, when take the conservative route and
    // only allow reuse of the return variable if no arguments have been
    // used.

    bool SeenArgUse = false;

    for (auto ItBB = Blocks.rbegin(), ItBBEnd = Blocks.rend(); ItBB != ItBBEnd;
         ++ItBB) {
        const BasicBlock *BB = *ItBB;
        // Loop over instructions in the basic block in reverse
        for (auto ItInst = BB->rbegin(), ItInstEnd = BB->rend();
             ItInst != ItInstEnd; ++ItInst) {
            const Instruction &I = *ItInst;
            if (shouldSkipInstruction(&I, TAD.flags & SkipReturnMov)) {
                continue;
            }

            {
                auto It = TAD.Map.find(&I);
                if (It != TAD.Map.end() and (It->second.Kind == IrImmediate or
                                             It->second.ConstantExpression)) {
                    continue;
                }
            }

            LLVM_DEBUG(dbgs() << "  For: " << I << "\n");

            // For calls to the identity mapping pseudo instruction
            // we simply want to propagate the type allocated for the result
            // of the call to the operand.
            if (isa<CallInst>(&I)) {
                auto *Call = cast<CallInst>(&I);
                PseudoInst Inst = getPseudoInstFromCall(Call);
                if (Inst == IdentityMap) {
                    Value *Arg = Call->getArgOperand(0);
                    auto It = TAD.Map.find(cast<Value>(&I));
                    assert(It != TAD.Map.end());
                    const TcgV Original = It->second;
                    LLVM_DEBUG(dbgs() << "    Identity mapping");
                    if (TAD.Map.find(Arg) != TAD.Map.end()) {
                        LLVM_DEBUG(dbgs() << " (forward)\n");
                        // Propagate forward
                        Expected<TcgV> Tcg = mapValue(TAD, Annotations,
                                                      DebugInfo, FreeList, Arg);
                        assert(Tcg);

                        TcgV Propagated = Tcg.get();
                        // If we're identity mapped to an argument we might
                        // lose vector information if we propagate forward
                        // blindly, ensure vector type remains, likewise for
                        // scalar we might bit width.
                        if (Original.Kind == IrPtrToOffset) {
                            Propagated.Kind = IrPtrToOffset;
                            Propagated.Size = Original.Size;
                        } else {
                            Propagated.Size.Val.LlvmBitWidth =
                                Original.Size.Val.LlvmBitWidth;
                        }
                        TAD.Map[cast<Value>(&I)] = Propagated;

                        LLVM_DEBUG({
                            dbgs() << "Original:\n";
                            Original.dump(dbgs());
                            dbgs() << "Arg:\n";
                            Tcg->dump(dbgs());
                            dbgs() << "Ret:\n";
                            Propagated.dump(dbgs());
                        });
                    } else {
                        LLVM_DEBUG(dbgs() << " (forward)\n");
                        // Propagate back
                        TcgV Propagated = It->second;
                        // TODO:
                        if (auto *IntTy =
                                dyn_cast<IntegerType>(Arg->getType())) {
                            auto NewSize = ValueSize::fromLlvmType(IntTy);
                            assert(NewSize);
                            Propagated.Size.Val.LlvmBitWidth =
                                Original.Size.Val.LlvmBitWidth;
                        }
                        TAD.Map[Arg] = Propagated;

                        LLVM_DEBUG({
                            dbgs() << "Original:\n";
                            Original.dump(dbgs());
                            dbgs() << "Ret:\n";
                            Propagated.dump(dbgs());
                        });
                    }

                    continue;
                }
            }

            // Check if we've encountered any non-immediate argument yet
            for (const Use &U : getOperands(&I)) {
                if (valueIsNonImmediateArg(TAD, U)) {
                    SeenArgUse = true;
                }
            }

            // Free up variables as they are defined, iteration is in post
            // order meaning uses of vars always occur before definitions.
            bool AllowDestReuse = !instructionClobbersArguments(&I);
            if (AllowDestReuse) {
                removeDefinedVariable(TAD, FreeList, &I);
            }

            // Loop over operands and assign TcgV's. On first encounter of a
            // given operand we assign a new TcgV from the FreeList.
            for (const Use &V : getOperands(&I)) {
                auto It = TAD.Map.find(V);
                if (It != TAD.Map.end() or shouldSkipValue(V)) {
                    continue;
                }

                uint32_t Flags = 0;
                if (valueIsReference(V)) {
                    Flags |= ForceNewValue;
                }

                Expected<TcgV> Tcg =
                    mapValue(TAD, Annotations, DebugInfo, FreeList, V, Flags);
                if (!Tcg) {
                    return Tcg.takeError();
                }

                LLVM_DEBUG({
                    dbgs() << "    Arg: " << *V << "\n";
                    Tcg->dump(dbgs());
                });

                // If our value V got mapped to the return value,
                // make sure the mapping is valid
                //
                // A mapping to the return value is valid as long as
                // an argument has not been used.  This is to prevent
                // clobbering in the case that arguments and the return
                // value overlap.
                if (TAD.hasReturnValue() and *Tcg == TAD.ReturnValue) {
                    bool Valid =
                        isRetMapValid(TAD, ItBB, ItBBEnd, ItInst, ItInstEnd, V);
                    if (!SeenArgUse and Valid) {
                        continue;
                    }

                    // The mapping was not valid, erase it and assign a new
                    // one, this takes the return `TcgV` of out the `freelist`
                    // pool.
                    TAD.Map.erase(V);
                    Expected<TcgV> Tcg = mapValue(TAD, Annotations, DebugInfo,
                                                  FreeList, V, Flags);
                    if (!Tcg) {
                        return Tcg.takeError();
                    }
                }
            }

            // Free up variables as they are defined, iteration is in post
            // order meaning uses of vars always occur before definitions.
            if (!AllowDestReuse) {
                removeDefinedVariable(TAD, FreeList, &I);
            }
        }
    }

    return Error::success();
}
