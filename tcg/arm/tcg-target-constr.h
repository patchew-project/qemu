/* SPDX-License-Identifier: MIT */
/*
 * Arm target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

#define ALL_GENERAL_REGS  0xffffu

#ifdef CONFIG_SOFTMMU
#define ALL_QLOAD_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_R0) | (1 << TCG_REG_R1) | \
                          (1 << TCG_REG_R2) | (1 << TCG_REG_R3) | \
                          (1 << TCG_REG_R14)))
#define ALL_QSTORE_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_R0) | (1 << TCG_REG_R1) | \
                          (1 << TCG_REG_R2) | (1 << TCG_REG_R14) | \
                          ((TARGET_LONG_BITS == 64) << TCG_REG_R3)))
#else
#define ALL_QLOAD_REGS   ALL_GENERAL_REGS
#define ALL_QSTORE_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_R0) | (1 << TCG_REG_R1)))
#endif

REGS('r', ALL_GENERAL_REGS)
REGS('l', ALL_QLOAD_REGS)
REGS('s', ALL_QSTORE_REGS)
REGS('w', 0xffff0000u)

CONST('I', TCG_CT_CONST_ARM)
CONST('K', TCG_CT_CONST_INV)
CONST('N', TCG_CT_CONST_NEG)
CONST('Z', TCG_CT_CONST_ZERO)
