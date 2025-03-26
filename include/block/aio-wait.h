/*
 * AioContext wait support
 *
 * Copyright (C) 2018 Red Hat, Inc.
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

#ifndef QEMU_AIO_WAIT_H
#define QEMU_AIO_WAIT_H

#include "block/aio.h"
#include "qemu/main-loop.h"

/**
 * AioWait:
 *
 * An object that facilitates synchronous waiting on a condition. A single
 * global AioWait object (global_aio_wait) is used internally.
 *
 * The main loop can wait on an operation running in an IOThread as follows:
 *
 *   AioContext *ctx = ...;
 *   MyWork work = { .done = false };
 *   schedule_my_work_in_iothread(ctx, &work);
 *   AIO_WAIT_WHILE(ctx, !work.done);
 *
 * The IOThread must call aio_wait_kick() to notify the main loop when
 * work.done changes:
 *
 *   static void do_work(...)
 *   {
 *       ...
 *       work.done = true;
 *       aio_wait_kick();
 *   }
 */
typedef struct {
    /* Number of waiting AIO_WAIT_WHILE() callers. Accessed with atomic ops. */
    unsigned num_waiters;
} AioWait;

extern AioWait global_aio_wait;

/**
 * AIO_WAIT_WHILE_TIMEOUT:
 * @ctx: the aio context, or NULL if multiple aio contexts (for which the
 *       caller does not hold a lock) are involved in the polling condition.
 * @cond: wait while this conditional expression is true
 * @timeout_ns: maximum duration to wait, in nanoseconds, except the value
 *              is unsigned, 0 means infinite.
 *
 * Wait while a condition is true.  Use this to implement synchronous
 * operations that require event loop activity.
 *
 * The caller must be sure that something calls aio_wait_kick() when the value
 * of @cond might have changed.
 *
 * The caller's thread must be the IOThread that owns @ctx or the main loop
 * thread (with @ctx acquired exactly once).  This function cannot be used to
 * wait on conditions between two IOThreads since that could lead to deadlock,
 * go via the main loop instead.
 *
 * Returns: 0 if succeeded; -ETIMEDOUT when a timeout occurs.
 */
#define AIO_WAIT_WHILE_TIMEOUT(ctx, cond, timeout_ns) ({           \
    int ret_ = 0;                                                  \
    uint64_t timeout_ = (timeout_ns);                              \
    AioWait *wait_ = &global_aio_wait;                             \
    AioContext *ctx_ = (ctx);                                      \
    AioContext *current_ctx_ = NULL;                               \
    QEMUTimer timer_;                                              \
    /* Increment wait_->num_waiters before evaluating cond. */     \
    qatomic_inc(&wait_->num_waiters);                              \
    /* Paired with smp_mb in aio_wait_kick(). */                   \
    smp_mb__after_rmw();                                           \
    if (ctx_ && in_aio_context_home_thread(ctx_)) {                \
        current_ctx_ = ctx_;                                       \
    } else {                                                       \
        assert(qemu_get_current_aio_context() ==                   \
               qemu_get_aio_context());                            \
        current_ctx_ = qemu_get_aio_context();                     \
    }                                                              \
    if (timeout_ > 0) {                                            \
        timer_init_full(&timer_, &current_ctx_->tlg,               \
                        QEMU_CLOCK_REALTIME,                       \
                        SCALE_NS, 0, aio_wait_timer_cb, NULL);     \
        timer_mod_ns(&timer_,                                      \
                     qemu_clock_get_ns(QEMU_CLOCK_REALTIME) +      \
                     timeout_);                                    \
    }                                                              \
    while ((cond)) {                                               \
        aio_poll(current_ctx_, true);                              \
        if (timeout_ > 0 && !timer_pending(&timer_)) {             \
            ret_ = -ETIMEDOUT;                                     \
            break;                                                 \
        }                                                          \
    }                                                              \
    if (timeout_ > 0) {                                            \
        timer_del(&timer_);                                        \
    }                                                              \
    qatomic_dec(&wait_->num_waiters);                              \
    ret_; })

#define AIO_WAIT_WHILE(ctx, cond)                                  \
    AIO_WAIT_WHILE_TIMEOUT(ctx, cond, 0)

/* TODO replace this with AIO_WAIT_WHILE() in a future patch */
#define AIO_WAIT_WHILE_UNLOCKED(ctx, cond)                         \
    AIO_WAIT_WHILE_TIMEOUT(ctx, cond, 0)

/**
 * aio_wait_kick:
 * Wake up the main thread if it is waiting on AIO_WAIT_WHILE().  During
 * synchronous operations performed in an IOThread, the main thread lets the
 * IOThread's event loop run, waiting for the operation to complete.  A
 * aio_wait_kick() call will wake up the main thread.
 */
void aio_wait_kick(void);

/**
 * aio_wait_bh_oneshot:
 * @ctx: the aio context
 * @cb: the BH callback function
 * @opaque: user data for the BH callback function
 *
 * Run a BH in @ctx and wait for it to complete.
 *
 * Must be called from the main loop thread without @ctx acquired.
 * Note that main loop event processing may occur.
 */
void aio_wait_bh_oneshot(AioContext *ctx, QEMUBHFunc *cb, void *opaque);

/**
 * in_aio_context_home_thread:
 * @ctx: the aio context
 *
 * Return whether we are running in the thread that normally runs @ctx.  Note
 * that acquiring/releasing ctx does not affect the outcome, each AioContext
 * still only has one home thread that is responsible for running it.
 */
static inline bool in_aio_context_home_thread(AioContext *ctx)
{
    if (ctx == qemu_get_current_aio_context()) {
        return true;
    }

    if (ctx == qemu_get_aio_context()) {
        return bql_locked();
    } else {
        return false;
    }
}

void aio_wait_timer_cb(void *opaque);

#endif /* QEMU_AIO_WAIT_H */
