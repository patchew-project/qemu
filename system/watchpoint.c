/*
 * CPU watchpoints
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/cputlb.h"
#include "exec/target_page.h"
#include "exec/breakpoint.h"
#include "hw/core/cpu.h"

/* Add a watchpoint.  */
CPUBreakpoint *cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr last,
                                     BreakpointFlags flags, unsigned id)
{
    CPUBreakpoint *wp;

    assert(last >= addr);
    assert(flags & BP_MEM_ACCESS);
    assert(ctpop32(flags & BP_ANY) == 1);
    assert(!(flags & ~(BP_ANY | BP_MEM_ACCESS | BP_STOP_BEFORE_ACCESS)));

    wp = g_new0(CPUBreakpoint, 1);

    wp->itree.start = addr;
    wp->itree.last = last;
    wp->flags = flags;

    interval_tree_insert(&wp->itree, &cpu->watchpoints);

    tlb_flush_range_by_mmuidx(cpu, addr, last, ALL_MMUIDX_BITS,
                              target_long_bits());
    return wp;
}

/* Remove a specific watchpoint.  */
int cpu_watchpoint_remove(CPUState *cpu, vaddr addr, vaddr len,
                          BreakpointFlags flags)
{
    vaddr last = addr + len - 1;
    IntervalTreeNode *n;

    for (n = interval_tree_iter_first(&cpu->watchpoints, addr, addr); n;
         n = interval_tree_iter_next(n, addr, addr)) {
        CPUBreakpoint *wp = container_of(n, CPUBreakpoint, itree);

        if (addr == wp->itree.start &&
            last == wp->itree.last &&
            flags == (wp->flags & ~BP_WATCHPOINT_HIT)) {
            cpu_watchpoint_remove_by_ref(cpu, wp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Remove a specific watchpoint by reference.  */
void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUBreakpoint *watchpoint)
{
    interval_tree_remove(&watchpoint->itree, &cpu->watchpoints);
    tlb_flush_page(cpu, watchpoint->itree.start);
    g_free(watchpoint);
}

/* Remove all matching watchpoints.  */
void cpu_watchpoint_remove_all(CPUState *cpu, BreakpointFlags mask)
{
    IntervalTreeNode *n = interval_tree_iter_first(&cpu->watchpoints, 0, -1);

    while (n) {
        CPUBreakpoint *wp = container_of(n, CPUBreakpoint, itree);

        n = interval_tree_iter_next(n, 0, -1);
        if (wp->flags & mask) {
            cpu_watchpoint_remove_by_ref(cpu, wp);
        }
    }
}
