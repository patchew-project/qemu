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

#include "PrepareForOptPass.hpp"
#include "CmdLineOptions.hpp"
#include "Error.hpp"
#include "FunctionAnnotation.hpp"
#include "LlvmCompat.hpp"

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Debug.h>

#include <queue>
#include <set>

#define DEBUG_TYPE "prepare-for-opt"

using namespace llvm;

static void demangleFunctionNames(Module &M) {
    StringSet DemangledNames;
    for (Function &F : M) {
        const std::string DemangledName = llvm::demangle(F.getName().str());
        if (DemangledName == F.getName()) {
            // Name not mangled
            continue;
        }
        // The resulting demangled name might look something like
        //
        //   namespace::subnamespace::function(...)
        //
        // Extract the function name and use this to replace mangled name.  If
        // we previously encountered the same name, give up and leave the
        // mangled name.
        std::string FunctionName;
        size_t Index = 0;
        // Remove namespaces
        Index = DemangledName.find_last_of(':');
        if (Index != std::string::npos) {
            FunctionName = DemangledName.substr(Index + 1);
        }
        // Remove arguments
        Index = FunctionName.find_first_of('(');
        if (Index != std::string::npos) {
            FunctionName = FunctionName.substr(0, Index);
        }

        if (DemangledNames.contains(FunctionName)) {
            continue;
        }
        DemangledNames.insert(FunctionName);

        F.setName(FunctionName);
    }
}

static Error parseAnnotationStr(Annotations &Ann, StringRef Str,
                                size_t NumArgs) {
    Str = Str.trim();

    // Function annotations
    if (Str.consume_front("helper-to-tcg")) {
        Ann.set(FunctionAnnotation::HelperToTcg);
        return Error::success();
    } else if (Str.consume_front("returns-immediate")) {
        Ann.set(FunctionAnnotation::ReturnsImmediate);
        return Error::success();
    }

    // Argument annotations
    ArgumentAnnotation AA;
    if (Str.consume_front("immediate")) {
        AA = ArgumentAnnotation::Immediate;
    } else if (Str.consume_front("ptr-to-offset")) {
        AA = ArgumentAnnotation::PtrToOffset;
    } else {
        return mkError("Unknown annotation");
    }

    // An argument annotation looks like
    //
    //  "immediate: 0, 1, 2",
    //
    // parse the comma separated list of argument indices.
    if (!Str.consume_front(":")) {
        return mkError("Expected \":\"");
    }
    Str = Str.ltrim(' ');
    do {
        Str = Str.ltrim(' ');
        size_t I = 0;
        Str.consumeInteger(10, I);
        if (I >= NumArgs) {
            return mkError("Annotation has out of bounds argument index");
        }
        Ann.set(I, AA);
    } while (Str.consume_front(","));

    return Error::success();
}

static void collectAnnotations(Module &M, AnnotationMapTy &ResultAnnotations) {
    // cast over dyn_cast is being used here to
    // assert that the structure of
    //
    //     llvm.global.annotation
    //
    // is what we expect.

    GlobalVariable *GA = M.getGlobalVariable("llvm.global.annotations");
    if (!GA) {
        return;
    }

    // Get the metadata which is stored in the first op
    auto *CA = cast<ConstantArray>(GA->getOperand(0));
    // Loop over metadata
    for (Value *CAOp : CA->operands()) {
        auto *Struct = cast<ConstantStruct>(CAOp);
        assert(Struct->getNumOperands() >= 2);

        Function *F = cast<Function>(Struct->getOperand(0));
        ConstantDataArray *AnnData =
            cast<ConstantDataArray>(Struct->getOperand(1)->getOperand(0));

        StringRef AnnStr = AnnData->getAsString();
        AnnStr = AnnStr.substr(0, AnnStr.size() - 1);
        Annotations Ann = ResultAnnotations[F];
        if (auto Err = parseAnnotationStr(Ann, AnnStr, F->arg_size()); Err) {
            errs() << "Failed to parse annotation: \"" << Err
                   << "\" for function " << F->getName() << "\n";
            continue;
        }
        ResultAnnotations[F] = Ann;
    }

    LLVM_DEBUG({
        for (auto &P : ResultAnnotations) {
            dbgs() << "Annotations for " << P.first->getName() << "\n";
            P.second.dump(dbgs());
        }
    });
}

inline bool hasValidReturnTy(const Module &M, const Function *F) {
    Type *RetTy = F->getReturnType();
    return RetTy->isStructTy() || RetTy == Type::getVoidTy(F->getContext()) ||
           RetTy == Type::getInt8Ty(M.getContext()) ||
           RetTy == Type::getInt16Ty(M.getContext()) ||
           RetTy == Type::getInt32Ty(M.getContext()) ||
           RetTy == Type::getInt64Ty(M.getContext());
}

// Functions that should be removed:
//   - No helper-to-tcg annotation (if TranslateAllHelpers == false);
//   - Invalid (non-integer/void) return type
static bool shouldRemoveFunction(const Module &M, const Function &F,
                                 const AnnotationMapTy &AnnotationMap) {
    if (F.isDeclaration()) {
        return false;
    }

    if (!hasValidReturnTy(M, &F)) {
        return true;
    }

    std::queue<const Function *> Worklist;
    std::set<const Function *> Visited;
    Worklist.push(&F);
    while (!Worklist.empty()) {
        const Function *F = Worklist.front();
        Worklist.pop();
        if (F->isDeclaration() or Visited.find(F) != Visited.end()) {
            continue;
        }
        Visited.insert(F);

        if (TranslateAllHelpers and
            compat::isFunctionQemuHelper(F->getName())) {
            // If --translate-all-helpers is provided and `F` starts with
            // "helper_*", then don't skip it.
            return false;
        } else if (auto It = AnnotationMap.find(F); It != AnnotationMap.end()) {
            // Otherwise check "helper-to-tcg" annotation.
            const Annotations &Ann = It->second;
            if (Ann.isSet(FunctionAnnotation::HelperToTcg)) {
                return false;
            }
        }

        // Push functions that call `F` to the worklist, this way we retain
        // functions that are being called by functions with the "helper-to-tcg"
        // annotation.
        for (const User *U : F->users()) {
            auto Call = dyn_cast<CallInst>(U);
            if (!Call) {
                continue;
            }
            const Function *ParentF = Call->getParent()->getParent();
            Worklist.push(ParentF);
        }
    }

    return true;
}

static void cullUnusedFunctions(Module &M, AnnotationMapTy &Annotations) {
    SmallPtrSet<Function *, 16> FunctionsToRemove;
    for (auto &F : M) {
        if (shouldRemoveFunction(M, F, Annotations)) {
            FunctionsToRemove.insert(&F);
        }
    }

    for (Function *F : FunctionsToRemove) {
        Annotations.erase(F);
        F->setComdat(nullptr);
        F->deleteBody();
    }
}

struct RetAddrReplaceInfo {
    User *Parent;
    unsigned OpIndex;
    Type *Ty;
};

static void replaceRetaddrWithUndef(Module &M) {
    // Replace uses of llvm.returnaddress arguments to cpu_ld* w. undef,
    // and let optimizations remove it.  Needed as llvm.returnaddress is
    // not reprensentable in TCG.
    SmallVector<RetAddrReplaceInfo, 24> UsesToReplace;
    Function *Retaddr = compat::Intrinsic::getOrInsertDeclaration(
        &M, Intrinsic::returnaddress, {});
    // Loop over all calls to llvm.returnaddress
    for (auto *CallUser : Retaddr->users()) {
        auto *Call = dyn_cast<CallInst>(CallUser);
        if (!Call) {
            continue;
        }
        for (auto *PtrToIntUser : Call->users()) {
            auto *Cast = dyn_cast<PtrToIntInst>(PtrToIntUser);
            if (!Cast) {
                continue;
            }
            for (Use &U : Cast->uses()) {
                auto *Call = dyn_cast<CallInst>(U.getUser());
                Function *F = Call->getCalledFunction();
                if (compat::isFunctionQemuLoadStore(F->getName())) {
                    UsesToReplace.push_back({
                        .Parent = U.getUser(),
                        .OpIndex = U.getOperandNo(),
                        .Ty = U->getType(),
                    });
                }
            }
        }
    }

    // Defer replacement to not invalidate iterators
    for (RetAddrReplaceInfo &RI : UsesToReplace) {
        auto *Undef = UndefValue::get(RI.Ty);
        RI.Parent->setOperand(RI.OpIndex, Undef);
    }
}

PreservedAnalyses PrepareForOptPass::run(Module &M,
                                         ModuleAnalysisManager &MAM) {
    demangleFunctionNames(M);
    collectAnnotations(M, ResultAnnotations);
    cullUnusedFunctions(M, ResultAnnotations);
    replaceRetaddrWithUndef(M);
    return PreservedAnalyses::none();
}
