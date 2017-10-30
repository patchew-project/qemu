/*
 * QEMU TCG accelerator stub
 *
 * Copyright Red Hat, Inc. 2013
 *
 * Author: Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "exec/cpu-common.h"
#include "exec/exec-all.h"

bool parallel_cpus;

void tb_flush(CPUState *cpu)
{
}

void tlb_reset_dirty(CPUState *cpu, ram_addr_t start1, ram_addr_t length)
{
}

int cpu_exec(CPUState *cpu)
{
    return -1;
}

void cpu_exec_step_atomic(CPUState *cpu)
{
}

void cpu_reloading_memory_map(void)
{
}
