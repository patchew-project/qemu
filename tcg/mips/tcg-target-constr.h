/* SPDX-License-Identifier: MIT */
/*
 * MIPS target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

#define ALL_GENERAL_REGS  0xffffffffu
#define NOA0_REGS         (ALL_GENERAL_REGS & ~(1 << TCG_REG_A0))

#ifdef CONFIG_SOFTMMU
#define ALL_QLOAD_REGS \
    (NOA0_REGS & ~((TCG_TARGET_REG_BITS < TARGET_LONG_BITS) << TCG_REG_A2))
#define ALL_QSTORE_REGS \
    (NOA0_REGS & ~(TCG_TARGET_REG_BITS < TARGET_LONG_BITS   \
                   ? (1 << TCG_REG_A2) | (1 << TCG_REG_A3)  \
                   : (1 << TCG_REG_A1)))
#else
#define ALL_QLOAD_REGS   NOA0_REGS
#define ALL_QSTORE_REGS  NOA0_REGS
#endif

REGS('r', ALL_GENERAL_REGS)
REGS('L', ALL_QLOAD_REGS)
REGS('S', ALL_QSTORE_REGS)

CONST('I', TCG_CT_CONST_U16)
CONST('J', TCG_CT_CONST_S16)
CONST('K', TCG_CT_CONST_P2M1)
CONST('N', TCG_CT_CONST_N16)
CONST('W', TCG_CT_CONST_WSZ)
CONST('Z', TCG_CT_CONST_ZERO)
