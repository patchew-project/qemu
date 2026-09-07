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

#ifdef CONFIG_HMP
void hmp_info_tlb(MonitorHMP *hmp, const QDict *qdict)
{
    CPUArchState *env1 = monitor_hmp_get_cpu_env(hmp);

    if (!env1) {
        monitor_hmp_printf(hmp, "No CPU available\n");
        return;
    }

    if (qdict_haskey(qdict, "start") || qdict_haskey(qdict, "end")) {
        monitor_hmp_printf(hmp, "The range arguments will be ignored.\n");
    }

    dump_mmu(env1);
}
#endif
