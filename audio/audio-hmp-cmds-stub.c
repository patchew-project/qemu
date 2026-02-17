/*
 * Stub for audio-hmp-cmds.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "audio_int.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qemu/error-report.h"

void hmp_info_capture(Monitor *mon, const QDict *qdict)
{
    monitor_puts(mon, "audio subsystem is disabled at compile time");
}

void hmp_stopcapture(Monitor *mon, const QDict *qdict)
{
    monitor_puts(mon, "audio subsystem is disabled at compile time");
}

void hmp_wavcapture(Monitor *mon, const QDict *qdict)
{
    monitor_puts(mon, "audio subsystem is disabled at compile time");
}
