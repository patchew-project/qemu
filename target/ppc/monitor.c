/*
 * QEMU PPC (monitor definitions)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "cpu.h"
#include "qobject/qdict.h"

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env(mon);

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (qdict_haskey(qdict, "start") || qdict_haskey(qdict, "end")) {
        monitor_printf(mon, "The range arguments will be ignored.\n");
    }

    dump_mmu(env1);
}
