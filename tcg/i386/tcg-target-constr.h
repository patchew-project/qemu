/* SPDX-License-Identifier: MIT */
/*
 * i386 target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

REGS('a', 1u << TCG_REG_EAX)
REGS('b', 1u << TCG_REG_EBX)
REGS('c', 1u << TCG_REG_ECX)
REGS('d', 1u << TCG_REG_EDX)
REGS('S', 1u << TCG_REG_ESI)
REGS('D', 1u << TCG_REG_EDI)

REGS('r', ALL_GENERAL_REGS)
REGS('x', ALL_VECTOR_REGS)
/* A register that can be used as a byte operand.  */
REGS('q', ALL_BYTEL_REGS)
/* A register with an addressable second byte (e.g. %ah).  */
REGS('Q', ALL_BYTEH_REGS)
/* qemu_ld/st address constraint */
REGS('L', ALL_GENERAL_REGS & ~((1 << TCG_REG_L0) | (1 << TCG_REG_L1)))

CONST('e', TCG_CT_CONST_S32)
CONST('I', TCG_CT_CONST_I32)
CONST('W', TCG_CT_CONST_WSZ)
CONST('Z', TCG_CT_CONST_U32)
