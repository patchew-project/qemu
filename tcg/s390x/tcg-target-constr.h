/* SPDX-License-Identifier: MIT */
/*
 * S390 target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

REGS('r', 0xffff)
REGS('L', 0xffff & ~((1 << TCG_REG_R2) | (1 << TCG_REG_R3) | (1 << TCG_REG_R4)))
REGS('a', 1u << TCG_REG_R2)
REGS('b', 1u << TCG_REG_R3)
REGS('v', 0xffffffff00000000ull)

CONST('A', TCG_CT_CONST_S33)
CONST('I', TCG_CT_CONST_S16)
CONST('J', TCG_CT_CONST_S32)
CONST('Z', TCG_CT_CONST_ZERO)
