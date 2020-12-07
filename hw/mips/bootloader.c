/*
 * Utility for QEMU MIPS to generate it's simple bootloader
 *
 * Instructions used here are carefully selected to keep compatibility with
 * MIPS Release 6.
 *
 * Copyright (C) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "hw/mips/cpudevs.h"

/* Base types */
static void bl_gen_nop(uint32_t **p)
{
    stl_p(*p, 0);
    *p = *p + 1;
}

static void bl_gen_r_type(uint32_t **p, uint8_t opcode, uint8_t rs, uint8_t rt,
                            uint8_t rd, uint8_t shift, uint8_t funct)
{
    uint32_t insn = 0;

    insn = deposit32(insn, 26, 6, opcode);
    insn = deposit32(insn, 21, 5, rs);
    insn = deposit32(insn, 16, 5, rt);
    insn = deposit32(insn, 11, 5, rd);
    insn = deposit32(insn, 6, 5, shift);
    insn = deposit32(insn, 0, 6, funct);

    stl_p(*p, insn);
    *p = *p + 1;
}

static void bl_gen_i_type(uint32_t **p, uint8_t opcode, uint8_t rs, uint8_t rt,
                            uint16_t imm)
{
    uint32_t insn = 0;

    insn = deposit32(insn, 26, 6, opcode);
    insn = deposit32(insn, 21, 5, rs);
    insn = deposit32(insn, 16, 5, rt);
    insn = deposit32(insn, 0, 16, imm);

    stl_p(*p, insn);
    *p = *p + 1;
}

/* Single instructions */
static void bl_gen_dsll(uint32_t **p, uint8_t rd, uint8_t rt, uint8_t sa)
{
    /* R6: OK, 32: NO */
    bl_gen_r_type(p, 0, 0, rt, rd, sa, 0x38);
}

static void bl_gen_daddiu(uint32_t **p, uint8_t rt, uint8_t rs, uint16_t imm)
{
    /* R6: OK, 32: NO */
    bl_gen_i_type(p, 0x19, rs, rt, imm);
}

static void bl_gen_jalr(uint32_t **p, uint8_t rs)
{
    /* R6: OK, 32: OK */
    bl_gen_r_type(p, 0, rs, 0, 31, 0, 0x9);
}

static void bl_gen_lui(uint32_t **p, uint8_t rt, uint16_t imm)
{
    /* R6: It's a alias of AUI with RS = 0, 32: OK */
    bl_gen_i_type(p, 0xf, 0, rt, imm);
}

static void bl_gen_ori(uint32_t **p, uint8_t rt, uint8_t rs, uint16_t imm)
{
    /* R6: OK, 32: OK */
    bl_gen_i_type(p, 0xd, rs, rt, imm);
}

static void bl_gen_sw(uint32_t **p, uint8_t rt, uint8_t base, uint16_t offset)
{
    /* R6: OK, 32: NO */
    bl_gen_i_type(p, 0x2b, base, rt, offset);
}

static void bl_gen_sd(uint32_t **p, uint8_t rt, uint8_t base, uint16_t offset)
{
    /* R6: OK, 32: NO */
    bl_gen_i_type(p, 0x3f, base, rt, offset);
}

/* Pseudo instructions */
static void bl_gen_li(uint32_t **p, uint8_t rt, uint32_t imm)
{
    /* R6: OK, 32 OK */
    bl_gen_lui(p, rt, extract32(imm, 16, 16));
    bl_gen_ori(p, rt, rt, extract32(imm, 0, 16));
}

static void bl_gen_dli(uint32_t **p, uint8_t rt, uint64_t imm)
{
    /* R6: OK, 32 NO */
    bl_gen_li(p, rt, extract64(imm, 32, 32));
    bl_gen_dsll(p, rt, rt, 16);
    bl_gen_daddiu(p, rt, rt, extract64(imm, 16, 16));
    bl_gen_dsll(p, rt, rt, 16);
    bl_gen_daddiu(p, rt, rt, extract64(imm, 0, 16));
}

/* Helpers */
void bl_gen_jump_to(uint32_t **p, uint32_t jump_addr)
{
    /* Use ra to jump */
    bl_gen_li(p, 31, jump_addr);
    bl_gen_jalr(p, 31);
    bl_gen_nop(p); /* delay slot, useless for R6 */
}

void bl_gen_jump_kernel(uint32_t **p, uint32_t sp, uint32_t a0,
                        uint32_t a1, uint32_t a2, uint32_t a3,
                        uint32_t kernel_addr)
{
    bl_gen_li(p, 29, sp);
    bl_gen_li(p, 4, a0);
    bl_gen_li(p, 5, a1);
    bl_gen_li(p, 6, a2);
    bl_gen_li(p, 7, a3);

    bl_gen_jump_to(p, kernel_addr);
}

void bl_gen_writel(uint32_t **p, uint32_t val, uint32_t addr)
{
    bl_gen_li(p, 26, val);
    bl_gen_li(p, 27, addr);
    bl_gen_sw(p, 26, 27, 0x0);
}

void bl_gen_writeq(uint32_t **p, uint64_t val, uint32_t addr)
{
    /* 64 Only */
    bl_gen_dli(p, 26, val);
    bl_gen_li(p, 27, addr);
    bl_gen_sd(p, 26, 27, 0x0);
}
