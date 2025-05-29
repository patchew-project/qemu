/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/thread.h"

/*
 * Valid transitions:
 * - FREE -> SET (qemu_event_set)
 * - BUSY -> SET (qemu_event_set)
 * - SET -> FREE (qemu_event_reset)
 * - FREE -> BUSY (qemu_event_wait)
 *
 * With futex, the waking and blocking operations follow
 * BUSY -> SET and FREE -> BUSY, respectively.
 *
 * Without futex, BUSY -> SET and FREE -> BUSY never happen. Instead, the waking
 * operation follows FREE -> SET and the blocking operation will happen in
 * qemu_event_wait() if the event is not SET.
 *
 * The following orders specified in a thread are preserved for any other thread
 * accessing the event value:
 * 1. qemu_event_set: X -> store SET
 * 2. qemu_event_reset: store FREE -> X
 * 3. qemu_event_wait: load SET -> X
 * 4. qemu_event_set: store SET -> wake
 *
 * Different combinations of orders 1, 2, 3, and 4 establish the order visible
 * to the users of QemuEvent in different situations:
 * When qemu_event_set() happens before qemu_event_reset(): orders 1 and 2
 * When qemu_event_set() happens before qemu_event_wait(): orders 1 and 3
 * when qemu_event_wait() waits for qemu_event_set(): orders 1 and 3
 *
 * Order 4 ensures that qemu_event_set() wakes qemu_event_wait() if it is
 * blocked.
 */

#define EV_SET         0
#define EV_FREE        1
#define EV_BUSY       -1

void qemu_event_init(QemuEvent *ev, bool init)
{
#ifndef HAVE_FUTEX
    pthread_mutex_init(&ev->lock, NULL);
    pthread_cond_init(&ev->cond, NULL);
#endif

    ev->value = (init ? EV_SET : EV_FREE);
    ev->initialized = true;
}

void qemu_event_destroy(QemuEvent *ev)
{
    assert(ev->initialized);
    ev->initialized = false;
#ifndef HAVE_FUTEX
    pthread_mutex_destroy(&ev->lock);
    pthread_cond_destroy(&ev->cond);
#endif
}

void qemu_event_set(QemuEvent *ev)
{
    assert(ev->initialized);

#ifdef HAVE_FUTEX
    /*
     * Transitions:
     * - FREE -> SET
     * - BUSY -> SET
     *
     * Order 1. X -> store SET
     */
    if (qatomic_xchg(&ev->value, EV_SET) == EV_BUSY) {
        /* Order 4. store SET -> wake  */
        qemu_futex_wake_all(ev);
    }
#else
    pthread_mutex_lock(&ev->lock);
    /*
     * Transition FREE -> SET
     * Order 1. X -> store SET
     */
    qatomic_store_release(&ev->value, EV_SET);
    pthread_cond_broadcast(&ev->cond);
    pthread_mutex_unlock(&ev->lock);
#endif
}

void qemu_event_reset(QemuEvent *ev)
{
    assert(ev->initialized);

    /*
     * Transition SET -> FREE
     *
     * Ensure that BUSY -> FREE never happens with an OR, which becomes a no-op
     * if the event has concurrently transitioned to FREE or BUSY.
     */
    qatomic_or(&ev->value, EV_FREE);

    /* Order 2. store FREE -> X */
    smp_mb__after_rmw();
}

void qemu_event_wait(QemuEvent *ev)
{
    assert(ev->initialized);

#ifdef HAVE_FUTEX
    while (true) {
        /* Order 3. load SET -> X */
        unsigned value = qatomic_load_acquire(&ev->value);
        if (value == EV_SET) {
            break;
        }

        if (value == EV_FREE) {
            /* Order 3. load SET -> X */
            if (qatomic_cmpxchg(&ev->value, EV_FREE, EV_BUSY) == EV_SET) {
                break;
            }
        }

        qemu_futex_wait(ev, EV_BUSY);
    }
#else
    /*
     * Order 3. load SET -> X
     *
     * qatomic_read() loads SET. ev->lock ensures the order.
     */
    pthread_mutex_lock(&ev->lock);
    while (qatomic_read(&ev->value) != EV_SET) {
        pthread_cond_wait(&ev->cond, &ev->lock);
    }
    pthread_mutex_unlock(&ev->lock);
#endif
}
