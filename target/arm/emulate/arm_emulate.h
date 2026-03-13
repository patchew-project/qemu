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

/*
 * CPUState is only used as an opaque pointer (via qemu/typedefs.h).
 * Callers that dereference CPUState include hw/core/cpu.h themselves.
 */

/**
 * ArmEmulResult - return status from arm_emul_insn()
 */
typedef enum {
    ARM_EMUL_OK,         /* Instruction emulated successfully */
    ARM_EMUL_UNHANDLED,  /* Instruction not recognized by decoder */
    ARM_EMUL_ERR_MEM,    /* Memory access callback failed */
} ArmEmulResult;

/**
 * struct arm_emul_ops - hypervisor register/memory callbacks
 *
 * GPR reg 31 = SP (the XZR/SP distinction is handled internally).
 * Memory callbacks use guest virtual addresses.
 */
struct arm_emul_ops {
    uint64_t (*read_gpr)(CPUState *cpu, int reg);
    void (*write_gpr)(CPUState *cpu, int reg, uint64_t val);

    /* @size: access width in bytes (4, 8, or 16) */
    void (*read_fpreg)(CPUState *cpu, int reg, void *buf, int size);
    void (*write_fpreg)(CPUState *cpu, int reg, const void *buf, int size);

    /* Returns 0 on success, non-zero on failure */
    int (*read_mem)(CPUState *cpu, uint64_t va, void *buf, int size);
    int (*write_mem)(CPUState *cpu, uint64_t va, const void *buf, int size);
};

/**
 * arm_emul_insn - decode and emulate one AArch64 instruction
 *
 * Caller must synchronize CPU state and fetch @insn before calling.
 */
ArmEmulResult arm_emul_insn(CPUState *cpu, const struct arm_emul_ops *ops,
                            uint32_t insn);

#endif /* ARM_EMULATE_H */
