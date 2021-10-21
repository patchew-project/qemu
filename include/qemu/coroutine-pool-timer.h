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
#ifndef COROUTINE_POOL_TIMER_H
#define COROUTINE_POOL_TIMER_H

#include "qemu/osdep.h"
#include "block/aio.h"

/**
 * A timer that periodically resizes this thread's coroutine pool, freeing
 * memory if there are too many unused coroutines.
 *
 * Threads that make heavy use of coroutines should use this. Failure to resize
 * the coroutine pool can lead to large amounts of memory sitting idle and
 * never being used after the first time.
 */
typedef struct {
    QEMUTimer *timer;
} CoroutinePoolTimer;

/* Call this before the thread runs the AioContext */
void coroutine_pool_timer_init(CoroutinePoolTimer *pt, AioContext *ctx);

/* Call this before the AioContext from the init function is destroyed */
void coroutine_pool_timer_cleanup(CoroutinePoolTimer *pt);

#endif /* COROUTINE_POOL_TIMER_H */
