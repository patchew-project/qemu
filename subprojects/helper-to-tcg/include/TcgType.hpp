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

#include "Error.hpp"
#include "LlvmCompat.hpp"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/raw_ostream.h>

#include <assert.h>
#include <stdint.h>
#include <string>
#include <utility> // std::pair

// Data representing allowed vector and value sizes.

// Sizes corresponding to a TCGv_i* register.
enum AllowedTcgSize : uint8_t {
    T32 = 32,
    T64 = 64,
    T128 = 128,
};

// Sizes corresponding of allowed LLVM values, operations smaller than
// `AllowedTcgSize` act on these sizes.
enum AllowedLlvmSize : uint8_t {
    I1 = 1,
    I8 = 8,
    I16 = 16,
    I32 = 32,
    I64 = 64,
};

// Despite limiting sizes to an enum, when assigned from runtime values they may
// still differ.
inline bool verifyLlvmSize(AllowedLlvmSize S) {
    switch (S) {
    case I1:
    case I8:
    case I16:
    case I32:
    case I64:
        return true;
    default:
        return false;
    };
}

// Size of an integer value, combining a TCG and LLVM size.
struct ValueSize {
    AllowedTcgSize TcgBitWidth;
    AllowedLlvmSize LlvmBitWidth;

    static llvm::Expected<ValueSize> fromBitWidth(size_t BitWidth) {
        ValueSize Ret;
        if (BitWidth <= 32) {
            Ret.TcgBitWidth = T32;
        } else if (BitWidth <= 64) {
            Ret.TcgBitWidth = T64;
        } else {
            Ret.TcgBitWidth = T128;
        }
        Ret.LlvmBitWidth = (AllowedLlvmSize)BitWidth;
        // There's no runtime sanity checks that an enum is not just any integer
        // value, so verify manually.
        if (verifyLlvmSize(Ret.LlvmBitWidth)) {
            return Ret;
        } else {
            return mkError("Invalid bit width");
        }
    }

    static llvm::Expected<ValueSize> fromLlvmType(llvm::IntegerType *Ty) {
        return fromBitWidth(Ty->getBitWidth());
    }
};

struct VectorSize {
    uint8_t ElementCount;
    AllowedLlvmSize ElementBitWidth;

    inline size_t bytes() const { return (ElementCount * ElementBitWidth) / 8; }

    static llvm::Expected<VectorSize> fromLlvmType(llvm::VectorType *Ty) {
        auto *IntTy = llvm::dyn_cast<llvm::IntegerType>(Ty->getElementType());
        if (!IntTy) {
            return mkError(
                "Vectors of non-integer element type not supported!\n");
        }
        VectorSize Ret{
            (uint8_t)compat::getVectorElementCount(Ty),
            (AllowedLlvmSize)IntTy->getBitWidth(),
        };
        // There's no runtime sanity checks that an enum is not just any integer
        // value, so verify manually.
        if (verifyLlvmSize(Ret.ElementBitWidth)) {
            return Ret;
        } else {
            return mkError("Invalid bit width");
        }
    }
};

union TcgSize {
    VectorSize Vec;
    ValueSize Val;
};

// clang-format off
enum TcgKind : uint8_t {
    IrInvalid     = 0,
    IrValue       = 1,  // TCG register value (TCGv_i*)
    IrImmediate   = 2,  // Immediate argument to TCG operation ([u]int*_t)
    IrPtr         = 4,  // TCG host pointer (TCGv_ptr)
    IrPtrToOffset = 8,  // Target "gvec" vector (intptr_t)
    IrLabel       = 16, // TCG label (TCGv_label)
};
// clang-format on

// Describes a single value to be output in TCG, discriminated by `TcgKind`.
//
// This data is designed to be copied around and each value is identified via a
// unique `Id`, as a result the "same" `TcgV` can take on e.g. different sizes
// at different points in function, which allows one TCG variable to represent
// multiple smaller "logical" LLVM sizes.
//
// TODO:
//
//   * There is overlap between `Kind == IrImmediate` and `(ConstantExpression
//     and Kind == IrValue)` that is not obvious, the condition `!Name.empty()`
//     is also rather similar to `ConstantExpression` in a lot of cases.
//
//   * `Name` should not be string, this is resulting in redundant copies and is
//     also completely unnecessary given our usage pattern.  `Name` is never
//     modified, and all strings are created by the same few passes.  A better
//     solution would be to use a `StringSaver/BumpAllocator` in `CEmitter` and
//     use `StringRef`s everywhere else.
//
struct TcgV {
    uint16_t Id = 0;
    TcgKind Kind = IrInvalid;
    TcgSize Size;
    bool ConstantExpression = false;
    std::string Name = "";

    // Static id used to identify `TcgV`s
    inline static uint16_t IdCounter = 0;
    static void resetId() { IdCounter = 0; }
    static uint16_t getId() { return IdCounter++; }

    // Helper functions for accessing size fields.
    inline ValueSize intSize() const {
        assert(Kind == IrImmediate or Kind == IrValue);
        return Size.Val;
    }

    inline VectorSize vecSize() const {
        assert(Kind == IrPtrToOffset);
        return Size.Vec;
    }

    // Return as `int` to reduce casts when printing and to ease comparisons.
    inline int tcgBitWidth() const { return intSize().TcgBitWidth; }
    inline int llvmBitWidth() const { return intSize().LlvmBitWidth; }

    static TcgV makeVector(VectorSize Size) {
        return TcgV({}, {.Vec = Size}, IrPtrToOffset);
    }

    static TcgV makeImmediate(llvm::StringRef Name, ValueSize Size) {
        return TcgV(Name.str(), {.Val = Size}, IrImmediate);
    }

    static TcgV makeTemp(ValueSize Size, TcgKind Kind) {
        return TcgV({}, {.Val = Size}, Kind);
    }

    static TcgV makeConstantExpression(llvm::StringRef Expression,
                                       ValueSize Size, TcgKind Kind) {
        TcgV Tcg(Expression.str(), {.Val = Size}, Kind);
        Tcg.ConstantExpression = true;
        return Tcg;
    }

    static TcgV makeLabel() { return TcgV("", {.Val = {T32, I32}}, IrLabel); }

    TcgV() = default;

    TcgV(std::string Name, TcgSize Size, TcgKind Kind)
        : Id(getId()), Kind(Kind), Size(Size), Name(Name) {}

    // Equality between two values it determined only by the assigned id,
    // consider a 16- to 8-bit truncation:
    //
    //   %1 = trunc i8, i16 %0,
    //
    // which after identity mapping becomes
    //
    //   %1 = call i8 @IdentityMap.i16.i8(i16 %0).
    //
    // Identity mapping will copy the `TcgV` assigned to `%0` to `%1`, but `%1`
    // will retain its `LlvmSize` needed to emit correctly sized operations down
    // the line.  Despite differing in `LlvmSize` both `%0` and `%1` will be
    // emitted as the same TCG value with type `TCGv_i32`.
    bool operator==(const TcgV &Other) const { return Other.Id == Id; }
    bool operator!=(const TcgV &Other) const { return !operator==(Other); }

    // Print out struct fields, useful for debugging.
    inline void dump(llvm::raw_ostream &Out) const {
        Out << "TcgV " << Id;
        if (!Name.empty()) {
            Out << " (" << Name << ")";
        }
        Out << ":\n";
        Out << "  Kind: ";
        switch (Kind) {
        case IrInvalid:
            Out << "IrInvalid\n";
            break;
        case IrValue:
            Out << "IrValue\n";
            break;
        case IrImmediate:
            Out << "IrImmediate\n";
            break;
        case IrPtr:
            Out << "IrPtr\n";
            break;
        case IrPtrToOffset:
            Out << "IrPtrToOffset\n";
            break;
        case IrLabel:
            Out << "IrLabel\n";
            break;
        }
        if (Kind == IrPtrToOffset) {
            Out << "  VectorElementCount: " << (int)Size.Vec.ElementCount
                << "\n";
            Out << "  VectorElementBitWidth: " << (int)Size.Vec.ElementBitWidth
                << "\n";
        } else {
            Out << "  TcgSize: " << (int)Size.Val.TcgBitWidth << "\n";
            Out << "  LlvmSize: " << (int)Size.Val.LlvmBitWidth << "\n";
        }
        Out << "  ConstantExpression: " << ConstantExpression << "\n";
    }
};

// Helper function for verifying that a set of `TcgV` are of acceptable types,
// use with flags like
//
//   assertKinds({{Dst, IrPtr}, {Src, IrValue | IrImmediate}))
//
inline void assertKinds(llvm::ArrayRef<std::pair<const TcgV, uint8_t>> Pairs) {
    for (auto &[Tcg, Flag] : Pairs) {
        assert(Tcg.Kind & Flag);
    }
}
