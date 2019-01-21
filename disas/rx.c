/*
 * Renesas RX Disassembler
 *
 * Copyright (c) 2019 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "disas/bfd.h"

struct opcode {
    const char *nim;
    int size;
    int szwid;
    int cond;
    int len;
};

enum operand_type {
    none,
    imm135,
    imm8,
    uimm48,
    uimm8_4,
    imm,
    float32,
    incdec,
    ind,
    creg,
    pcdsp,
    memory,
    dsp5,
    regub,
    psw,
    reg,
    reg8,
    range,
};

struct operand {
    enum operand_type type;
    union {
        struct {
            int pos;
            int sz;
        } imm135;
        struct {
            int pos;
        } imm8;
        struct {
            int pos;
            int sz;
        } uimm48;
        struct {
            int pos;
        } uimm8_4;
        struct {
            int pos;
            int li;
        } imm;
        struct {
            int pos;
        } float32;
        struct {
            int reg;
            int incdec;
        } incdec;
        struct {
            int base;
            int offset;
        } ind;
        struct {
            int creg;
        } creg;
        struct {
            int pos;
            int sz;
        } pcdsp;
        struct {
            int reg;
            int id;
            int mi;
        } memory;
        struct {
            int reg;
            int id;
        } regub;
        struct {
            int reg;
            int offset1;
            int offset1w;
            int offset2;
        } dsp5;
        struct {
            int b;
        } psw;
        struct {
            int r;
        } reg;
        struct {
            int r;
        } reg8;
        struct {
            int start;
            int end;
        } range;
    };
};

#define opcode(_code, _mask, _nim, _size, _szwid, _cond, _len)  \
    .code = _code,                                              \
    .mask = _mask,                                              \
    .opcode = {                                                 \
        .nim = _nim, .size = _size, .szwid = _szwid,            \
        .cond = _cond, .len = _len,                             \
    },
#define operand(no, _type, ...)                 \
    .operand[no] = { .type = _type, ._type = {__VA_ARGS__}, },
#define NONE (-1)
#define PCRELB (-2)

/* Instruction Tables */
static struct instruction {
    unsigned int code;
    unsigned int mask;
    struct opcode opcode;
    struct operand operand[3];
} const instructions[] = {
    {
        opcode(0xfd180000, 0xffffef00, "racw", NONE, NONE, NONE, 3)
        operand(0, imm135, 19, 1)
    },
    {
        opcode(0xfd170000, 0xfffff000, "mvtachi", NONE, NONE, NONE, 3)
        operand(0, reg, 20)
    },
    {
        opcode(0xfd171000, 0xfffff000, "mvtaclo", NONE, NONE, NONE, 3)
        operand(0, reg, 20)
    },
    {
        opcode(0xfd722000, 0xfffff000, "fadd", NONE, NONE, NONE, 3)
        operand(0, float32, 24)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd720000, 0xfffff000, "fsub", NONE, NONE, NONE, 3)
        operand(0, float32, 24)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd723000, 0xfffff000, "fmul", NONE, NONE, NONE, 3)
        operand(0, float32, 24)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd724000, 0xfffff000, "fdiv", NONE, NONE, NONE, 3)
        operand(0, float32, 24)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd721000, 0xfffff000, "fcmp", NONE, NONE, NONE, 3)
        operand(0, float32, 24)
        operand(1, reg, 20)
    },
    {
        opcode(0x06200000, 0xff3cff00, "sbb", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200200, 0xff3cff00, "adc", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200400, 0xff3cff00, "max", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200500, 0xff3cff00, "min", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200600, 0xff3cff00, "emul", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200700, 0xff3cff00, "emulu", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200800, 0xff3cff00, "div", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200900, 0xff3cff00, "divu", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200c00, 0xff3cff00, "tst", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06200d00, 0xff3cff00, "xor", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06201000, 0xff3cff00, "xchg", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0x06201100, 0xff3cff00, "itof", NONE, NONE, NONE, 4)
        operand(0, memory, 24, 14, 8)
        operand(1, reg, 28)
    },
    {
        opcode(0xfd702000, 0xfff3f000, "adc", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd704000, 0xfff3f000, "max", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd705000, 0xfff3f000, "min", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd706000, 0xfff3f000, "emul", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd707000, 0xfff3f000, "emulu", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd708000, 0xfff3f000, "div", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd709000, 0xfff3f000, "divu", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd70c000, 0xfff3f000, "tst", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd70d000, 0xfff3f000, "xor", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd70e000, 0xfff3f000, "stz", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd70f000, 0xfff3f000, "stnz", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd6a0000, 0xffff0000, "mvfc", NONE, NONE, NONE, 3)
        operand(0, creg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd670000, 0xffff0000, "revl", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd660000, 0xffff0000, "rotl", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd650000, 0xffff0000, "revl", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd640000, 0xffff0000, "rotr", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd620000, 0xffff0000, "shll", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd610000, 0xffff0000, "shar", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd600000, 0xffff0000, "shlr", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd1f0000, 0xffff0000, "mvfachi", NONE, NONE, NONE, 3)
        operand(0, reg, 20)
    },
    {
        opcode(0xfd1f2000, 0xffff0000, "mvfacmi", NONE, NONE, NONE, 3)
        operand(0, reg, 20)
    },
    {
        opcode(0xfd050000, 0xffff0000, "maclo", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd040000, 0xffff0000, "machi", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd010000, 0xffff0000, "mullo", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd000000, 0xffff0000, "mulhi", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0x7f960000, 0xffff0000, "wait", NONE, NONE, NONE, 2)
    },
    {
        opcode(0x7f950000, 0xffff0000, "rte", NONE, NONE, NONE, 2)
    },
    {
        opcode(0x7f940000, 0xffff0000, "rtfi", NONE, NONE, NONE, 2)
    },
    {
        opcode(0x7f930000, 0xffff0000, "satr", NONE, NONE, NONE, 2)
    },
    {
        opcode(0x7f8f0000, 0xffff0000, "smovf", NONE, NONE, NONE, 2)
    },
    {
        opcode(0x7f8b0000, 0xffff0000, "smovb", NONE, NONE, NONE, 2)
    },
    {
        opcode(0x7f870000, 0xffff0000, "smovu", NONE, NONE, NONE, 2)
    },
    {
        opcode(0x7f830000, 0xffff0000, "scmpu", NONE, NONE, NONE, 2)
    },
    {
        opcode(0x75700000, 0xffff0000, "mvtipl", NONE, NONE, NONE, 3)
        operand(0, uimm48, 20, 4)
    },
    {
        opcode(0x75600000, 0xffff0000, "int", NONE, NONE, NONE, 3)
        operand(0, uimm48, 16, 8)
    },
    {
        opcode(0xfc0f0000, 0xffff0000, "abs", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc070000, 0xffff0000, "neg", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc000000, 0xffff0000, "sbb", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd6e0000, 0xfffe0000, "rotl", NONE, NONE, NONE, 3)
        operand(0, imm135, 15, 5)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd6c0000, 0xfffe0000, "rotl", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc980000, 0xfffc0000, "round", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc940000, 0xfffc0000, "ftoi", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc900000, 0xfffc0000, "fdiv", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc8c0000, 0xfffc0000, "fmul", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, NONE)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc880000, 0xfffc0000, "fadd", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, NONE)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc840000, 0xfffc0000, "fcmp", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, NONE)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc800000, 0xfffc0000, "fsub", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, NONE)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc6c0000, 0xfffc0000, "bnot", NONE, NONE, NONE, 3)
        operand(0, reg, 20)
        operand(1, memory, 16, 14, NONE)
    },
    {
        opcode(0xfc640000, 0xfffc0000, "btst", NONE, NONE, NONE, 3)
        operand(0, reg, 20)
        operand(1, memory, 16, 14, NONE)
    },
    {
        opcode(0xfc680000, 0xfffc0000, "bclr", NONE, NONE, NONE, 3)
        operand(0, reg, 20)
        operand(1, memory, 16, 14, NONE)
    },
    {
        opcode(0xfc600000, 0xfffc0000, "bset", NONE, NONE, NONE, 3)
        operand(0, reg, 20)
        operand(1, memory, 16, 14, NONE)
    },
    {
        opcode(0xfc440000, 0xfffc0000, "itof", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc340000, 0xfffc0000, "xor", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc300000, 0xfffc0000, "tst", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc380000, 0xfffc0000, "not", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0x7f8c0000, 0xfffc0000, "rmpa", 14, 2, NONE, 2)
    },
    {
        opcode(0x7f880000, 0xfffc0000, "sstr", 14, 2, NONE, 2)
    },
    {
        opcode(0x7f840000, 0xfffc0000, "swhile", 14, 2, NONE, 2)
    },
    {
        opcode(0x7f800000, 0xfffc0000, "suntil", 14, 2, NONE, 2)
    },
    {
        opcode(0xfd680000, 0xfff80000, "mvtc", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, creg, 20)
    },
    {
        opcode(0xfd280000, 0xfff80000, "mov", 14, 2, NONE, 3)
        operand(0, incdec, 16, 13)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd200000, 0xfff80000, "mov", 14, 2, NONE, 3)
        operand(0, reg, 20)
        operand(1, incdec, 16, 13)
    },
    {
        opcode(0xfc200000, 0xfff80000, "div", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc180000, 0xfff80000, "emul", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc100000, 0xfff80000, "max", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc000000, 0xfff80000, "sbb", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfc080000, 0xfff80000, "adb", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0xfd730000, 0xfff30000, "mvtc", NONE, NONE, NONE, 3)
        operand(0, imm, 24, 12)
        operand(1, creg, 20)
    },
    {
        opcode(0xfd300000, 0xfff20000, "movu", 15, 1, NONE, 3)
        operand(0, incdec, 16, 13)
        operand(1, reg, 20)
    },
    {
        opcode(0xff500000, 0xfff00000, "or", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
        operand(2, reg, 12)
    },
    {
        opcode(0xff400000, 0xfff00000, "or", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
        operand(2, reg, 12)
    },
    {
        opcode(0xff300000, 0xfff00000, "mul", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
        operand(2, reg, 12)
    },
    {
        opcode(0xff200000, 0xfff00000, "add", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
        operand(2, reg, 12)
    },
    {
        opcode(0xff000000, 0xfff00000, "sub", NONE, NONE, NONE, 3)
        operand(0, reg, 16)
        operand(1, reg, 20)
        operand(2, reg, 12)
    },
    {
        opcode(0xfcd00000, 0xfff00000, "sc", 12, 2, 20, 3)
        operand(0, memory, 16, 14, NONE)
    },
    {
        opcode(0x7fb00000, 0xfff00000, "clrpsw", NONE, NONE, NONE, 2)
        operand(0, psw, 12)
    },
    {
        opcode(0x7fa00000, 0xfff00000, "setpsw", NONE, NONE, NONE, 2)
        operand(0, psw, 12)
    },
    {
        opcode(0x7f500000, 0xfff00000, "bsr.l", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7f400000, 0xfff00000, "bra.l", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7f100000, 0xfff00000, "jsr", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7f000000, 0xfff00000, "jmp", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7ee00000, 0xfff00000, "popc", NONE, NONE, NONE, 2)
        operand(0, creg, 12)
    },
    {
        opcode(0x7ec00000, 0xfff00000, "pushc", NONE, NONE, NONE, 2)
        operand(0, creg, 12)
    },
    {
        opcode(0x7eb00000, 0xfff00000, "pop", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7e300000, 0xfff00000, "sat", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x75500000, 0xfff00000, "cmp", NONE, NONE, NONE, 3)
        operand(0, uimm48, 16, 8)
        operand(1, reg, 12)
    },
    {
        opcode(0x75400000, 0xfff00000, "mov.l", NONE, NONE, NONE, 3)
        operand(0, uimm48, 16, 8)
        operand(1, reg, 12)
    },
    {
        opcode(0x7e500000, 0xfff00000, "rolc", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7e400000, 0xfff00000, "rorc", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7e000000, 0xfff00000, "not", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7e100000, 0xfff00000, "neg", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x7e200000, 0xfff00000, "abs", NONE, NONE, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0x06140000, 0xff3c0000, "or", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, 8)
        operand(1, reg, 20)
    },
    {
        opcode(0x06100000, 0xff3c0000, "and", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, 8)
        operand(1, reg, 20)
    },
    {
        opcode(0x060c0000, 0xff3c0000, "mul", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, 8)
        operand(1, reg, 20)
    },
    {
        opcode(0x06080000, 0xff3c0000, "add", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, 8)
        operand(1, reg, 20)
    },
    {
        opcode(0x06040000, 0xff3c0000, "cmp", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, 8)
        operand(1, reg, 20)
    },
    {
        opcode(0x06000000, 0xff3c0000, "sub", NONE, NONE, NONE, 3)
        operand(0, memory, 16, 14, 8)
        operand(1, reg, 20)
    },
    {
        opcode(0xfde0f000, 0xffe0f000, "bnot", NONE, NONE, NONE, 3)
        operand(0, imm135, 11, 5)
        operand(1, reg, 20)
    },
    {
        opcode(0xfce00f00, 0xffe00f00, "bnot", NONE, NONE, NONE, 3)
        operand(0, imm135, 11, 3)
        operand(1, memory, 16, 14, NONE)
    },
    {
        opcode(0xfec00000, 0xffe00000, "movu.l", NONE, NONE, NONE, 3)
        operand(0, uimm48, 16, 8)
        operand(1, reg, 12)
    },
    {
        opcode(0xfde00000, 0xffe00000, "bm", NONE, NONE, 16, 3)
        operand(0, imm135, 11, 5)
        operand(1, reg, 20)
    },
    {
        opcode(0xfdc00000, 0xffe00000, "shll", NONE, NONE, NONE, 3)
        operand(0, imm135, 11, 5)
        operand(1, reg, 16)
        operand(2, reg, 20)
    },
    {
        opcode(0xfda00000, 0xffe00000, "shar", NONE, NONE, NONE, 3)
        operand(0, imm135, 11, 5)
        operand(1, reg, 16)
        operand(2, reg, 20)
    },
    {
        opcode(0xfd800000, 0xffe00000, "shlr", NONE, NONE, NONE, 3)
        operand(0, imm135, 11, 5)
        operand(1, reg, 16)
        operand(2, reg, 20)
    },
    {
        opcode(0xfce00000, 0xffe00000, "bm", NONE, NONE, 20, 3)
        operand(0, imm135, 11, 3)
        operand(1, memory, 16, 14, NONE)
    },
    {
        opcode(0x7e800000, 0xffc00000, "push", 10, 2, NONE, 2)
        operand(0, reg, 12)
    },
    {
        opcode(0xfe400000, 0xffc00000, "mov", 10, 2, NONE, 3)
        operand(0, ind, 16, 12)
        operand(1, reg, 20)
    },
    {
        opcode(0xfe000000, 0xffc00000, "mov", 10, 2, NONE, 3)
        operand(0, reg, 20)
        operand(1, ind, 16, 12)
    },
    {
        opcode(0xfc400000, 0xffc00000, "xchg", NONE, NONE, NONE, 3)
        operand(0, regub, 16, 14)
        operand(1, reg, 20)
    },
    {
        opcode(0x74300000, 0xfcf00000, "or", NONE, NONE, NONE, 2)
        operand(0, imm, 16, 6)
        operand(1, reg, 20)
    },
    {
        opcode(0x74200000, 0xfcf00000, "and", NONE, NONE, NONE, 2)
        operand(0, imm, 16, 6)
        operand(1, reg, 20)
    },
    {
        opcode(0x74100000, 0xfcf00000, "mul", NONE, NONE, NONE, 2)
        operand(0, imm, 16, 6)
        operand(1, reg, 20)
    },
    {
        opcode(0x74000000, 0xfcf00000, "cmp", NONE, NONE, NONE, 2)
        operand(0, imm, 16, 6)
        operand(1, reg, 20)
    },
    {
        opcode(0xfb020000, 0xff030000, "mov.l", NONE, NONE, NONE, 2)
        operand(0, imm, 16, 12)
        operand(1, reg, 8)
    },
    {
        opcode(0xf4080000, 0xfc0c0000, "push", NONE, NONE, NONE, 2)
        operand(0, memory, 8, 6, NONE)
    },
    {
        opcode(0x6f000000, 0xff000000, "popm", NONE, NONE, NONE, 2)
        operand(0, range, 8, 12)
    },
    {
        opcode(0x6e000000, 0xff000000, "pushm", NONE, NONE, NONE, 2)
        operand(0, range, 8, 12)
    },
    {
        opcode(0x67000000, 0xff000000, "rtsd", NONE, NONE, NONE, 2)
        operand(0, uimm8_4, 8)
    },
    {
        opcode(0x66000000, 0xff000000, "mov.l", NONE, NONE, NONE, 2)
        operand(0, uimm48, 8, 4)
        operand(1, reg, 12)
    },
    {
        opcode(0x65000000, 0xff000000, "or", NONE, NONE, NONE, 2)
        operand(0, uimm48, 8, 4)
        operand(1, reg, 12)
    },
    {
        opcode(0x64000000, 0xff000000, "and", NONE, NONE, NONE, 2)
        operand(0, uimm48, 8, 4)
        operand(1, reg, 12)
    },
    {
        opcode(0x63000000, 0xff000000, "mul", NONE, NONE, NONE, 2)
        operand(0, uimm48, 8, 4)
        operand(1, reg, 12)
    },
    {
        opcode(0x62000000, 0xff000000, "add", NONE, NONE, NONE, 2)
        operand(0, uimm48, 8, 4)
        operand(1, reg, 12)
    },
    {
        opcode(0x61000000, 0xff000000, "cmp", NONE, NONE, NONE, 2)
        operand(0, uimm48, 8, 4)
        operand(1, reg, 12)
    },
    {
        opcode(0x60000000, 0xff000000, "sub", NONE, NONE, NONE, 2)
        operand(0, uimm48, 8, 4)
        operand(1, reg, 12)
    },
    {
        opcode(0x3f000000, 0xff000000, "rtsd", NONE, NONE, NONE, 3)
        operand(0, uimm8_4, 16)
        operand(1, range, 8, 12)
    },
    {
        opcode(0x39000000, 0xff000000, "bsr.w", NONE, NONE, NONE, 3)
        operand(0, pcdsp, 8, 16)
    },
    {
        opcode(0x38000000, 0xff000000, "bra.w", NONE, NONE, NONE, 3)
        operand(0, pcdsp, 8, 16)
    },
    {
        opcode(0x2e000000, 0xff000000, "bra.b", NONE, NONE, NONE, 2)
        operand(0, pcdsp, 8, 8)
    },
    {
        opcode(0x05000000, 0xff000000, "bsr.a", NONE, NONE, NONE, 4)
        operand(0, pcdsp, 8, 24)
    },
    {
        opcode(0x04000000, 0xff000000, "bra.a", NONE, NONE, NONE, 4)
        operand(0, pcdsp, 8, 24)
    },
    {
        opcode(0x03000000, 0xff000000, "nop", NONE, NONE, NONE, 1)
    },
    {
        opcode(0x02000000, 0xff000000, "rts", NONE, NONE, NONE, 1)
    },
    {
        opcode(0x00000000, 0xff000000, "brk", NONE, NONE, NONE, 1)
    },
    {
        opcode(0x3a000000, 0xff000000, "beq.w", NONE, NONE, NONE, 3)
        operand(0, pcdsp, 8, 16)
    },
    {
        opcode(0x3b000000, 0xff000000, "bne.w", NONE, NONE, NONE, 3)
        operand(0, pcdsp, 8, 16)
    },
    {
        opcode(0x7c000000, 0xfe000000, "btst", NONE, NONE, NONE, 2)
        operand(0, imm135, 7, 5)
        operand(1, reg, 12)
    },
    {
        opcode(0x7a000000, 0xfe000000, "bclr", NONE, NONE, NONE, 2)
        operand(0, imm135, 7, 5)
        operand(1, reg, 12)
    },
    {
        opcode(0x78000000, 0xfe000000, "bset", NONE, NONE, NONE, 2)
        operand(0, imm135, 7, 5)
        operand(1, reg, 12)
    },
    {
        opcode(0x6c000000, 0xfe000000, "shll", NONE, NONE, NONE, 2)
        operand(0, imm135, 7, 5)
        operand(1, reg, 12)
    },
    {
        opcode(0x6a000000, 0xfe000000, "shar", NONE, NONE, NONE, 2)
        operand(0, imm135, 7, 5)
        operand(1, reg, 12)
    },
    {
        opcode(0x68000000, 0xfe000000, "shlr", NONE, NONE, NONE, 2)
        operand(0, imm135, 7, 5)
        operand(1, reg, 12)
    },
    {
        opcode(0xf4000000, 0xfc000000, "btst", NONE, NONE, NONE, 2)
        operand(0, imm135, 13, 3)
        operand(1, memory, 8, 6, NONE)
    },
    {
        opcode(0xf0080000, 0xfc080000, "bclr", NONE, NONE, NONE, 2)
        operand(0, imm135, 13, 3)
        operand(1, memory, 8, 6, NONE)
    },
    {
        opcode(0xf0000000, 0xfc080000, "bset", NONE, NONE, NONE, 2)
        operand(0, imm135, 13, 3)
        operand(1, memory, 8, 6, NONE)
    },
    {
        opcode(0xf8000000, 0xfc000000, "mov", 14, 2, NONE, 2)
        operand(0, imm, NONE, 12)
        operand(1, memory, 8, 6, NONE)
    },
    {
        opcode(0x70000000, 0xfc000000, "add", NONE, NONE, NONE, 2)
        operand(0, imm, NONE, 6)
        operand(1, reg, 8)
        operand(2, reg, 12)
    },
    {
        opcode(0x54000000, 0xfc000000, "or", NONE, NONE, NONE, 2)
        operand(0, regub, 8, 6)
        operand(1, reg, 12)
    },
    {
        opcode(0x50000000, 0xfc000000, "and", NONE, NONE, NONE, 2)
        operand(0, regub, 8, 6)
        operand(1, reg, 12)
    },
    {
        opcode(0x4c000000, 0xfc000000, "mul", NONE, NONE, NONE, 2)
        operand(0, regub, 8, 6)
        operand(1, reg, 12)
    },
    {
        opcode(0x48000000, 0xfc000000, "add", NONE, NONE, NONE, 2)
        operand(0, regub, 8, 6)
        operand(1, reg, 12)
    },
    {
        opcode(0x44000000, 0xfc000000, "cmp", NONE, NONE, NONE, 2)
        operand(0, regub, 8, 6)
        operand(1, reg, 12)
    },
    {
        opcode(0x40000000, 0xfc000000, "sub", NONE, NONE, NONE, 2)
        operand(0, regub, 8, 6)
        operand(1, reg, 12)
    },
    {
        opcode(0x3c000000, 0xfc000000, "mov", 6, 2, NONE, 3)
        operand(0, uimm48, 16, 8)
        operand(1, dsp5, 9, 8, 1, 12)
    },
    {
        opcode(0xcf000000, 0xcf000000, "mov", 2, 2, NONE, 2)
        operand(0, reg, 8)
        operand(1, reg, 12)
    },
    {
        opcode(0x58000000, 0xf8000000, "movu", 5, 1, NONE, 2)
        operand(0, memory, 8, 6, NONE)
        operand(1, reg, 12)
    },
    {
        opcode(0x08000000, 0xf8000000, "bra.s", NONE, NONE, NONE, 1)
        operand(0, pcdsp, 5, 3)
    },
    {
        opcode(0x10000000, 0xf8000000, "beq.s", NONE, NONE, NONE, 1)
        operand(0, pcdsp, 5, 3)
    },
    {
        opcode(0x18000000, 0xf8000000, "bne.s", NONE, NONE, NONE, 1)
        operand(0, pcdsp, 5, 3)
    },
    {
        opcode(0xb0000000, 0xf0000000, "movu", 4, 1, NONE, 2)
        operand(0, dsp5, 9, 8, 4, 12)
        operand(1, reg8, 13)
    },
    {
        opcode(0x20000000, 0xf0000000, "b", PCRELB, NONE, 4, 2)
        operand(0, pcdsp, 8, 8)
    },
    {
        opcode(0xcc000000, 0xcc000000, "mov", 2, 2, NONE, 2)
        operand(0, memory, 8, 6, NONE)
        operand(1, reg, 12)
    },
    {
        opcode(0x88000000, 0xc8000000, "mov", 2, 2, NONE, 2)
        operand(0, dsp5, 9, 8, 4, 12)
        operand(1, reg8, 13)
    },
    {
        opcode(0x80000000, 0xc8000000, "mov", 2, 2, NONE, 2)
        operand(0, reg8, 13)
        operand(1, dsp5, 9, 8, 4, 12)
    },
    {
        opcode(0xc3000000, 0xc3000000, "mov", 2, 2, NONE, 2)
        operand(0, reg, 12)
        operand(1, memory, 8, 4, NONE)
    },
    {
        opcode(0xc0000000, 0xc0000000, "mov", 2, 2, NONE, 2)
        operand(0, memory, 8, 6, NONE)
        operand(1, memory, 12, 4, NONE)
    },
};

static const char *cond[] = {
    "eq", "ne", "c", "nc", "gtu", "leu", "pz", "n",
    "ge", "lt", "gt", "le", "o", "no", "<inv>", "<inv>",
};

static const char size[] = { 'b', 'w', 'l', '?' };

static const char *creg_name[] = {
    "psw", "pc", "usp", "fpsw", "<inv>", "<inv>", "<inv>", "<inv>",
    "bpsw", "bpc", "isp", "fintv", "intb", "<inv>", "<inv>", "<inv>",
};

static const char *psw_bit[] = {
    "c", "s", "z", "o", "<inv>", "<inv>", "<inv>", "<inv>",
    "i", "u", "<inv>", "<inv>", "<inv>", "<inv>", "<inv>", "<inv>",
};

static const char *memex[] = {
    "b", "w", "l", "uw",
};

#define prt(...) dis->fprintf_func(dis->stream, __VA_ARGS__)
#define opcval(pos, wid) (op >> (32 - insn->opcode.pos - (wid)) \
                                & ((1 << (wid)) - 1))
#define oprval(pos, wid) (op >> (32 - insn->operand[i].pos - (wid))     \
                                & ((1 << (wid)) - 1))
#define oplen() (insn->opcode.len)
#define opr(type, field) (insn->operand[i].type.field)

#define memdisp(offset, reg)                                            \
    do {                                                                \
        bfd_byte dbuf[2];                                               \
        uint16_t dsp;                                                   \
        dis->read_memory_func(addr + oplen() + offset,                 \
                              dbuf, id, dis);                           \
        dsp = (id == 1) ? dbuf[0] : (dbuf[0] | dbuf[1] << 8);               \
        prt("%d[r%d]", dsp << scale, reg);                              \
        append += id;                                                   \
    } while (0)

int print_insn_rx(bfd_vma addr, disassemble_info *dis)
{
    bfd_byte buf;
    uint32_t op;
    unsigned int i;
    int append = 0;
    int32_t val;
    int scale = 0;
    struct instruction const *insn = NULL;

    op = 0;
    for (i = 0; i < 4; i++) {
        op <<= 8;
        if (!dis->read_memory_func(addr + i, &buf, 1, dis)) {
            op |= buf;
        }
    }

    for (i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
        if ((op & instructions[i].mask) == instructions[i].code) {
            insn = &instructions[i];
            break;
        }
    }

    if (insn == NULL) {
        prt(".byte\t0x%02x", op >> 24);
        return 1;
    }

    prt("%s", insn->opcode.nim);
    if (insn->opcode.cond > 0) {
        prt("%s", cond[opcval(cond, 4)]);
        if (insn->opcode.size == PCRELB) {
            prt(".b");
        }
    }
    if (insn->opcode.size > 0) {
        scale = opcval(size, insn->opcode.szwid);
        prt(".%c", size[scale]);
    }

    for (i = 0; i < 3; i++) {
        if (insn->operand[i].type) {
            prt((i > 0) ? ", " : "\t");
        }
        switch (insn->operand[i].type) {
        case none:
            break;
        case imm135:
            prt("#%d", oprval(imm135.pos, opr(imm135, sz)));
            break;
        case imm8:
            val = (op >> (32 - opr(imm8, pos) - 8)) << 24;
            val >>= 24;
            prt("#%d", val);
            break;
        case uimm48:
            prt("#%d", oprval(uimm48.pos, opr(uimm48, sz)));
            break;
        case uimm8_4:
            prt("#%d", oprval(uimm8_4.pos, 8) << 2);
            break;
        case imm: {
            int li = oprval(imm.li, 2);
            bfd_byte ibuf[4];
            int offset = append;

            /* "mov #imm, dsp[rd]" is destination first */
            if ((op & 0xfc000000) == 0xf8000000) {
                offset = oprval(memory.id, 2);
            }
            if (li == 0) {
                li = 4;
            }
            dis->read_memory_func(addr + oplen() + offset,
                                  ibuf, li, dis);
            switch (li) {
            case 1:
                val = (signed char)ibuf[0];
                break;
            case 2:
                val = (signed short)(ibuf[0] | ibuf[1] << 8);
                break;
            case 3:
                val = ibuf[0] | ibuf[1] << 8 | ibuf[2] << 16;
                /* sign extended */
                val <<= 8;
                val >>= 8;
                break;
            case 4:
                val = ibuf[0] | ibuf[1] << 8 | ibuf[2] << 16 | ibuf[3] << 24;
                break;
            }
            append += li;
            if (abs(val) < 256) {
                prt("#%d", val);
            } else {
                prt("#0x%08x", val);
            }
            break;
        }
        case float32: {
            float f;
            dis->read_memory_func(addr + oplen() + append,
                                  (bfd_byte *)&f, 4, dis);
            append += 4;
            prt("#%f", f);
            break;
        }
        case incdec:
            if (oprval(incdec.incdec, 1) & 1) {
                prt("[-r%d]", oprval(incdec.reg, 4));
            } else {
                prt("[r%d+]", oprval(incdec.reg, 4));
            }
            break;
        case ind:
            prt("[r%d,r%d]", oprval(ind.offset, 4), oprval(ind.base, 4));
            break;
        case creg:
            prt("%s", creg_name[oprval(creg.creg, 4)]);
            break;
        case pcdsp:
            val = oprval(pcdsp.pos, opr(pcdsp, sz));
            switch (opr(pcdsp, sz)) {
            case 3:
                if (val < 3) {
                    val += 8;
                }
                break;
            case 8:
                if (val >= 0x80) {
                    val = -(~val & 0xff) - 1;
                }
                break;
            case 16:
                val = ((val >> 8) & 0xff) | (val << 8);
                val &= 0xffff;
                if (val >= 0x8000) {
                    val = -(~val & 0xffff) - 1;
                }
                break;
            case 24:
                val = ((val >> 16) & 0xff) | (val << 16) | (val & 0x00ff00);
                val &= 0xffffff;
                if (val >= 0x800000) {
                    val = -(~val & 0xffffff) - 1;
                }
                break;
            }
            dis->print_address_func(addr + val, dis);
            break;
        case memory: {
            int id = oprval(memory.id, 2);
            int mi = NONE;
            int offset = append;

            /* "mov #imm, dsp[rd]" is destination first */
            if ((op & 0xfc000000) == 0xf8000000) {
                offset = 0;
            }
            if (opr(memory, mi) >= 0) {
                mi =  oprval(memory.mi, 2);
                scale = mi;
            }

            switch (id) {
            case 0:
                prt("[r%d]", oprval(memory.reg, 4));
                break;
            case 1 ... 2:
                memdisp(offset, oprval(memory.reg, 4));
                break;
            case 3:
                prt("r%d", oprval(memory.reg, 4));
                break;
            }
            if (id < 3 && mi >= 0) {
                prt(".%s", memex[mi]);
            }
            break;
        }
        case dsp5:
            val = oprval(dsp5.offset1, opr(dsp5, offset1w));
            val |= oprval(dsp5.offset2, (5 - opr(dsp5, offset1w)));
            prt("%d[r%d]", val, oprval(memory.reg, 4));
            break;
        case regub: {
            int id = oprval(regub.id, 2);
            switch (id) {
            case 0:
                prt("[r%d].ub", oprval(regub.reg, 4));
                break;
            case 1 ... 2:
                memdisp(append, oprval(regub.reg, 4));
                prt(".ub");
                append += id;
                break;
            case 3:
                prt("r%d", oprval(regub.reg, 4));
                break;
            }
            break;
        }
        case psw:
            prt("%s", psw_bit[oprval(psw.b, 4)]);
            break;
        case reg:
            prt("r%d", oprval(reg.r, 4));
            break;
        case reg8:
            prt("r%d", oprval(reg8.r, 3));
            break;
        case range:
            prt("r%d-r%d", oprval(range.start, 4), oprval(range.end, 4));
            break;
        }
    }
    return oplen() + append;
}
