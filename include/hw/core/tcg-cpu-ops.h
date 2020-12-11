/*
 * TCG-Specific operations that are not meaningful for hardware accelerators
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_CPU_OPS_H
#define TCG_CPU_OPS_H

#include "hw/core/cpu.h"

/**
 * struct TcgCpuOperations: TCG operations specific to a CPU class
 */
typedef struct TcgCpuOperations {
    /**
     * @initialize: Initalize TCG state
     *
     * Called when the first CPU is realized.
     */
    void (*initialize)(void);
    /**
     * @synchronize_from_tb: Synchronize state from a TCG #TranslationBlock
     *
     * This is called when we abandon execution of a TB before
     * starting it, and must set all parts of the CPU state which
     * the previous TB in the chain may not have updated. This
     * will need to do more. If this hook is not implemented then
     * the default is to call @set_pc(tb->pc).
     */
    void (*synchronize_from_tb)(CPUState *cpu, struct TranslationBlock *tb);
} TcgCpuOperations;

#endif /* TCG_CPU_OPS_H */
