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

#include "PrepareForTcgPass.hpp"

#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

using namespace llvm;

static void removeFunctionsWithLoops(Module &M, ModuleAnalysisManager &MAM) {
    // Iterate over all Strongly Connected Components (SCCs), a SCC implies
    // the existence of loops if:
    //   - it has more than one node, or;
    //   - it has a self-edge.
    SmallPtrSet<Function *, 16> FunctionsToRemove;
    for (Function &F : M) {
        if (F.isDeclaration()) {
            continue;
        }
        for (auto It = scc_begin(&F); !It.isAtEnd(); ++It) {
            if (It.hasCycle()) {
                FunctionsToRemove.insert(&F);
                break;
            }
        }
    }

    for (Function *F : FunctionsToRemove) {
        F->setComdat(nullptr);
        F->deleteBody();
    }
}

PreservedAnalyses PrepareForTcgPass::run(Module &M,
                                         ModuleAnalysisManager &MAM) {
    removeFunctionsWithLoops(M, MAM);
    return PreservedAnalyses::none();
}
