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
#include "exec/exec-all.h"
#include "translate-all.h"
#include "exec/ram_addr.h"

bool parallel_cpus;

void tb_flush(CPUState *cpu)
{
}

void tb_check_watchpoint(CPUState *cpu, uintptr_t retaddr)
{
}

void tb_invalidate_phys_range(ram_addr_t start, ram_addr_t end)
{
}

void tb_invalidate_phys_page_range(tb_page_addr_t start, tb_page_addr_t end)
{
}

void tb_invalidate_phys_page_fast(struct page_collection *pages,
                                  tb_page_addr_t start, int len,
                                  uintptr_t retaddr)
{
}

void tlb_init(CPUState *cpu)
{
}

void tlb_set_dirty(CPUState *cpu, target_ulong vaddr)
{
}

void tlb_flush(CPUState *cpu)
{
}

void tlb_flush_page(CPUState *cpu, target_ulong addr)
{
}

void tlb_reset_dirty(CPUState *cpu, ram_addr_t start1, ram_addr_t length)
{
}

void tcg_region_init(void)
{
}

void tcg_register_thread(void)
{
}

void tcg_flush_softmmu_tlb(CPUState *cs)
{
}

void cpu_loop_exit_noexc(CPUState *cpu)
{
    cpu->exception_index = -1;
    cpu_loop_exit(cpu);
}

void cpu_loop_exit(CPUState *cpu)
{
    cpu->can_do_io = 1;
    siglongjmp(cpu->jmp_env, 1);
}

void cpu_reloading_memory_map(void)
{
}

int cpu_exec(CPUState *cpu)
{
    return 0;
}

void cpu_exec_step_atomic(CPUState *cpu)
{
}

bool cpu_restore_state(CPUState *cpu, uintptr_t host_pc, bool will_exit)
{
    return false;
}

void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc)
{
    while (1) {
    }
}

struct page_collection *
page_collection_lock(tb_page_addr_t start, tb_page_addr_t end)
{
    return NULL;
}

void page_collection_unlock(struct page_collection *set)
{
}

void dump_exec_info(void)
{
    qemu_debug_assert(0);
}

void dump_opcount_info(void)
{
    qemu_debug_assert(0);
}
