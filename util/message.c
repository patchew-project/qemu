/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "qemu/message.h"
#include "monitor/monitor.h"

static int message_format;

void qmessage_set_format(int flags)
{
    message_format = flags;
}

void qmessage_context_print(FILE *fp)
{
    if (message_format & QMESSAGE_FORMAT_TIMESTAMP) {
        g_autoptr(GDateTime) dt = g_date_time_new_now_utc();
        g_autofree char *timestr = g_date_time_format_iso8601(dt);
        fprintf(fp, "%s ", timestr);
    }
}
