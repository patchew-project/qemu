/*
 *  Header file for Loongson 2F disassembler component of QEMU
 *
 *  Copyright (C) 2020  Stefan Brankovic <stefan.brankovic@syrmia.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef DISAS_LOONGSON2F_H
#define DISAS_LOONGSON2F_H

#include "disas/dis-asm.h"

class Fields32
{
public:
    virtual void decode_fields32(uint32_t insn) = 0;
};

class Fields32RdRsRt : public Fields32
{
protected:
    int rd;
    int rs;
    int rt;
public:
    int getRd();
    int getRs();
    int getRt();
};

class Fields32RdRsRtD0 : public Fields32RdRsRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32ImmRsRt : public Fields32
{
protected:
    int imm;
    int rs;
    int rt;
public:
    int getImm();
    int getRs();
    int getRt();
};

class Fields32ImmRsRtD0 : public Fields32ImmRsRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32ImmRsRtD1 : public Fields32ImmRsRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32RdRs : public Fields32
{
protected:
    int rd;
    int rs;
public:
    int getRd();
    int getRs();
};

class Fields32RdRsD0 : public Fields32RdRs
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32Rs : public Fields32
{
protected:
    int rs;
public:
    int getRs();
};

class Fields32RsD0 : public Fields32Rs
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32BaseOffsetRt : public Fields32
{
protected:
    int base;
    int offset;
    int rt;
public:
    int getBase();
    int getOffset();
    int getRt();
};

class Fields32BaseOffsetRtD0 : public Fields32BaseOffsetRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32ImmRt : public Fields32
{
protected:
    int imm;
    int rt;
public:
    int getImm();
    int getRt();
};

class Fields32ImmRtD0 : public Fields32ImmRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32Rd : public Fields32
{
protected:
    int rd;
public:
    int getRd();
};

class Fields32RdD0 : public Fields32Rd
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32Stype : public Fields32
{
protected:
    int stype;
public:
    int getStype();
};

class Fields32StypeD0 : public Fields32Stype
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32CodeRsRt : public Fields32
{
protected:
    int code;
    int rs;
    int rt;
public:
    int getCode();
    int getRs();
    int getRt();
};

class Fields32CodeRsRtD0 : public Fields32CodeRsRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32ImmRs : public Fields32
{
protected:
    int imm;
    int rs;
public:
    int getImm();
    int getRs();
};

class Fields32ImmRsD0 : public Fields32ImmRs
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32FdFs : public Fields32
{
protected:
    int fd;
    int fs;
public:
    int getFd();
    int getFs();
};

class Fields32FdFsD0 : public Fields32FdFs
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32FdFsFt : public Fields32
{
protected:
    int fd;
    int fs;
    int ft;
public:
    int getFd();
    int getFs();
    int getFt();
};

class Fields32FdFsFtD0 : public Fields32FdFsFt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32Offset : public Fields32
{
protected:
    int offset;
public:
    int getOffset();
};

class Fields32OffsetD0 : public Fields32Offset
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32FsFt : public Fields32
{
protected:
    int fs;
    int ft;
public:
    int getFs();
    int getFt();
};

class Fields32FsFtD0 : public Fields32FsFt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32FsRt : public Fields32
{
protected:
    int fs;
    int rt;
public:
    int getFs();
    int getRt();
};

class Fields32FsRtD0 : public Fields32FsRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32BaseFtOffset : public Fields32
{
protected:
    int base;
    int ft;
    int offset;
public:
    int getBase();
    int getFt();
    int getOffset();
};

class Fields32BaseFtOffsetD0 : public Fields32BaseFtOffset
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32OffsetRsRt : public Fields32
{
protected:
    int offset;
    int rs;
    int rt;
public:
    int getOffset();
    int getRs();
    int getRt();
};

class Fields32OffsetRsRtD0 : public Fields32OffsetRsRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32OffsetRs : public Fields32
{
protected:
    int offset;
    int rs;
public:
    int getOffset();
    int getRs();
};

class Fields32OffsetRsD0 : public Fields32OffsetRs
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32Code : public Fields32
{
protected:
    int code;
public:
    int getCode();
};

class Fields32CodeD0 : public Fields32Code
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32Cop_fun : public Fields32
{
protected:
    int cop_fun;
public:
    int getCop_fun();
};

class Fields32Cop_funD0 : public Fields32Cop_fun
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32RsRt : public Fields32
{
protected:
    int rs;
    int rt;
public:
    int getRs();
    int getRt();
};

class Fields32RsRtD0 : public Fields32RsRt
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32RdRtSa : public Fields32
{
protected:
    int rd;
    int rt;
    int sa;
public:
    int getRd();
    int getRt();
    int getSa();
};

class Fields32RdRtSaD0 : public Fields32RdRtSa
{
public:
    void decode_fields32(uint32_t insn);
};

class Fields32Instr_index : public Fields32
{
protected:
    int instr_index;
public:
    int getInstr_index();
};

class Fields32Instr_indexD0 : public Fields32Instr_index
{
public:
    void decode_fields32(uint32_t insn);
};

class Instruction32
{
protected:
    uint32_t opcode32;
    uint32_t mask32;
    Fields32 *fields32;

    void getAlias(char *buffer, int regNo);
public:
    virtual bool disas_output(disassemble_info *info) = 0;
    virtual ~Instruction32()  = 0;
};

class ADD : public Instruction32
{
public:
    ADD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ADDI : public Instruction32
{
public:
    ADDI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ADDIU : public Instruction32
{
public:
    ADDIU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ADDU : public Instruction32
{
public:
    ADDU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class AND : public Instruction32
{
public:
    AND(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ANDI : public Instruction32
{
public:
    ANDI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BEQ : public Instruction32
{
public:
    BEQ(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BEQL : public Instruction32
{
public:
    BEQL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BGEZ : public Instruction32
{
public:
    BGEZ(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BGEZAL : public Instruction32
{
public:
    BGEZAL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BGEZALL : public Instruction32
{
public:
    BGEZALL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BGEZL : public Instruction32
{
public:
    BGEZL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BGTZ : public Instruction32
{
public:
    BGTZ(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BGTZL : public Instruction32
{
public:
    BGTZL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BLEZ : public Instruction32
{
public:
    BLEZ(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BLEZL : public Instruction32
{
public:
    BLEZL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BLTZ : public Instruction32
{
public:
    BLTZ(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BLTZAL : public Instruction32
{
public:
    BLTZAL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BLTZALL : public Instruction32
{
public:
    BLTZALL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BLTZL : public Instruction32
{
public:
    BLTZL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BNE : public Instruction32
{
public:
    BNE(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BNEL : public Instruction32
{
public:
    BNEL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BREAK : public Instruction32
{
public:
    BREAK(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class COP0 : public Instruction32
{
public:
    COP0(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class COP3 : public Instruction32
{
public:
    COP3(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DADD : public Instruction32
{
public:
    DADD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DADDI : public Instruction32
{
public:
    DADDI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DADDIU : public Instruction32
{
public:
    DADDIU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DADDU : public Instruction32
{
public:
    DADDU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DDIV : public Instruction32
{
public:
    DDIV(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DDIVU : public Instruction32
{
public:
    DDIVU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DIV : public Instruction32
{
public:
    DIV(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DIVU : public Instruction32
{
public:
    DIVU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DMULT : public Instruction32
{
public:
    DMULT(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DMULTU : public Instruction32
{
public:
    DMULTU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSLL : public Instruction32
{
public:
    DSLL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSLL32 : public Instruction32
{
public:
    DSLL32(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSLLV : public Instruction32
{
public:
    DSLLV(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSRA : public Instruction32
{
public:
    DSRA(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSRA32 : public Instruction32
{
public:
    DSRA32(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSRAV : public Instruction32
{
public:
    DSRAV(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSRL : public Instruction32
{
public:
    DSRL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSRL32 : public Instruction32
{
public:
    DSRL32(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSRLV : public Instruction32
{
public:
    DSRLV(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSUB : public Instruction32
{
public:
    DSUB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSUBU : public Instruction32
{
public:
    DSUBU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class J : public Instruction32
{
public:
    J(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class JAL : public Instruction32
{
public:
    JAL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class JALR : public Instruction32
{
public:
    JALR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class JR : public Instruction32
{
public:
    JR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LB : public Instruction32
{
public:
    LB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LBU : public Instruction32
{
public:
    LBU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LD : public Instruction32
{
public:
    LD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LDC2 : public Instruction32
{
public:
    LDC2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LDL : public Instruction32
{
public:
    LDL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LDR : public Instruction32
{
public:
    LDR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LH : public Instruction32
{
public:
    LH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LHU : public Instruction32
{
public:
    LHU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LL : public Instruction32
{
public:
    LL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LLD : public Instruction32
{
public:
    LLD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LUI : public Instruction32
{
public:
    LUI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LW : public Instruction32
{
public:
    LW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LWC2 : public Instruction32
{
public:
    LWC2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LWC3 : public Instruction32
{
public:
    LWC3(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LWL : public Instruction32
{
public:
    LWL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LWR : public Instruction32
{
public:
    LWR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LWU : public Instruction32
{
public:
    LWU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MFHI : public Instruction32
{
public:
    MFHI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MFLO : public Instruction32
{
public:
    MFLO(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MTHI : public Instruction32
{
public:
    MTHI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MTLO : public Instruction32
{
public:
    MTLO(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MULT : public Instruction32
{
public:
    MULT(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MULTU : public Instruction32
{
public:
    MULTU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class NOR : public Instruction32
{
public:
    NOR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class OR : public Instruction32
{
public:
    OR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ORI : public Instruction32
{
public:
    ORI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SB : public Instruction32
{
public:
    SB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SC : public Instruction32
{
public:
    SC(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SCD : public Instruction32
{
public:
    SCD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SD : public Instruction32
{
public:
    SD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SDC2 : public Instruction32
{
public:
    SDC2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SDL : public Instruction32
{
public:
    SDL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SDR : public Instruction32
{
public:
    SDR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SH : public Instruction32
{
public:
    SH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLL : public Instruction32
{
public:
    SLL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLLV : public Instruction32
{
public:
    SLLV(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLT : public Instruction32
{
public:
    SLT(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLTI : public Instruction32
{
public:
    SLTI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLTIU : public Instruction32
{
public:
    SLTIU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLTU : public Instruction32
{
public:
    SLTU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SRA : public Instruction32
{
public:
    SRA(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SRAV : public Instruction32
{
public:
    SRAV(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SRL : public Instruction32
{
public:
    SRL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SRLV : public Instruction32
{
public:
    SRLV(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SUB : public Instruction32
{
public:
    SUB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SUBU : public Instruction32
{
public:
    SUBU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SW : public Instruction32
{
public:
    SW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SWC2 : public Instruction32
{
public:
    SWC2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SWC3 : public Instruction32
{
public:
    SWC3(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SWL : public Instruction32
{
public:
    SWL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SWR : public Instruction32
{
public:
    SWR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SYNC : public Instruction32
{
public:
    SYNC(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SYSCALL : public Instruction32
{
public:
    SYSCALL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TEQ : public Instruction32
{
public:
    TEQ(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TEQI : public Instruction32
{
public:
    TEQI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TGE : public Instruction32
{
public:
    TGE(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TGEI : public Instruction32
{
public:
    TGEI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TGEIU : public Instruction32
{
public:
    TGEIU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TGEU : public Instruction32
{
public:
    TGEU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TLT : public Instruction32
{
public:
    TLT(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TLTI : public Instruction32
{
public:
    TLTI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TLTIU : public Instruction32
{
public:
    TLTIU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TLTU : public Instruction32
{
public:
    TLTU(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TNE : public Instruction32
{
public:
    TNE(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TNEI : public Instruction32
{
public:
    TNEI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class XOR : public Instruction32
{
public:
    XOR(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class XORI : public Instruction32
{
public:
    XORI(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ABS_S : public Instruction32
{
public:
    ABS_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ABS_D : public Instruction32
{
public:
    ABS_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ADD_S : public Instruction32
{
public:
    ADD_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ADD_D : public Instruction32
{
public:
    ADD_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BC1F : public Instruction32
{
public:
    BC1F(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BC1FL : public Instruction32
{
public:
    BC1FL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BC1T : public Instruction32
{
public:
    BC1T(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BC1TL : public Instruction32
{
public:
    BC1TL(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_F_S : public Instruction32
{
public:
    C_F_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_UN_S : public Instruction32
{
public:
    C_UN_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_EQ_S : public Instruction32
{
public:
    C_EQ_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_UEQ_S : public Instruction32
{
public:
    C_UEQ_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_OLT_S : public Instruction32
{
public:
    C_OLT_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_ULT_S : public Instruction32
{
public:
    C_ULT_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_OLE_S : public Instruction32
{
public:
    C_OLE_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_ULE_S : public Instruction32
{
public:
    C_ULE_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_SF_S : public Instruction32
{
public:
    C_SF_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_NGLE_S : public Instruction32
{
public:
    C_NGLE_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_SEQ_S : public Instruction32
{
public:
    C_SEQ_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_NGL_S : public Instruction32
{
public:
    C_NGL_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_LT_S : public Instruction32
{
public:
    C_LT_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_NGE_S : public Instruction32
{
public:
    C_NGE_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_LE_S : public Instruction32
{
public:
    C_LE_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_NGT_S : public Instruction32
{
public:
    C_NGT_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_F_D : public Instruction32
{
public:
    C_F_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_UN_D : public Instruction32
{
public:
    C_UN_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_EQ_D : public Instruction32
{
public:
    C_EQ_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_UEQ_D : public Instruction32
{
public:
    C_UEQ_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_OLT_D : public Instruction32
{
public:
    C_OLT_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_ULT_D : public Instruction32
{
public:
    C_ULT_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_OLE_D : public Instruction32
{
public:
    C_OLE_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_ULE_D : public Instruction32
{
public:
    C_ULE_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_SF_D : public Instruction32
{
public:
    C_SF_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_NGLE_D : public Instruction32
{
public:
    C_NGLE_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_SEQ_D : public Instruction32
{
public:
    C_SEQ_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_NGL_D : public Instruction32
{
public:
    C_NGL_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_LT_D : public Instruction32
{
public:
    C_LT_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_NGE_D : public Instruction32
{
public:
    C_NGE_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_LE_D : public Instruction32
{
public:
    C_LE_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class C_NGT_D : public Instruction32
{
public:
    C_NGT_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CEIL_L_S : public Instruction32
{
public:
    CEIL_L_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CEIL_L_D : public Instruction32
{
public:
    CEIL_L_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CEIL_W_S : public Instruction32
{
public:
    CEIL_W_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CEIL_W_D : public Instruction32
{
public:
    CEIL_W_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CFC1 : public Instruction32
{
public:
    CFC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CTC1 : public Instruction32
{
public:
    CTC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_D_S : public Instruction32
{
public:
    CVT_D_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_D_W : public Instruction32
{
public:
    CVT_D_W(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_D_L : public Instruction32
{
public:
    CVT_D_L(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_L_S : public Instruction32
{
public:
    CVT_L_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_L_D : public Instruction32
{
public:
    CVT_L_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_S_D : public Instruction32
{
public:
    CVT_S_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_S_W : public Instruction32
{
public:
    CVT_S_W(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_S_L : public Instruction32
{
public:
    CVT_S_L(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_W_S : public Instruction32
{
public:
    CVT_W_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class CVT_W_D : public Instruction32
{
public:
    CVT_W_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DIV_S : public Instruction32
{
public:
    DIV_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DIV_D : public Instruction32
{
public:
    DIV_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DMFC1 : public Instruction32
{
public:
    DMFC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DMTC1 : public Instruction32
{
public:
    DMTC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class FLOOR_L_S : public Instruction32
{
public:
    FLOOR_L_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class FLOOR_L_D : public Instruction32
{
public:
    FLOOR_L_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class FLOOR_W_S : public Instruction32
{
public:
    FLOOR_W_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class FLOOR_W_D : public Instruction32
{
public:
    FLOOR_W_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LDC1 : public Instruction32
{
public:
    LDC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class LWC1 : public Instruction32
{
public:
    LWC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MFC1 : public Instruction32
{
public:
    MFC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MOV_S : public Instruction32
{
public:
    MOV_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MOV_D : public Instruction32
{
public:
    MOV_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MTC1 : public Instruction32
{
public:
    MTC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MUL_S : public Instruction32
{
public:
    MUL_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MUL_D : public Instruction32
{
public:
    MUL_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class NEG_S : public Instruction32
{
public:
    NEG_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class NEG_D : public Instruction32
{
public:
    NEG_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ROUND_L_S : public Instruction32
{
public:
    ROUND_L_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ROUND_L_D : public Instruction32
{
public:
    ROUND_L_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ROUND_W_S : public Instruction32
{
public:
    ROUND_W_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ROUND_W_D : public Instruction32
{
public:
    ROUND_W_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SDC1 : public Instruction32
{
public:
    SDC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SQRT_S : public Instruction32
{
public:
    SQRT_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SQRT_D : public Instruction32
{
public:
    SQRT_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SUB_S : public Instruction32
{
public:
    SUB_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SUB_D : public Instruction32
{
public:
    SUB_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SWC1 : public Instruction32
{
public:
    SWC1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TRUNC_L_S : public Instruction32
{
public:
    TRUNC_L_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TRUNC_L_D : public Instruction32
{
public:
    TRUNC_L_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TRUNC_W_S : public Instruction32
{
public:
    TRUNC_W_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class TRUNC_W_D : public Instruction32
{
public:
    TRUNC_W_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MULT_G : public Instruction32
{
public:
    MULT_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MULTU_G : public Instruction32
{
public:
    MULTU_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DMULT_G : public Instruction32
{
public:
    DMULT_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DMULTU_G : public Instruction32
{
public:
    DMULTU_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DIV_G : public Instruction32
{
public:
    DIV_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DIVU_G : public Instruction32
{
public:
    DIVU_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DDIV_G : public Instruction32
{
public:
    DDIV_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DDIVU_G : public Instruction32
{
public:
    DDIVU_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MOD_G : public Instruction32
{
public:
    MOD_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MODU_G : public Instruction32
{
public:
    MODU_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DMOD_G : public Instruction32
{
public:
    DMOD_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DMODU_G : public Instruction32
{
public:
    DMODU_G(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MADD_S : public Instruction32
{
public:
    MADD_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MADD_D : public Instruction32
{
public:
    MADD_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MSUB_S : public Instruction32
{
public:
    MSUB_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class MSUB_D : public Instruction32
{
public:
    MSUB_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class NMADD_S : public Instruction32
{
public:
    NMADD_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class NMADD_D : public Instruction32
{
public:
    NMADD_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class NMSUB_S : public Instruction32
{
public:
    NMSUB_S(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class NMSUB_D : public Instruction32
{
public:
    NMSUB_D(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PACKSSHB : public Instruction32
{
public:
    PACKSSHB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PACKSSWH : public Instruction32
{
public:
    PACKSSWH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PACKUSHB : public Instruction32
{
public:
    PACKUSHB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PADDB : public Instruction32
{
public:
    PADDB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PADDH : public Instruction32
{
public:
    PADDH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PADDW : public Instruction32
{
public:
    PADDW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PADDD : public Instruction32
{
public:
    PADDD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PADDSB : public Instruction32
{
public:
    PADDSB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PADDSH : public Instruction32
{
public:
    PADDSH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PADDUSB : public Instruction32
{
public:
    PADDUSB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PADDUSH : public Instruction32
{
public:
    PADDUSH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PANDN : public Instruction32
{
public:
    PANDN(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PAVGB : public Instruction32
{
public:
    PAVGB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PAVGH : public Instruction32
{
public:
    PAVGH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PCMPEQB : public Instruction32
{
public:
    PCMPEQB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PCMPEQH : public Instruction32
{
public:
    PCMPEQH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PCMPEQW : public Instruction32
{
public:
    PCMPEQW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PCMPGTB : public Instruction32
{
public:
    PCMPGTB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PCMPGTH : public Instruction32
{
public:
    PCMPGTH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PCMPGTW : public Instruction32
{
public:
    PCMPGTW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PEXTRH : public Instruction32
{
public:
    PEXTRH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PINSRH_0 : public Instruction32
{
public:
    PINSRH_0(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PINSRH_1 : public Instruction32
{
public:
    PINSRH_1(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PINSRH_2 : public Instruction32
{
public:
    PINSRH_2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PINSRH_3 : public Instruction32
{
public:
    PINSRH_3(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMADDHW : public Instruction32
{
public:
    PMADDHW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMAXSH : public Instruction32
{
public:
    PMAXSH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMAXUB : public Instruction32
{
public:
    PMAXUB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMINSH : public Instruction32
{
public:
    PMINSH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMINUB : public Instruction32
{
public:
    PMINUB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMOVMSKB : public Instruction32
{
public:
    PMOVMSKB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMULHUH : public Instruction32
{
public:
    PMULHUH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMULHH : public Instruction32
{
public:
    PMULHH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMULLH : public Instruction32
{
public:
    PMULLH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PMULUW : public Instruction32
{
public:
    PMULUW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PASUBUB : public Instruction32
{
public:
    PASUBUB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class BIADD : public Instruction32
{
public:
    BIADD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSHUFH : public Instruction32
{
public:
    PSHUFH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSLLH : public Instruction32
{
public:
    PSLLH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSLLW : public Instruction32
{
public:
    PSLLW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSRAH : public Instruction32
{
public:
    PSRAH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSRAW : public Instruction32
{
public:
    PSRAW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSRLH : public Instruction32
{
public:
    PSRLH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSRLW : public Instruction32
{
public:
    PSRLW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSUBB : public Instruction32
{
public:
    PSUBB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSUBH : public Instruction32
{
public:
    PSUBH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSUBW : public Instruction32
{
public:
    PSUBW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSUBD : public Instruction32
{
public:
    PSUBD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSUBSB : public Instruction32
{
public:
    PSUBSB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSUBSH : public Instruction32
{
public:
    PSUBSH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSUBUSB : public Instruction32
{
public:
    PSUBUSB(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PSUBUSH : public Instruction32
{
public:
    PSUBUSH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PUNPCKHBH : public Instruction32
{
public:
    PUNPCKHBH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PUNPCKHHW : public Instruction32
{
public:
    PUNPCKHHW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PUNPCKHWD : public Instruction32
{
public:
    PUNPCKHWD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PUNPCKLBH : public Instruction32
{
public:
    PUNPCKLBH(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PUNPCKLHW : public Instruction32
{
public:
    PUNPCKLHW(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class PUNPCKLWD : public Instruction32
{
public:
    PUNPCKLWD(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ADD_CP2 : public Instruction32
{
public:
    ADD_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class ADDU_CP2 : public Instruction32
{
public:
    ADDU_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DADD_CP2 : public Instruction32
{
public:
    DADD_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SUB_CP2 : public Instruction32
{
public:
    SUB_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SUBU_CP2 : public Instruction32
{
public:
    SUBU_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSUB_CP2 : public Instruction32
{
public:
    DSUB_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class OR_CP2 : public Instruction32
{
public:
    OR_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLI_CP2 : public Instruction32
{
public:
    SLI_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSLL_CP2 : public Instruction32
{
public:
    DSLL_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class XOR_CP2 : public Instruction32
{
public:
    XOR_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class NOR_CP2 : public Instruction32
{
public:
    NOR_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class AND_CP2 : public Instruction32
{
public:
    AND_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SRL_CP2 : public Instruction32
{
public:
    SRL_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSRL_CP2 : public Instruction32
{
public:
    DSRL_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SRA_CP2 : public Instruction32
{
public:
    SRA_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class DSRA_CP2 : public Instruction32
{
public:
    DSRA_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SEQU_CP2 : public Instruction32
{
public:
    SEQU_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLTU_CP2 : public Instruction32
{
public:
    SLTU_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLEU_CP2 : public Instruction32
{
public:
    SLEU_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SEQ_CP2 : public Instruction32
{
public:
    SEQ_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLT_CP2 : public Instruction32
{
public:
    SLT_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class SLE_CP2 : public Instruction32
{
public:
    SLE_CP2(uint32_t insn);
    bool disas_output(disassemble_info *info);
};

class Decoder
{
public:
    int decode32(disassemble_info *ctx, uint32_t insn);
};

#endif
