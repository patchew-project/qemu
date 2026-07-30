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

#include "PseudoInst.hpp"
#include "LlvmCompat.hpp"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Twine.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Casting.h>

using namespace llvm;

// Define an array `PseudoInstName[]`, indexed by `PseudoInst` and mapping
// to a string representation of the enum.
#define PSEUDO_INST_DEF(name, ret, args) #name
static const char *PseudoInstName[] = {
#include "PseudoInst.inc"
};
#undef PSEUDO_INST_DEF

// Define an array `PseudoInstArgTypes[]` indexed by `PseudoInst` and
// mapping to an array of `PseudoInstArg` representing allowed argument types.
#define PSEUDO_INST_ARGVEC(...) {__VA_ARGS__}

#define PSEUDO_INST_DEF(name, ret, args) args
static const SmallVector<PseudoInstArg, 6> PseudoInstArgTypes[] = {
#include "PseudoInst.inc"
};
#undef PSEUDO_INST_DEF
#undef PSEUDO_INST_ARGVEC

// In order to map from a `Function *` to a `PseudoInst`, we keep a map
// of all functions created, this simplifies mapping of callees to
// a `PseudoInst` that can be switched over.
static DenseMap<Function *, PseudoInst> MapFuncToInst;

// Converts llvm `Type`s to a string representation
// that can be embedded in function names for basic overloading.
//
// Ex.
//
//      [8 x i8] -> "a8xi8"
//      <128 x i8> -> "v128xi8"
//
// LLVM has an implementation of a similar function used by intrinsics,
// called `getMangledTypeStr`, but it's not exposed.
inline std::string getMangledTypeStr(Type *Ty) {
    std::string TypeStr = "";
    llvm::raw_string_ostream TypeStream(TypeStr);
    switch (Ty->getTypeID()) {
    case Type::ArrayTyID: {
        auto *ArrayTy = cast<ArrayType>(Ty);
        std::string ElementStr = getMangledTypeStr(ArrayTy->getElementType());
        TypeStream << "a" << ArrayTy->getNumElements() << "x" << ElementStr;
    } break;
    case Type::FixedVectorTyID: {
        auto *VecTy = cast<VectorType>(Ty);
        uint32_t ElementCount = compat::getVectorElementCount(VecTy);
        std::string ElementStr = getMangledTypeStr(VecTy->getElementType());
        TypeStream << "v" << ElementCount << "x" << ElementStr;
    } break;
    case Type::StructTyID: {
        auto *StructTy = cast<StructType>(Ty);
        TypeStream << StructTy->getName();
    } break;
    case Type::IntegerTyID: {
        auto *IntTy = cast<IntegerType>(Ty);
        TypeStream << "i" << IntTy->getBitWidth();
    } break;
    case Type::PointerTyID: {
        TypeStream << "p";
    } break;
    default:
        abort();
    }
    return TypeStream.str();
}

// Access functions into the static defined above.

const char *pseudoInstName(PseudoInst Inst) { return PseudoInstName[Inst]; }

uint8_t pseudoInstArgCount(PseudoInst Inst) {
    return PseudoInstArgTypes[Inst].size();
}

llvm::ArrayRef<PseudoInstArg> pseudoInstArgTypes(PseudoInst Inst) {
    return PseudoInstArgTypes[Inst];
}

// Match LLVM type against `PseudoIntsArg`.
static void assertMatchingType(PseudoInstArg Expected, Type *Ty) {
    const Type::TypeID Id = Ty->getTypeID();
    switch (Expected) {
    case ArgAny:
        return;
    case ArgInt:
        assert(Id == Type::IntegerTyID);
        return;
    case ArgVec:
        assert(Id == Type::FixedVectorTyID);
        return;
    case ArgPtr:
        assert(Id == Type::PointerTyID);
        return;
    case ArgLabel:
        assert(Id == Type::LabelTyID);
        return;
    case ArgVoid:
        assert(Id == Type::VoidTyID);
        return;
    default:
        abort();
    };
}

llvm::FunctionCallee pseudoInstFunction(llvm::Module &M, PseudoInst Inst,
                                        llvm::Type *RetType,
                                        llvm::ArrayRef<llvm::Type *> ArgTypes) {
    ArrayRef<PseudoInstArg> PArgTypes = pseudoInstArgTypes(Inst);
    assert(PArgTypes.size() == ArgTypes.size());
    for (size_t I = 0; I < ArgTypes.size(); ++I) {
        assertMatchingType(PArgTypes[I], ArgTypes[I]);
    }

    auto *FT = llvm::FunctionType::get(RetType, ArgTypes, false);

    std::string FnName{PseudoInstName[Inst]};
    if (!RetType->isVoidTy()) {
        FnName += ".";
        FnName += getMangledTypeStr(RetType);
    }
    for (llvm::Type *Ty : ArgTypes) {
        if (Ty->isLabelTy()) {
            continue;
        }
        FnName += ".";
        FnName += getMangledTypeStr(Ty);
    }

    llvm::FunctionCallee Fn = M.getOrInsertFunction(FnName, FT);
    auto *F = cast<Function>(Fn.getCallee());
    MapFuncToInst.insert({F, Inst});

    return Fn;
}

llvm::CallInst *createPseudoInstCall(llvm::Module &M, IRBuilder<> &Builder,
                                     PseudoInst Inst, llvm::Type *RetType,
                                     llvm::ArrayRef<llvm::Value *> Values) {
    SmallVector<Type *, 8> ArgTypes;
    for (Value *V : Values) {
        ArgTypes.push_back(V->getType());
    }

    FunctionCallee Fn = pseudoInstFunction(M, Inst, RetType, ArgTypes);
    return Builder.CreateCall(Fn, Values);
}

PseudoInst getPseudoInstFromCall(const CallInst *Call) {
    Function *F = Call->getCalledFunction();
    auto It = MapFuncToInst.find(F);
    if (It == MapFuncToInst.end()) {
        return InvalidPseudoInst;
    }
    return It->second;
}
