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

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>
#include <stdint.h>

namespace llvm {
class Function;
}

// Different kind of function annotations which control the behaviour
// of helper-to-tcg.
enum class ArgumentAnnotation : uint8_t {
    // Declares a list of arguments as immediates
    Immediate = 1,
    // Declares a list of arguments as vectors, represented by offsets into
    // the CPU state
    PtrToOffset = 2,
};

// Different kind of function annotations which control the behaviour
// of helper-to-tcg.
enum class FunctionAnnotation : uint8_t {
    // Function should be translated
    HelperToTcg = 1,
    // Return value of function is an immediate
    ReturnsImmediate = 2,
};

// Annotation data which may be attached to a function
class Annotations {
    // 8-bit flag for each argument in a function, fields defined by
    // `ArgumentAnnotions`.
    llvm::SmallVector<uint8_t, 4> ArgumentAnnotations;
    // Flag of function annotations, fields defined by `FunctionsAnnotations`.
    uint8_t FunctionAnnotations = 0;

  public:
    inline uint8_t getArgFlag(size_t Index) const {
        if (Index >= ArgumentAnnotations.size()) {
            return 0;
        }
        return ArgumentAnnotations[Index];
    }

    // Getters and setters for annotations flags.

    inline void set(FunctionAnnotation FA) {
        FunctionAnnotations |= (uint8_t)FA;
    }

    inline void set(size_t Index, ArgumentAnnotation AA) {
        if (Index >= ArgumentAnnotations.size()) {
            // Resizing will default initialize any new elements.
            ArgumentAnnotations.resize(Index + 1);
        }
        ArgumentAnnotations[Index] |= (uint8_t)AA;
    }

    inline bool isSet(FunctionAnnotation FA) const {
        return (FunctionAnnotations & (uint8_t)FA) != 0;
    }

    inline bool isSet(size_t Index, ArgumentAnnotation AA) const {
        uint8_t Flag = getArgFlag(Index);
        return (Flag & (uint8_t)AA) != 0;
    }

    // Pretty printing debug information
    inline void dump(llvm::raw_ostream &Out) const {
        Out << "Annotations:\n";
        Out << "  Function: " << llvm::format_hex(FunctionAnnotations, 4)
            << "\n";
        for (size_t I = 0; I < ArgumentAnnotations.size(); ++I) {
            const uint8_t Flag = ArgumentAnnotations[I];
            Out << "  Argument[" << I << "]: " << llvm::format_hex(Flag, 4)
                << "\n";
        }
    }
};

// Mapping from functions to annotations, this is the main structure to be used
// by other parts of the codebase when referencing annotations, filled out by
// `PrepareForOptPass`.
using AnnotationMapTy = llvm::DenseMap<llvm::Function *, Annotations>;
