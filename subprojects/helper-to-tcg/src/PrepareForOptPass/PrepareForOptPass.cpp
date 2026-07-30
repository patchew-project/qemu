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
#include "Error.hpp"

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Debug.h>

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

PreservedAnalyses PrepareForOptPass::run(Module &M,
                                         ModuleAnalysisManager &MAM) {
    demangleFunctionNames(M);
    collectAnnotations(M, ResultAnnotations);
    return PreservedAnalyses::none();
}
