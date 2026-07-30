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
#include "LinearizeBlocks.hpp"
#include "TcgEmit.hpp"
#include "TcgGlobalMap.hpp"
#include "TcgType.hpp"

#include <llvm/ADT/SmallSet.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Error.h>

//
// Value Mapping
//
// Data structures and functions needed for mapping various LLVM `Value`s to
// `TcgV`s.
//

namespace llvm {
class Function;
}

// Flags reprensting common special cases for function return values, used when
// emitted TCG to produce better output.
enum TempAllocationFlags {
    SkipReturnMov = 1,
    ReturnsImmediate = 2,
    ReturnsValue = 4,

    HasReturn = (ReturnsImmediate | ReturnsValue),
};

// Main data structure responsible for holding mappings between LLVM `Value`s
// and `TcgV`s, populated by functions declared below.
struct TempAllocationData {
    // Mapping of LLVM Values to the corresponding TcgV
    llvm::DenseMap<const llvm::Value *, TcgV> Map;

    // Whether or not the final mov in an instruction can safely
    // be ignored or not.
    uint8_t flags = 0;
    TcgV ReturnValue;

    inline bool hasReturnValue() const { return flags & HasReturn; }

    inline TcgV map(const llvm::Value *V, const TcgV &T) {
        return Map.try_emplace(V, T).first->second;
    }
};

inline const llvm::iterator_range<llvm::User::const_op_iterator>
getOperands(const llvm::Instruction *const I) {
    switch (I->getOpcode()) {
    case Instruction::GetElementPtr:
        return llvm::cast<llvm::GetElementPtrInst>(I)->operands();
    case Instruction::Call:
        return llvm::cast<llvm::CallInst>(I)->args();
    default:
        return I->operands();
    }
}

// Defined in MapArguments.cpp
llvm::Error mapArguments(const llvm::Function &F,
                         const AnnotationMapTy &AnnotationMap,
                         const DebugInfoMapTy &DebugInfo,
                         TempAllocationData &TAD);

// Defined in MapConstantExpressions.cpp
llvm::Error propagateConstantExpressions(CEmitter &C, const llvm::Function &F,
                                         const LinearBlocks &Blocks,
                                         const AnnotationMapTy &AnnotationMap,
                                         const DebugInfoMapTy &DebugInfo,
                                         const TcgGlobalMap &TcgGlobals,
                                         TempAllocationData &TAD);
// Defined in MapTemporaries.cpp
llvm::Error allocateTemporaries(const llvm::Function &F,
                                const LinearBlocks &Blocks,
                                const AnnotationMapTy &AnnotationMap,
                                const DebugInfoMapTy &DebugInfo, CEmitter &C,
                                const TcgGlobalMap &TcgGlobal,
                                TempAllocationData &TAD);

// Defined MapTcgOperations.cpp
llvm::Error mapTcgOperations(
    const LinearBlocks &Blocks, const TcgGlobalMap &TcgGlobals,
    const AnnotationMapTy &AnnotationMap,
    const llvm::SmallPtrSet<llvm::Function *, 16> &HasTranslatedFunction,
    const TempAllocationData &TAD, TcgEmitter &Tcg, CEmitter &C);
