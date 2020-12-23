/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AArch64 target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

#define ALL_GENERAL_REGS  0xffffffffu
#define ALL_VECTOR_REGS   0xffffffff00000000ull

#ifdef CONFIG_SOFTMMU
#define ALL_QLDST_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_X0) | (1 << TCG_REG_X1) | \
                          (1 << TCG_REG_X2) | (1 << TCG_REG_X3)))
#else
#define ALL_QLDST_REGS   ALL_GENERAL_REGS
#endif

REGS('r', ALL_GENERAL_REGS)
REGS('l', ALL_QLDST_REGS)
REGS('w', ALL_VECTOR_REGS)

CONST('A', TCG_CT_CONST_AIMM)
CONST('L', TCG_CT_CONST_LIMM)
CONST('M', TCG_CT_CONST_MONE)
CONST('O', TCG_CT_CONST_ORRI)
CONST('N', TCG_CT_CONST_ANDI)
CONST('Z', TCG_CT_CONST_ZERO)
