/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Process Mission
 */

#define SPR_SRR0         0x01a
#define SPR_BOOKE_ESR    0x03e
#define SPR_BOOKE_IVPR   0x03f
#define SPR_BOOKE_IVOR6  0x196
#define SPR_BOOKE_MAS0   0x270
#define SPR_BOOKE_MAS1   0x271
#define SPR_BOOKE_MAS2   0x272
#define SPR_BOOKE_MAS3   0x273
#define SPR_BOOKE_MAS7   0x3b0

    .macro load32 reg, value
    lis \reg, ((\value) >> 16) & 0xffff
    ori \reg, \reg, (\value) & 0xffff
    .endm

    .macro load_addr reg, symbol
    lis \reg, \symbol@ha
    addi \reg, \reg, \symbol@l
    .endm

    .macro load_value reg, value
#ifdef _LP64
    load32 \reg, (\value) >> 32
    sldi \reg, \reg, 32
    oris \reg, \reg, ((\value) >> 16) & 0xffff
    ori \reg, \reg, (\value) & 0xffff
#else
    load32 \reg, \value
#endif
    .endm

    .macro check_value reg, value
    load_value r12, \value
#ifdef _LP64
    cmpd \reg, r12
#else
    cmpw \reg, r12
#endif
    bne fail
    .endm

    .macro check_illegal insn:vararg
    load_addr r14, 1f
    load_addr r15, 2f
1:
    \insn
    b fail                      /* Executing the instruction is a failure */
2:
    li r14, 0
    .endm
