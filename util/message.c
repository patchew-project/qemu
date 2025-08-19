/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "qemu/message.h"
#include "monitor/monitor.h"

static int message_format;
static char *message_workloadname;

void qmessage_set_format(int flags)
{
    message_format = flags;
}


void qmessage_set_workload_name(const char *name)
{
    message_workloadname = g_strdup(name);
}


char *qmessage_context(int flags)
{
    g_autofree char *timestr = NULL;
    const char *wknamestr = NULL;

    if ((flags & QMESSAGE_CONTEXT_SKIP_MONITOR) &&
        monitor_cur()) {
        return g_strdup("");
    }

    if (message_format & QMESSAGE_FORMAT_TIMESTAMP) {
        g_autoptr(GDateTime) dt = g_date_time_new_now_utc();
        timestr = g_date_time_format_iso8601(dt);
    }

    if (message_format & QMESSAGE_FORMAT_WORKLOAD_NAME) {
        wknamestr = message_workloadname;
    }

    return g_strdup_printf("%s%s%s%s",
                           timestr ? timestr : "",
                           timestr ? " " : "",
                           wknamestr ? wknamestr : "",
                           wknamestr ? " " : "");
}
