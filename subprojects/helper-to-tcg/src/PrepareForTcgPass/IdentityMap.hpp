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

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

//
// Transformation of the IR, taking what would become trivial unary operations
// and maps them to a single @IdentityMap pseudo instruction.
//
// To motivate further, in order to produce nice IR on the other end, generally
// the operands of these trivial expressions needs to be forwarded and treated
// as the destination value (identity mapped).  However, directly removing these
// instructions will result in broken LLVM IR (consider zext i8, i32 where both
// the source and destination would map to TCGv_i32).
//
// Moreover, handling these identity mapped values in an adhoc way quickly
// becomes cumbersome and spreads throughout the codebase.  Therefore,
// introducing @IdentityMap allows code further down the pipeline to ignore the
// source of the identity map.
//

void identityMap(llvm::Module &M, llvm::Function &F);
