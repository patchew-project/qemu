/*
 * QEMU-side management of hypertrace in user-level emulation.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hypertrace/common.h"
#include "qemu/osdep.h"

void hypertrace_init_config(struct hypertrace_config *config,
                            unsigned int max_clients)
{
    config->max_clients = max_clients;
    config->client_args = CONFIG_HYPERTRACE_ARGS;
    config->client_data_size = config->client_args * sizeof(uint64_t);
    config->control_size = QEMU_ALIGN_UP(
        config->max_clients * sizeof(uint64_t), TARGET_PAGE_SIZE);
    config->data_size = QEMU_ALIGN_UP(
        config->max_clients * config->client_data_size, TARGET_PAGE_SIZE);
}
