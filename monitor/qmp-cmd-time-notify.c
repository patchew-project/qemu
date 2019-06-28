/*
 * qmp-cmd-time-notify.c
 *
 * Copyright (c) 2016,2019 GreenSocs SAS
 *
 * Authors:
 *    Fred Konrad
 *    Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-events-misc.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "sysemu/cpus.h"
#include "trace.h"

typedef struct TimeNotifEntry TimeNotifEntry;
static QLIST_HEAD(, TimeNotifEntry) events = QLIST_HEAD_INITIALIZER(events);
static QEMUTimer *timer;

struct TimeNotifEntry {
    uint64_t time_ns;
    int64_t id;
    bool pause;
    QLIST_ENTRY(TimeNotifEntry) node;
};

static void mod_next_event_timer(void)
{
    if (QLIST_EMPTY(&events)) {
        return;
    }

    timer_mod(timer, QLIST_FIRST(&events)->time_ns);
}

static void trigger_notif(void *opaque)
{
    TimeNotifEntry *entry;
    uint64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    bool do_stop;

    entry = QLIST_FIRST(&events);
    assert(entry && entry->time_ns <= current_time);
    do_stop = entry->pause;

    QLIST_REMOVE(entry, node);
    qapi_event_send_time_notification(entry->id, current_time);
    trace_qmp_time_notify_trigger(entry->id, current_time, entry->pause);
    g_free(entry);

    mod_next_event_timer();

    if (do_stop) {
        qemu_system_vmstop_request_prepare();
        qemu_system_vmstop_request(RUN_STATE_DEBUG);
        /*
         * FIXME: should not return to device code in case
         * vm_stop() has been requested.
         */
        cpu_stop_current();
    }
}

void qmp_time_notify(int64_t event_id, int64_t time_ns,
                     bool has_relative, bool relative,
                     bool has_pause, bool pause,
                     Error **errp)
{
    TimeNotifEntry *new_entry, *entry, *prev = NULL;

    if (!timer) {
        timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, trigger_notif, NULL);
    }

    if (time_ns < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "time_ns",
                "must be positive");
    }

    new_entry = g_new0(TimeNotifEntry, 1);
    new_entry->id = event_id;
    new_entry->time_ns = time_ns;
    new_entry->pause = has_pause && pause;
    if (has_relative && relative) {
        new_entry->time_ns += qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    trace_qmp_time_notify_schedule(new_entry->id, new_entry->time_ns,
                                   new_entry->pause);

    /* find the event just before the new one */
    QLIST_FOREACH(entry, &events, node) {
        if (entry->time_ns > new_entry->time_ns) {
            break;
        }
        prev = entry;
    }

    /* then insert the new entry */
    if (prev) {
        QLIST_INSERT_AFTER(prev, new_entry, node);
    } else {
        QLIST_INSERT_HEAD(&events, new_entry, node);
        mod_next_event_timer();
    }
}
