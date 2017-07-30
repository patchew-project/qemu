/*
 * QEMU-side management of hypertrace in user-level emulation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu-all.h"
#include "hypertrace/common.h"
#include "hypertrace/trace.h"


void hypertrace_init_config(struct hypertrace_config *config,
                            unsigned int max_clients)
{
    config->max_clients = max_clients;
    config->client_args = CONFIG_HYPERTRACE_ARGS;
    config->client_data_size = config->client_args * sizeof(uint64_t);

    /* Align for both, since they can be used on softmmu and user mode */
    int page_size = 1;
    page_size = QEMU_ALIGN_UP(page_size, getpagesize());
    page_size = QEMU_ALIGN_UP(page_size, TARGET_PAGE_SIZE);

#if defined(CONFIG_USER_ONLY)
    /* Twice the number of clients (*in pages*) for the double-fault protocol */
    config->control_size = QEMU_ALIGN_UP(
        config->max_clients * TARGET_PAGE_SIZE * 2, page_size);
#else
    config->control_size = QEMU_ALIGN_UP(
        config->max_clients * sizeof(uint64_t), page_size);
#endif
    config->data_size = QEMU_ALIGN_UP(
        config->max_clients * config->client_data_size, page_size);
}


#include "hypertrace/emit.c"

void hypertrace_emit(CPUState *cpu, uint64_t arg1, uint64_t *data)
{
    int i;
    /* swap event arguments to host endianness */
    arg1 = tswap64(arg1);
    for (i = 0; i < CONFIG_HYPERTRACE_ARGS - 1; i++) {
        data[i] = tswap64(data[i]);
    }

    /* emit event */
    do_hypertrace_emit(cpu, arg1, data);
}
