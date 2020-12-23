/* SPDX-License-Identifier: MIT */
/*
 * Sparc target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

#define RESERVE_QLDST  (7u << TCG_REG_O0)  /* O0, O1, O2 */

REGS('r', 0xffffffff)
REGS('R', ALL_64)
REGS('s', 0xffffffff & ~RESERVE_QLDST)
REGS('S', ALL_64 & ~RESERVE_QLDST)

CONST('I', TCG_CT_CONST_S11)
CONST('J', TCG_CT_CONST_S13)
CONST('Z', TCG_CT_CONST_ZERO)
