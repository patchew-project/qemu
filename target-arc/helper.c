/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2016 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "hw/irq.h"
#include "include/hw/sysbus.h"
#include "include/sysemu/sysemu.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"

void tlb_fill(CPUState *cs, target_ulong vaddr, MMUAccessType access_type,
                                int mmu_idx, uintptr_t retaddr)
{
    target_ulong page_size = TARGET_PAGE_SIZE;
    int prot = 0;
    MemTxAttrs attrs = {};
    uint32_t paddr;

    vaddr &= TARGET_PAGE_MASK;
    paddr = PHYS_BASE_RAM + vaddr - VIRT_BASE_RAM;
    prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    tlb_set_page_with_attrs(cs, vaddr, paddr, attrs, prot, mmu_idx, page_size);
}

void arc_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = -1;
}

bool arc_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    return false;
}

int arc_cpu_memory_rw_debug(CPUState *cs, vaddr addr, uint8_t *buf,
                                int len, bool is_write)
{
    return  cpu_memory_rw_debug(cs, addr, buf, len, is_write);
}

hwaddr arc_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return  addr;   /*  I assume 1:1 address correspondance */
}

void helper_debug(CPUARCState *env)
{
    CPUState *cs = CPU(arc_env_get_cpu(env));

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}
