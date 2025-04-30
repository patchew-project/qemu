/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mcdmonitor - MCD QAPI protocol handler
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "exec/mcdstub.h"
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "mcd/mcd-qapi-init-commands.h"
#include "mcd/mcd-qapi-commands.h"
#include "monitor/monitor-internal.h"

typedef struct {
    Monitor mon;
    JSONMessageParser parser;
} MonitorMCD;

static QmpCommandList qapi_commands;
static MonitorMCD mcd_mon;

static int mcd_monitor_can_read(void *opaque)
{
    return true;
}

static void mcd_monitor_read(void *opaque, const uint8_t *buf, int size)
{
    json_message_parser_feed(&mcd_mon.parser, (const char *) buf, size);
}

static void mcd_monitor_handle_chr_event(void *opaque, QEMUChrEvent event)
{
    switch (event) {
    default:
        /* Ignore */
        break;
    }
}

static void handle_mcd_command(void *opaque, QObject *req, Error *err)
{
    QDict *rsp = qmp_dispatch(&qapi_commands, req, false, &mcd_mon.mon);
    const QObject *data = QOBJECT(rsp);
    GString *json = qobject_to_json_pretty(data, false);
    assert(json != NULL);
    g_string_append_c(json, '\n');
    monitor_puts(&mcd_mon.mon, json->str);
    g_string_free(json, true);
}

static void mcd_monitor_init(Chardev *chr, Error **errp)
{
    if (!qemu_chr_fe_init(&mcd_mon.mon.chr, chr, errp)) {
        return;
    }
    qemu_chr_fe_set_echo(&mcd_mon.mon.chr, true);

    /* For now, don't use the I/O thread */
    monitor_data_init(&mcd_mon.mon, true, false, false);

    json_message_parser_init(&mcd_mon.parser, handle_mcd_command,
                             &mcd_mon.mon, NULL);

    qemu_chr_fe_set_handlers(&mcd_mon.mon.chr, mcd_monitor_can_read,
                             mcd_monitor_read, mcd_monitor_handle_chr_event,
                             NULL, &mcd_mon.mon, NULL, true);
}

bool mcd_monitor_start(const char *device, Error **errp)
{
    Chardev *chr = qemu_chr_new_noreplay("mcd", device, true, NULL);
    if (!chr) {
        error_setg(errp, "mcdstub: couldn't create Chardev");
        return false;
    }
    mcd_monitor_init(chr, errp);
    return true;
}

static void __attribute__((__constructor__)) mcd_monitor_init_commands(void)
{
    mcd_qmp_init_marshal(&qapi_commands);
}
