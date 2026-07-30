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

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Demangle/Demangle.h>
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

PreservedAnalyses PrepareForOptPass::run(Module &M,
                                         ModuleAnalysisManager &MAM) {
    demangleFunctionNames(M);
    return PreservedAnalyses::none();
}
