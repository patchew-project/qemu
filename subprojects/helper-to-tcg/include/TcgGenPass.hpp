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

#pragma once

#include "DebugInfo.hpp"
#include "FunctionAnnotation.hpp"
#include "TcgGlobalMap.hpp"
#include "VectorLayout.hpp"

#include <llvm/IR/PassManager.h>

//
// TcgGenPass
//
// Backend pass responsible for emitting the final TCG code.
//
// This pass is further diveded into smaller passes that map arguments,
// propagate constants, allocate registers, and emit finally TCG.
//
// Note, the incoming IR is broken and linearized to an array of basic blocks,
// which acts to simplify the various backend passes.
//

class TcgGenPass : public llvm::PassInfoMixin<TcgGenPass> {
    llvm::raw_ostream &OutSource;
    llvm::raw_ostream &OutHeader;
    llvm::raw_ostream &OutHelpers;
    llvm::raw_ostream &OutEnabled;
    llvm::StringRef HeaderPath;
    const AnnotationMapTy &Annotations;
    const DebugInfoMapTy &DebugInfo;
    const TcgGlobalMap &TcgGlobals;
    const VectorLayout &VL;

  public:
    TcgGenPass(llvm::raw_ostream &OutSource, llvm::raw_ostream &OutHeader,
               llvm::raw_ostream &OutHelpers, llvm::raw_ostream &OutEnabled,
               llvm::StringRef HeaderPath, const AnnotationMapTy &Annotations,
               const DebugInfoMapTy &DebugInfo, const TcgGlobalMap &TcgGlobals,
               const VectorLayout &VL)
        : OutSource(OutSource), OutHeader(OutHeader), OutHelpers(OutHelpers),
          OutEnabled(OutEnabled), HeaderPath(HeaderPath),
          Annotations(Annotations), DebugInfo(DebugInfo),
          TcgGlobals(TcgGlobals), VL(VL) {}

    llvm::PreservedAnalyses run(llvm::Module &M,
                                llvm::ModuleAnalysisManager &MAM);
};
