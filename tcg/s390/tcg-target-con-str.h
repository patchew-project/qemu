/* SPDX-License-Identifier: MIT */
/*
 * Define S390 target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', 0xffff)
REGS('L', 0xffff & ~((1 << TCG_REG_R2) | (1 << TCG_REG_R3) | (1 << TCG_REG_R4)))
REGS('a', 1u << TCG_REG_R2)
REGS('b', 1u << TCG_REG_R3)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('A', TCG_CT_CONST_S33)
CONST('I', TCG_CT_CONST_S16)
CONST('J', TCG_CT_CONST_S32)
CONST('Z', TCG_CT_CONST_ZERO)
