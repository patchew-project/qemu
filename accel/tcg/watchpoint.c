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
#include "qemu/main-loop.h"
#include "exec/breakpoint.h"
#include "exec/cpu-interrupt.h"
#include "exec/page-protection.h"
#include "exec/translation-block.h"
#include "system/tcg.h"
#include "system/replay.h"
#include "accel/tcg/cpu-loop.h"
#include "accel/tcg/cpu-ops.h"
#include "hw/core/cpu.h"
#include "internal-common.h"

/* Return flags for watchpoints that match addr + prot.  */
BreakpointFlags cpu_watchpoint_address_matches(CPUState *cpu,
                                               vaddr addr, vaddr len)
{
    vaddr last = addr + len - 1;
    BreakpointFlags ret = 0;
    IntervalTreeNode *n;

    for (n = interval_tree_iter_first(&cpu->watchpoints, addr, last); n;
         n = interval_tree_iter_next(n, addr, last)) {
        CPUBreakpoint *wp = container_of(n, CPUBreakpoint, itree);
        ret |= wp->flags;
    }
    return ret;
}

/* Generate a debug exception if a watchpoint has been hit.  */
void cpu_check_watchpoint(CPUState *cpu, vaddr addr, vaddr len,
                          MemTxAttrs attrs, BreakpointFlags flags, uintptr_t ra)
{
    vaddr last = addr + len - 1;
    IntervalTreeNode *n;
    CPUBreakpoint *found_wp = NULL;
    bool have_cpu_wp = false;

    assert(tcg_enabled());
    if (cpu->bp_wp_hit) {
        /*
         * We re-entered the check after replacing the TB.
         * Now raise the debug interrupt so that it will
         * trigger after the current instruction.
         */
        bql_lock();
        cpu_interrupt(cpu, CPU_INTERRUPT_DEBUG);
        bql_unlock();
        return;
    }

    if (cpu->cc->tcg_ops->adjust_watchpoint_address) {
        /* this is currently used only by ARM BE32 */
        addr = cpu->cc->tcg_ops->adjust_watchpoint_address(cpu, addr, len);
        last = addr + len - 1;
    }

    assert((flags & ~BP_MEM_ACCESS) == 0);

    for (n = interval_tree_iter_first(&cpu->watchpoints, addr, last); n;
         n = interval_tree_iter_next(n, addr, last)) {
        CPUBreakpoint *wp = container_of(n, CPUBreakpoint, itree);

        if (wp->flags & flags) {
            /*
             * Prefer GDB over CPU watchpoint, so we can take the first
             * GDB watchpoint that we see.
             */
            if (wp->flags & BP_GDB) {
                wp->flags &= ~BP_WATCHPOINT_HIT;
                wp->flags |= flags << BP_HIT_SHIFT;
                wp->hitaddr = MAX(addr, wp->itree.start);
                wp->hitlast = MIN(last, wp->itree.last);
                wp->hitattrs = attrs;

                found_wp = wp;
                goto found;
            }
            have_cpu_wp = true;
        }
    }

    if (have_cpu_wp) {
        const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;

        for (n = interval_tree_iter_first(&cpu->watchpoints, addr, last); n;
             n = interval_tree_iter_next(n, addr, last)) {
            CPUBreakpoint *wp = container_of(n, CPUBreakpoint, itree);

            if ((wp->flags & BP_CPU) && (wp->flags & flags)) {
                wp->flags &= ~BP_WATCHPOINT_HIT;
                wp->flags |= flags << BP_HIT_SHIFT;
                wp->hitaddr = MAX(addr, wp->itree.start);
                wp->hitlast = MIN(last, wp->itree.last);
                wp->hitattrs = attrs;

                if (tcg_ops->debug_check_watchpoint
                    && !tcg_ops->debug_check_watchpoint(cpu, wp)) {
                    wp->flags &= ~BP_WATCHPOINT_HIT;
                    continue;
                }

                found_wp = wp;
                goto found;
            }
        }
    }
    return;

 found:
    if (replay_running_debug()) {
        /*
         * replay_breakpoint reads icount.
         * Force recompile to succeed, because icount may
         * be read only at the end of the block.
         */
        if (!cpu->neg.can_do_io) {
            /* Force execution of one insn next time.  */
            cpu->cflags_next_tb = 1 | CF_NOIRQ | curr_cflags(cpu);
            cpu_loop_exit_restore(cpu, ra);
        }
        /*
         * Don't process the watchpoints when we are
         * in a reverse debugging operation.
         */
        replay_breakpoint();
        return;
    }

    cpu->bp_wp_hit = found_wp;

    /* This call also restores vCPU state */
    tb_check_watchpoint(cpu, ra);
    if (found_wp->flags & BP_STOP_BEFORE_ACCESS) {
        cpu->exception_index = EXCP_DEBUG;
        cpu_loop_exit(cpu);
    } else {
        /* Force execution of one insn next time.  */
        cpu->cflags_next_tb = 1 | CF_NOIRQ | curr_cflags(cpu);
        cpu_loop_exit_noexc(cpu);
    }
    qemu_build_not_reached();
}
