/*
 * QEMU CPU model
 *
 * Copyright (c) 2012-2014 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "exec/log.h"

void cpu_exit(CPUState *cpu)
{
    atomic_set(&cpu->exit_request, 1);
    /* Ensure cpu_exec will see the exit request after TCG has exited.  */
    smp_wmb();
    atomic_set(&cpu_neg(cpu)->icount_decr.u16.high, -1);
}

void cpu_common_reset(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", cpu->cpu_index);
        log_cpu_state(cpu, cc->reset_dump_flags);
    }

    cpu->interrupt_request = 0;
    cpu->halted = 0;
    cpu->mem_io_pc = 0;
    cpu->mem_io_vaddr = 0;
    cpu->icount_extra = 0;
    atomic_set(&cpu_neg(cpu)->icount_decr.u32, 0);
    cpu->can_do_io = 1;
    cpu->exception_index = -1;
    cpu->crash_occurred = false;
    cpu->cflags_next_tb = -1;

    if (tcg_enabled()) {
        cpu_tb_jmp_cache_clear(cpu);

        tcg_flush_softmmu_tlb(cpu);
    }
}
