/* SPDX-License-Identifier: MIT */
/*
 * Define Sparc target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', 0xffffffff)
REGS('R', ALL_64)
REGS('s', 0xffffffff & ~RESERVE_QLDST)
REGS('S', ALL_64 & ~RESERVE_QLDST)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('I', TCG_CT_CONST_S11)
CONST('J', TCG_CT_CONST_S13)
CONST('Z', TCG_CT_CONST_ZERO)
