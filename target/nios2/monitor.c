/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"

void nios2_info_mmu(Monitor *mon, CPUNios2State *env)
{
    Nios2CPU *cpu = env_archcpu(env);

    monitor_printf(mon, "MMU: ways %d, entries %d, pid bits %d\n",
                   cpu->tlb_num_ways, cpu->tlb_num_entries,
                   cpu->pid_num_bits);

    for (int i = 0; i < cpu->tlb_num_entries; i++) {
        Nios2TLBEntry *entry = &env->mmu.tlb[i];
        monitor_printf(mon, "TLB[%d] = %08X %08X %c VPN %05X "
                       "PID %02X %c PFN %05X %c%c%c%c\n",
                       i, entry->tag, entry->data,
                       (entry->tag & (1 << 10)) ? 'V' : '-',
                       entry->tag >> 12,
                       entry->tag & ((1 << cpu->pid_num_bits) - 1),
                       (entry->tag & (1 << 11)) ? 'G' : '-',
                       FIELD_EX32(entry->data, CR_TLBACC, PFN),
                       (entry->data & CR_TLBACC_C) ? 'C' : '-',
                       (entry->data & CR_TLBACC_R) ? 'R' : '-',
                       (entry->data & CR_TLBACC_W) ? 'W' : '-',
                       (entry->data & CR_TLBACC_X) ? 'X' : '-');
    }
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env(mon);

    nios2_info_mmu(mon, env1);
}
