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
#include "TcgGlobalMap.hpp"
#include <llvm/ADT/StringMap.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

//
// Transform of module that converts `getelementptr` (GEP) operators to
// pseudo instructions, either:
//
//   1. `call @AccessGlobalArray(StructIndex, Offset, Index)`
//      if `Offset` is mapped to a global TCG array;
//
//   2. `call @AccessGlobalValue(StructIndex, Offset)`
//      if `Offset` is mapped to a global TCG value;
//
//   3. Pointer math, if above fails.
//
// `StructIndex` is a an increasing integer assigned to each mapped struct type,
// usually only `CPUArchState` is mapped, so `StructIndex` would be 0.
//

void transformGEPs(llvm::Module &M, llvm::Function &F,
                   const TcgGlobalMap &TcgGlobals,
                   const llvm::StringMap<size_t> &TypeIndexMap,
                   const DebugInfoMapTy &DebugInfo);
