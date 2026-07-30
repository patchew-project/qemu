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

#include "DebugInfo.hpp"
#include "ValueMapping.hpp"

#include "Error.hpp"
#include "FunctionAnnotation.hpp"
#include "TcgType.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

#define DEBUG_TYPE "map-arguments"

//
// Map Arguments
//
// Assign `TcgV`s to function arguments, taking function annotations into
// account to force types.  Mapping them early and separately both makes sure
// that no other pass accidently assign to them, and separates out any special
// case logic that's necessary.
//

using namespace llvm;

static Expected<TcgV> mapIntegerArgument(TempAllocationData &TAD,
                                         const DebugInfoMapTy &DebugInfo,
                                         const Argument *Arg,
                                         uint8_t Annotations) {
    auto *IntTy = cast<IntegerType>(Arg->getType());
    auto Size = ValueSize::fromLlvmType(IntTy);
    if (!Size) {
        return Size.takeError();
    }

    StringRef Name = getDebugVarName(DebugInfo, Arg);

    if ((Annotations & (uint8_t)ArgumentAnnotation::Immediate) != 0) {
        auto Tcg = TcgV::makeImmediate(Name, *Size);
        return TAD.map(Arg, Tcg);
    } else {
        auto Tcg = TcgV::makeTemp(*Size, IrValue);
        Tcg.Name = Name;
        return TAD.map(Arg, Tcg);
    }
}

static Expected<TcgV> mapPointerArgument(TempAllocationData &TAD,
                                         const DebugInfoMapTy &DebugInfo,
                                         const Argument *Arg,
                                         uint8_t Annotations) {
    // If the value has an associated name from the debug information, use it
    StringRef Name{};
    if (auto It = DebugInfo.find(Arg); It != DebugInfo.end()) {
        Name = It->second.VarName;
    }

    if ((Annotations & (uint8_t)ArgumentAnnotation::PtrToOffset) != 0) {
        auto Tcg = TcgV::makeVector({});
        Tcg.Name = Name;
        return TAD.map(Arg, Tcg);
    } else {
        auto Tcg = TcgV::makeTemp({}, IrPtr);
        Tcg.Name = Name;
        return TAD.map(Arg, Tcg);
    }
}

static Expected<TcgV> mapArgument(TempAllocationData &Data,
                                  const DebugInfoMapTy &DebugInfo,
                                  const Argument *Arg, uint8_t Annotations) {
    // We only map each argument once, assert it's not been mapped previously.
    assert(!Data.Map.count(Arg));

    Type *Ty = Arg->getType();
    if (isa<IntegerType>(Ty)) {
        return mapIntegerArgument(Data, DebugInfo, Arg, Annotations);
    } else if (isa<PointerType>(Ty)) {
        return mapPointerArgument(Data, DebugInfo, Arg, Annotations);
    }

    return mkError("Unable to map value ", Arg);
}

Error mapArguments(const llvm::Function &F,
                   const AnnotationMapTy &AnnotationMap,
                   const DebugInfoMapTy &DebugInfo, TempAllocationData &Data) {
    // Map arguments
    for (size_t I = 0; I < F.arg_size(); ++I) {
        const Argument *Arg = F.getArg(I);
        auto It = AnnotationMap.find(&F);
        const uint8_t Annotations =
            (It != AnnotationMap.end()) ? It->second.getArgFlag(I) : 0;
        Expected<TcgV> TcgArg = mapArgument(Data, DebugInfo, Arg, Annotations);
        if (!TcgArg) {
            return TcgArg.takeError();
        }
        LLVM_DEBUG({
            dbgs() << "Mapped argument " << *Arg << " to: \n";
            TcgArg->dump(dbgs());
        });
    }

    return Error::success();
}
