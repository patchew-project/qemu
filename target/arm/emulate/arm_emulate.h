/*
 * AArch64 instruction emulation library
 *
 * Copyright (c) 2026 Lucas Amaral <lucaaamaral@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ARM_EMULATE_H
#define ARM_EMULATE_H

#include "qemu/osdep.h"

/**
 * ArmEmulResult - return status from arm_emul_insn()
 */
typedef enum {
    ARM_EMUL_OK,         /* Instruction emulated successfully */
    ARM_EMUL_UNHANDLED,  /* Instruction not recognized by decoder */
    ARM_EMUL_ERR_MEM,    /* Memory access failed */
} ArmEmulResult;

/**
 * arm_emul_insn - decode and emulate one AArch64 instruction
 *
 * Caller must synchronize CPU state and fetch @insn before calling.
 */
ArmEmulResult arm_emul_insn(CPUArchState *env, uint32_t insn);

#endif /* ARM_EMULATE_H */
