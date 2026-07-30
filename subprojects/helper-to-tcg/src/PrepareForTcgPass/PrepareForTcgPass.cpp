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
#include "CmdLineOptions.hpp"

#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/Local.h>

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

inline void demotePhis(Function &F) {
    if (F.isDeclaration()) {
        return;
    }

    SmallVector<PHINode *, 10> Phis;
    for (auto &I : instructions(F)) {
        if (auto *Phi = dyn_cast<PHINode>(&I)) {
            Phis.push_back(Phi);
        }
    }

    for (auto *Phi : Phis) {
        DemotePHIToStack(Phi);
    }
}

static StringMap<size_t> collectTcgGlobals(Module &M, TcgGlobalMap &ResultTcgGlobalMap) {
    auto *Map = M.getGlobalVariable(TcgGlobalMappingsName);
    if (!Map) {
        return {};
    }

    // In case the `tcg_global_mappings` array is empty,
    // casting to `ConstantArray` will fail, even though it's a
    // `[0 x %struct.cpu_tcg_mapping]`.
    auto *MapElems = dyn_cast<ConstantArray>(Map->getOperand(0));
    if (!MapElems) {
        return {};
    }

    StringMap<size_t> TypeIndexMap;

    for (auto Row : MapElems->operand_values()) {
        auto *ConstRow = cast<ConstantStruct>(Row);

        // Get code string
        auto *CodePtr = ConstRow->getOperand(0);
        StringRef CodeStr =
            cast<ConstantDataArray>(CodePtr->getOperand(0))->getAsString();
        CodeStr = CodeStr.rtrim('\0');

        // Get base type name
        auto *TypeNamePtr = ConstRow->getOperand(2);
        StringRef TypeNameStr =
            cast<ConstantDataArray>(TypeNamePtr->getOperand(0))->getAsString();
        TypeNameStr = TypeNameStr.rtrim('\0');

        // Get offset in cpu env
        auto *Offset = cast<ConstantInt>(ConstRow->getOperand(4));
        // Get size of variable in cpu env
        auto *SizeInBytes = cast<ConstantInt>(ConstRow->getOperand(5));
        unsigned SizeInBits = 8 * SizeInBytes->getLimitedValue();

        auto *Stride = cast<ConstantInt>(ConstRow->getOperand(6));
        auto *NumElements = cast<ConstantInt>(ConstRow->getOperand(7));

        if (auto It = TypeIndexMap.find(TypeNameStr);
            It == TypeIndexMap.end()) {
            TypeIndexMap[TypeNameStr] = ResultTcgGlobalMap.size();
            ResultTcgGlobalMap.emplace_back();
        }

        const size_t Index = TypeIndexMap[TypeNameStr];
        ResultTcgGlobalMap[Index][Offset->getLimitedValue()] = {
            CodeStr,
            SizeInBits,
            NumElements->getLimitedValue(),
            Stride->getLimitedValue(),
        };
    }

    return TypeIndexMap;
}

PreservedAnalyses PrepareForTcgPass::run(Module &M,
                                         ModuleAnalysisManager &MAM) {
    removeFunctionsWithLoops(M, MAM);
    for (Function &F : M) {
        demotePhis(F);
    }
    const StringMap<size_t> TypeIndexMap = collectTcgGlobals(M, ResultTcgGlobalMap);
    return PreservedAnalyses::none();
}
