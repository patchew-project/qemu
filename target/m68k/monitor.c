/*
 * QEMU monitor for m68k
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env(mon);

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    dump_mmu(env1);
}

static const MonitorDef monitor_defs[] = {
    { "ssp", offsetof(CPUM68KState, sp[0]) },
    { "usp", offsetof(CPUM68KState, sp[1]) },
    { "isp", offsetof(CPUM68KState, sp[2]) },
    { "sfc", offsetof(CPUM68KState, sfc) },
    { "dfc", offsetof(CPUM68KState, dfc) },
    { "urp", offsetof(CPUM68KState, mmu.urp) },
    { "srp", offsetof(CPUM68KState, mmu.srp) },
    { "dttr0", offsetof(CPUM68KState, mmu.ttr[M68K_DTTR0]) },
    { "dttr1", offsetof(CPUM68KState, mmu.ttr[M68K_DTTR1]) },
    { "ittr0", offsetof(CPUM68KState, mmu.ttr[M68K_ITTR0]) },
    { "ittr1", offsetof(CPUM68KState, mmu.ttr[M68K_ITTR1]) },
    { "mmusr", offsetof(CPUM68KState, mmu.mmusr) },
    { NULL },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}
