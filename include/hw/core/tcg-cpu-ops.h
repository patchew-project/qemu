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
} TcgCpuOperations;

#endif /* TCG_CPU_OPS_H */
