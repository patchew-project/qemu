/*
 * QEMU coroutine pool timer
 *
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#include "qemu/coroutine-pool-timer.h"

static void coroutine_pool_timer_cb(void *opaque)
{
    CoroutinePoolTimer *pt = opaque;
    int64_t expiry_time_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) +
                             15 * NANOSECONDS_PER_SECOND;

    qemu_coroutine_pool_periodic_resize();
    timer_mod(pt->timer, expiry_time_ns);
}

void coroutine_pool_timer_init(CoroutinePoolTimer *pt, AioContext *ctx)
{
    pt->timer = aio_timer_new(ctx, QEMU_CLOCK_REALTIME, SCALE_NS,
                              coroutine_pool_timer_cb, pt);
    coroutine_pool_timer_cb(pt);
}

void coroutine_pool_timer_cleanup(CoroutinePoolTimer *pt)
{
    timer_free(pt->timer);
    pt->timer = NULL;
}
