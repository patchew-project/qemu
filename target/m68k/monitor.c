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
#include "qobject/qdict.h"

#ifdef CONFIG_HMP
void hmp_info_tlb(MonitorHMP *hmp, const QDict *qdict)
{
    CPUArchState *env1 = monitor_hmp_get_cpu_env(hmp);
    hwaddr start = 0, end = HWADDR_MAX;

    if (!env1) {
        monitor_hmp_printf(hmp, "No CPU available\n");
        return;
    }

    if (qdict_haskey(qdict, "start")) {
        start = (hwaddr)qdict_get_int(qdict, "start");
    }
    if (qdict_haskey(qdict, "end")) {
        end = (hwaddr)qdict_get_int(qdict, "end");
    }
    if (start > end) {
        monitor_hmp_printf(hmp, "Invalid address range: start > end.\n");
        return;
    }

    dump_mmu(env1, start, end);
}
#endif
