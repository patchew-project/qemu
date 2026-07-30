#pragma once

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

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <stdint.h>

// `TcgGlobal` describes a field in a struct which has a mapping to a TCG
// global.  `Size`, `NumElements`, and `Stride` describe the mapped type, all
// types are assumed to be integer or arrays of integers.  `Code` is the
// expression to be emitted when accessing the mapped global, usually the
// variable name.
struct TcgGlobal {
    llvm::StringRef Code;
    uint64_t Size;
    uint64_t NumElements;
    uint64_t Stride;
};

// Array of maps between offsets into a mapped struct to the resulting global
// type, outer array is indexed by the base struct type to handle multiple
// struct-to-global mappings.
using TcgGlobalMap = llvm::SmallVector<llvm::DenseMap<uint32_t, TcgGlobal>, 1>;
