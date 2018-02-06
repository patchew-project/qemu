/*
 * Generic Balloon handlers and management
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Amit Shah <amit.shah@redhat.com>
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
#include "qemu-common.h"
#include "exec/cpu-common.h"
#include "sysemu/kvm.h"
#include "sysemu/balloon.h"
#include "trace-root.h"
#include "qmp-commands.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"

static QEMUBalloonEvent *balloon_event_fn;
static QEMUBalloonStatus *balloon_stat_fn;
static QEMUBalloonFreePageSupport *balloon_free_page_support_fn;
static QEMUBalloonFreePagePoll *balloon_free_page_poll_fn;
static void *balloon_opaque;
static bool balloon_inhibited;

bool qemu_balloon_is_inhibited(void)
{
    return balloon_inhibited;
}

void qemu_balloon_inhibit(bool state)
{
    balloon_inhibited = state;
}

static bool have_balloon(Error **errp)
{
    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        error_set(errp, ERROR_CLASS_KVM_MISSING_CAP,
                  "Using KVM without synchronous MMU, balloon unavailable");
        return false;
    }
    if (!balloon_event_fn) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_ACTIVE,
                  "No balloon device has been activated");
        return false;
    }
    return true;
}

bool balloon_free_page_support(void)
{
    return balloon_free_page_support_fn &&
           balloon_free_page_support_fn(balloon_opaque);
}

void balloon_free_page_poll(void)
{
    balloon_free_page_poll_fn(balloon_opaque);
}

void qemu_add_balloon_handler(QEMUBalloonEvent *event_fn,
                              QEMUBalloonStatus *stat_fn,
                              QEMUBalloonFreePageSupport *free_page_support_fn,
                              QEMUBalloonFreePagePoll *free_page_poll_fn,
                              void *opaque)
{
    if (balloon_event_fn || balloon_stat_fn || balloon_free_page_support_fn ||
        balloon_free_page_poll_fn || balloon_opaque) {
        /* We already registered one balloon handler. */
        return;
    }

    balloon_event_fn = event_fn;
    balloon_stat_fn = stat_fn;
    balloon_free_page_support_fn = free_page_support_fn;
    balloon_free_page_poll_fn = free_page_poll_fn;
    balloon_opaque = opaque;
}

void qemu_remove_balloon_handler(void *opaque)
{
    if (balloon_opaque != opaque) {
        return;
    }
    balloon_event_fn = NULL;
    balloon_stat_fn = NULL;
    balloon_free_page_support_fn = NULL;
    balloon_free_page_poll_fn = NULL;
    balloon_opaque = NULL;
}

BalloonInfo *qmp_query_balloon(Error **errp)
{
    BalloonInfo *info;

    if (!have_balloon(errp)) {
        return NULL;
    }

    info = g_malloc0(sizeof(*info));
    balloon_stat_fn(balloon_opaque, info);
    return info;
}

void qmp_balloon(int64_t target, Error **errp)
{
    if (!have_balloon(errp)) {
        return;
    }

    if (target <= 0) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "target", "a size");
        return;
    }

    trace_balloon_event(balloon_opaque, target);
    balloon_event_fn(balloon_opaque, target);
}
