/*
 * QEMU-side management of hypertrace in user-level emulation.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#if !defined(__KERNEL__)
#include <stdint.h>
#endif

struct hypertrace_config {
    uint64_t max_clients;
    uint64_t client_args;
    uint64_t client_data_size;
    uint64_t control_size;
    uint64_t data_size;
};

void hypertrace_init_config(struct hypertrace_config *config,
                            unsigned int max_clients);
