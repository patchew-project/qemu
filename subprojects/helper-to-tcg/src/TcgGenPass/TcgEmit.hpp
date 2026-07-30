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

#include "TcgType.hpp"

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/IR/InstrTypes.h> // for CmpInst::Predicate
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include <string>

using llvm::CmpInst;
using llvm::Instruction;
using llvm::raw_ostream;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;
using llvm::Type;
using llvm::Value;

class CEmitter;

struct VectorLayout;

inline std::string getName(const TcgV &V) {
    if (!V.Name.empty() or V.ConstantExpression) {
        return V.Name;
    } else {
        switch (V.Kind) {
        case IrImmediate:
            return (!V.Name.empty()) ? V.Name
                                     : Twine("imm").concat(Twine(V.Id)).str();
        case IrValue:
            return Twine("tmp").concat(Twine(V.Id)).str();
        case IrPtr:
            return Twine("ptr").concat(Twine(V.Id)).str();
        case IrPtrToOffset:
            return Twine("vec").concat(Twine(V.Id)).str();
        case IrLabel:
            return Twine("label").concat(Twine(V.Id)).str();
        default:
            abort();
        };
    }
}

// Small helper class to construct `Twine`s on the stack.
template <size_t N> class StackTwine {
    llvm::SmallString<N> Str;

  public:
    StackTwine() = delete;
    StackTwine(Twine &&T) { T.toVector(Str); }
    operator StringRef() { return Str; }
};

class TcgEmitter {
    raw_ostream &Out;
    CEmitter &C;
    size_t AllocatedVectorMemory = 0;
    size_t NumVectorInstructions = 0;
    bool EmittedVectorMem = false;

    void genVecUnaryCall(StringRef Name, int ElementSize, const TcgV &Dst,
                         const TcgV &Src, const TcgV &Size);
    void genVecBinaryCall(StringRef Name, int ElementSize, const TcgV &Dst,
                          const TcgV &Src0, const TcgV &Src1, size_t Size);
    void genVecCall(StringRef Name, llvm::ArrayRef<const TcgV> Args,
                    const VectorSize OpSize, const VectorSize MaxSize);
    void genVecSizeChange(StringRef Name, uint8_t DstElementBits,
                          const TcgV &Dst, const TcgV &Src);

  public:
    bool NeedVectorSizeChangeOps = false;

    TcgEmitter(raw_ostream &Out, CEmitter &C) : Out(Out), C(C) {}

    inline size_t allocatedVectorMemory() const {
        return AllocatedVectorMemory;
    }
    inline size_t numVectorInstructions() const {
        return NumVectorInstructions;
    }

    const TcgV getGlobalEnv() {
        return TcgV::makeConstantExpression("tcg_env", {}, IrPtr);
    }

    const TcgV getDisasContext() {
        return TcgV::makeConstantExpression("ctx", {}, IrPtr);
    }

    // String representation of types
    const std::string getType(const TcgV &Value);

    inline const TcgV materialize(const TcgV &Value) {
        if (Value.Kind != IrImmediate) {
            return Value;
        }
        TcgV M = Value;
        M.Name = Twine("tcg_constant_i")
                     .concat(Twine((int)Value.tcgBitWidth()))
                     .concat("(")
                     .concat(getName(Value))
                     .concat(")")
                     .str();
        M.Kind = IrValue;
        return M;
    }

    template <typename I> void emitArgListTcg(const I Beg, const I End) {
        auto It = Beg;
        if (It != End) {
            Out << getName(*It);
            ++It;
        }
        while (It != End) {
            Out << ", " << getName(*It);
            ++It;
        }
    }

    template <typename I>
    void emitCall(const StringRef S, const I Beg, const I End) {
        Out << S << '(';
        auto It = Beg;
        if (It != End) {
            Out << *It;
            ++It;
        }
        while (It != End) {
            Out << ", " << *It;
            ++It;
        }
        Out << ");\n";
    }

    template <typename Iterator>
    void emitCallTcg(const StringRef S, Iterator Begin, Iterator End) {
        assert(Begin != End);
        Out << S << '(';
        Out << getName(*Begin);
        ++Begin;
        while (Begin != End) {
            Out << ", " << getName(*Begin);
            ++Begin;
        }
        Out << ");\n";
    }

    inline void emitArgListTcg(const std::initializer_list<TcgV> Args) {
        emitArgListTcg(Args.begin(), Args.end());
    }

    inline void emitCall(const StringRef S,
                         const std::initializer_list<StringRef> Args) {
        emitCall(S, Args.begin(), Args.end());
    }

    inline void emitCallTcg(StringRef S, std::initializer_list<TcgV> Args) {
        emitCallTcg(S, Args.begin(), Args.end());
    }

    inline void genCallHelper(const StringRef Helper,
                              const std::initializer_list<TcgV> Args) {
        auto Func = Twine("gen_").concat(Helper).str();
        emitCallTcg(Func, Args);
    }

    template <typename I>
    void genCallHelper(const StringRef Helper, I Beg, I End) {
        auto Func = Twine("gen_").concat(Helper).str();
        emitCallTcg(Func, Beg, End);
    }

    template <typename I>
    void genCallCFunc(const StringRef Name, TcgV Ret, I Beg, I End) {
        if (Ret.Kind != IrInvalid) {
            Out << getName(Ret) << " = ";
        }
        emitCallTcg(Name, Beg, End);
    }

    void genNewLabel();
    void genSetLabel(const TcgV &L);

    void defineNewTemp(const TcgV &Tcg);

    void genBr(const TcgV &L);

    void genExts(const TcgV &Dst, const TcgV &Src);
    void genExtI32I64(const TcgV &Dst, const TcgV &Src);
    void genExtrlI64I32(const TcgV &Dst, const TcgV &Src);
    void genExtuI32I64(const TcgV &Dst, const TcgV &Src);
    void genExtrhI64I32(const TcgV &Dst, const TcgV &Src);
    void genExtract(bool Sign, const TcgV &Dst, const TcgV &Src,
                    const TcgV &Offset, const TcgV &Length);
    void genDeposit(const TcgV &Dst, const TcgV &Into, const TcgV &From,
                    const TcgV &Offset, const TcgV &Length);

    void genPtrToValue(const TcgV &Dst, const TcgV &Src);
    void genValueToPtr(const TcgV &Dst, const TcgV &Src);

    void genConcat(const TcgV &Dst, const TcgV &Src1, const TcgV &Src2);
    void genMov(const TcgV &Dst, const TcgV &Src);
    void genMovPtr(const TcgV &Dst, const TcgV &Src);
    void genAddPtr(const TcgV &Dst, const TcgV &Ptr, const TcgV &Offset);
    void genBinOp(const TcgV &Dst, const Instruction::BinaryOps Opcode,
                  const TcgV &Src0, const TcgV &Src1);
    void genMovcond(const CmpInst::Predicate &Pred, const TcgV &Ret,
                    const TcgV &C1, const TcgV &C2, const TcgV &V1,
                    const TcgV &V2);
    void genSetcond(const CmpInst::Predicate &Pred, const TcgV &Dst,
                    const TcgV &Op1, const TcgV &Op2);
    void genBrcond(const CmpInst::Predicate &Pred, const TcgV &Arg1,
                   const TcgV &Arg2, const TcgV &Label);

    std::string getMemOp(uint8_t Size, uint8_t Endianness, uint8_t Sign);
    void genGuestLoad(const TcgV &Dst, const TcgV &Ptr, StringRef MemOp);
    void genGuestStore(const TcgV &Ptr, const TcgV &Src, StringRef MemOp);
    void genHostLoad(const TcgV &Dst, const TcgV &Ptr, const TcgV &Offset);
    void genHostLoadFromVec(const TcgV &Dst, const TcgV &Offset);
    void genHostStore(const TcgV &Ptr, const TcgV &Src);

    void genFunnelShl(const TcgV &Dst, const TcgV &Src0, const TcgV &Src1,
                      const TcgV &Shift);
    void genBitreverse(const TcgV &Dst, const TcgV &Src);
    void genAbs(const TcgV &Dst, const TcgV &Src);
    void genCountLeadingZeros(const TcgV &Dst, const TcgV &Src);
    void genCountTrailingZeros(const TcgV &Dst, const TcgV &Src);
    void genCountOnes(const TcgV &Dst, const TcgV &Src);
    void genByteswap(const TcgV &Dst, const TcgV &Src);
    void genUnsignedSatSub(const TcgV &Dst, const TcgV &Src0, const TcgV &Src1);

    // Vector ops.
    void genVecBinOpStr(StringRef Op, const TcgV &Dst, const TcgV &Src0,
                        const TcgV &Src1);
    void genVecBinOp(const Instruction::BinaryOps Opcode, const TcgV &Dst,
                     const TcgV &Src0, const TcgV &Src1);
    void genVecOrScalarBinOp(StringRef Op, const TcgV &Dst, const TcgV &Src0,
                             const TcgV &Src1);
    void genVecSignedSatAdd(const TcgV &Dst, const TcgV &Src0,
                            const TcgV &Src1);
    void genVecSignedSatSub(const TcgV &Dst, const TcgV &Src0,
                            const TcgV &Src1);
    void genVecUnsignedSatSub(const TcgV &Dst, const TcgV &Src0,
                              const TcgV &Src1);
    void genVecSignedMax(const TcgV &Dst, const TcgV &Src0, const TcgV &Src1);
    void genVecUnsignedMax(const TcgV &Dst, const TcgV &Src0, const TcgV &Src1);
    void genVecSignedMin(const TcgV &Dst, const TcgV &Src0, const TcgV &Src1);
    void genVecUnsignedMin(const TcgV &Dst, const TcgV &Src0, const TcgV &Src1);
    void genVecMemcpy(const TcgV &Dst, const TcgV &Src, const TcgV &Size);
    void genVecMemset(const TcgV &Dst, const TcgV &Src, const TcgV &Size);
    void genVecSplat(const TcgV &Dst, const TcgV &Src);
    void genVecArrSplat(const TcgV &Dst, const TcgV &Src);
    void genVecBitsel(const TcgV &Dst, const TcgV &Cond, const TcgV &Src0,
                      const TcgV &Src1);
    void genVecCmp(const TcgV &Dst, const CmpInst::Predicate &Pred,
                   const TcgV &Src0, const TcgV &Src1);
    void genVecNot(const TcgV &Dst, const TcgV &Src);
    void genVecTrunc(uint8_t DstElementBits, const TcgV &Dst, const TcgV &Src);
    void genVecSext(uint8_t DstElementBits, const TcgV &Dst, const TcgV &Src);
    void genVecZext(uint8_t DstElementBits, const TcgV &Dst, const TcgV &Src);
};

class CEmitter {
    std::string mapBinOp(const Instruction::BinaryOps &Opcode, const TcgV &Src0,
                         const TcgV &Src1);

  public:
    CEmitter() {}

    std::string intType(bool Signed, uint8_t LlvmSize);
    TcgV ptrAdd(const TcgV &Ptr, const TcgV &Offset);
    TcgV ternary(const TcgV &Cond, const TcgV &True, const TcgV &False);
    TcgV deref(const TcgV &Ptr, ValueSize Size);
    TcgV compare(const CmpInst::Predicate &Pred, const TcgV &Src0,
                 const TcgV &Src1);
    TcgV extend(bool Signed, const TcgV &V, ValueSize Size);
    TcgV binop(Instruction::BinaryOps Opcode, const TcgV &Src0,
               const TcgV &Src1);
};

// The below function deal with a lot of code generation that we expect to be
// present in the output.

// Emits `VectorMem` struct definition along with allocation funcs needed for
// producing vector temporaries.
void emitVectorMem(raw_ostream &Out);
// Emit `gen_vec_zext*()` and friends for performing vector size changing
// operations, taking target vector layout into account.
void emitVectorSizeChangeOps(raw_ostream &OutSource, raw_ostream &OutHeader,
                             const VectorLayout &VL);
// Emits gen_helper_*() definition for helpers that failed to translate.
void emitHelperGen(raw_ostream &Out, StringRef Name, Type *ReturnTy,
                   llvm::ArrayRef<Type *> ArgTys);
