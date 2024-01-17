/*
 *  Reset handlers.
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "sysemu/reset.h"

/* reset/shutdown handler */

typedef struct QEMUResetEntry {
    QTAILQ_ENTRY(QEMUResetEntry) entry;
    QEMUResetHandler *func;
    void *opaque;
    bool skip_on_snapshot_load;
} QEMUResetEntry;

typedef QTAILQ_HEAD(QEMUResetList, QEMUResetEntry) QEMUResetList;
static QEMUResetList reset_handlers[QEMU_RESET_STAGES_N];

static void __attribute__((__constructor__)) qemu_reset_handlers_init(void)
{
    QEMUResetList *head;
    int i = 0;

    for (i = 0; i < QEMU_RESET_STAGES_N; i++) {
        head = &reset_handlers[i];
        QTAILQ_INIT(head);
    }
}

void qemu_register_reset_one(QEMUResetHandler *func, void *opaque,
                             bool skip_snap, int stage)
{
    QEMUResetEntry *re = g_new0(QEMUResetEntry, 1);
    QEMUResetList *head;

    assert(stage >= 0 && stage < QEMU_RESET_STAGES_N);
    head = &reset_handlers[stage];

    re->func = func;
    re->opaque = opaque;
    re->skip_on_snapshot_load = skip_snap;
    QTAILQ_INSERT_TAIL(head, re, entry);
}

void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
    qemu_register_reset_one(func, opaque, false, 0);
}

void qemu_register_reset_nosnapshotload(QEMUResetHandler *func, void *opaque)
{
    qemu_register_reset_one(func, opaque, true, 0);
}

void qemu_unregister_reset_one(QEMUResetHandler *func, void *opaque, int stage)
{
    QEMUResetList *head;
    QEMUResetEntry *re;

    assert(stage >= 0 && stage < QEMU_RESET_STAGES_N);
    head = &reset_handlers[stage];

    QTAILQ_FOREACH(re, head, entry) {
        if (re->func == func && re->opaque == opaque) {
            QTAILQ_REMOVE(head, re, entry);
            g_free(re);
            return;
        }
    }
}

void qemu_unregister_reset(QEMUResetHandler *func, void *opaque)
{
    qemu_unregister_reset_one(func, opaque, 0);
}

void qemu_devices_reset(ShutdownCause reason)
{
    QEMUResetEntry *re, *nre;
    QEMUResetList *head;
    int stage;

    /* reset all devices */
    for (stage = 0; stage < QEMU_RESET_STAGES_N; stage++) {
        head = &reset_handlers[stage];
        QTAILQ_FOREACH_SAFE(re, head, entry, nre) {
            if (reason == SHUTDOWN_CAUSE_SNAPSHOT_LOAD &&
                re->skip_on_snapshot_load) {
                continue;
            }
            re->func(re->opaque);
        }
    }
}

