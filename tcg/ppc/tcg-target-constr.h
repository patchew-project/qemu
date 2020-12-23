/* SPDX-License-Identifier: MIT */
/*
 * PowerPC target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

#define ALL_GENERAL_REGS  0xffffffffu
#define ALL_VECTOR_REGS   0xffffffff00000000ull

#ifdef CONFIG_SOFTMMU
#define ALL_QLOAD_REGS \
    (ALL_GENERAL_REGS & \
     ~((1 << TCG_REG_R3) | (1 << TCG_REG_R4) | (1 << TCG_REG_R5)))
#define ALL_QSTORE_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_R3) | (1 << TCG_REG_R4) | \
                          (1 << TCG_REG_R5) | (1 << TCG_REG_R6)))
#else
#define ALL_QLOAD_REGS  (ALL_GENERAL_REGS & ~(1 << TCG_REG_R3))
#define ALL_QSTORE_REGS ALL_QLOAD_REGS
#endif

REGS('r', ALL_GENERAL_REGS)
REGS('v', ALL_VECTOR_REGS)
REGS('A', 1u << TCG_REG_R3)
REGS('B', 1u << TCG_REG_R4)
REGS('C', 1u << TCG_REG_R5)
REGS('D', 1u << TCG_REG_R6)
REGS('L', ALL_QLOAD_REGS)
REGS('S', ALL_QSTORE_REGS)

CONST('I', TCG_CT_CONST_S16)
CONST('J', TCG_CT_CONST_U16)
CONST('M', TCG_CT_CONST_MONE)
CONST('T', TCG_CT_CONST_S32)
CONST('U', TCG_CT_CONST_U32)
CONST('W', TCG_CT_CONST_WSZ)
CONST('Z', TCG_CT_CONST_ZERO)
