/*
 * QEMU-side management of hypertrace in user-level emulation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdint.h>
#include "qemu/typedefs.h"

/* NOTE: Linux's kernel headers must be synced with this */
struct hypertrace_config {
    uint64_t max_clients;
    uint64_t client_args;
    uint64_t client_data_size;
    uint64_t control_size;
    uint64_t data_size;
};

void hypertrace_init_config(struct hypertrace_config *config,
                            unsigned int max_clients);

void hypertrace_emit(struct CPUState *cpu, uint64_t arg1, uint64_t *data);
