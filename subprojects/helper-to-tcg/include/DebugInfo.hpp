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

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/ValueMap.h>

namespace llvm {
class Value;
}

// StringRefs will refer do debug metadata fields which have the same
// lifetime as the LLVMContext and survive accross optimizations and
// possible deletions of functions/variables.
struct DebugInfo {
    llvm::StringRef VarName;
    llvm::StringRef BaseTypeName;
};

using DebugInfoMapTy = llvm::ValueMap<const llvm::Value *, DebugInfo>;

// Helper to get the variable name from debug info associated with a particular
// value, or default construct an empty name.
inline llvm::StringRef getDebugVarName(const DebugInfoMapTy &DM,
                                       const llvm::Value *V) {
    auto It = DM.find(V);
    if (It != DM.end()) {
        return It->second.VarName;
    }
    return {};
}
