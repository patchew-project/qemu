/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "qemu/message.h"
#include "monitor/monitor.h"

static int message_format = QMESSAGE_FORMAT_PROGRAM_NAME;
static char *message_workloadname;

void qmessage_set_format(int flags)
{
    message_format = flags;
}

void qmessage_set_workload_name(const char *name)
{
    message_workloadname = g_strdup(name);
}

void qmessage_context_print(FILE *fp)
{
    if (message_format & QMESSAGE_FORMAT_TIMESTAMP) {
        g_autoptr(GDateTime) dt = g_date_time_new_now_utc();
        g_autofree char *timestr = g_date_time_format_iso8601(dt);
        fprintf(fp, "%s ", timestr);
    }

    if ((message_format & QMESSAGE_FORMAT_WORKLOAD_NAME) &&
        message_workloadname) {
        fprintf(fp, "%s ", message_workloadname);
    }

    if (message_format & QMESSAGE_FORMAT_PROGRAM_NAME) {
        const char *pgnamestr = g_get_prgname();
        if (pgnamestr) {
            fprintf(fp, "%s: ", pgnamestr);
        }
    }
}
