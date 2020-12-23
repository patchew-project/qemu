/* SPDX-License-Identifier: MIT */
/*
 * RISC-V target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

#define ALL_GENERAL_REGS  0xffffffffu

#ifdef CONFIG_SOFTMMU
#define ALL_QLDST_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_A0) | (1 << TCG_REG_A1) | \
                          (1 << TCG_REG_A2) | (1 << TCG_REG_A3) | \
                          (1 << TCG_REG_A5)))
#else
#define ALL_QLDST_REGS   ALL_GENERAL_REGS
#endif

REGS('r', ALL_GENERAL_REGS)
REGS('L', ALL_QLDST_REGS)

CONST('I', TCG_CT_CONST_S12)
CONST('N', TCG_CT_CONST_N12)
CONST('M', TCG_CT_CONST_M12)
CONST('Z', TCG_CT_CONST_ZERO)
