/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * RCU APIs for coroutines
 *
 * The RCU coroutine APIs are kept separate from the main RCU code to avoid
 * depending on AioContext APIs in rcu.c. This is necessary because at least
 * tests/unit/ptimer-test.c has replacement functions for AioContext APIs that
 * conflict with the real functions.
 *
 * It's also nice to logically separate the core RCU code from the coroutine
 * APIs :).
 */
#include "qemu/osdep.h"
#include "block/aio.h"
#include "qemu/atomic.h"
#include "qemu/coroutine.h"
#include "qemu/rcu.h"
#include "rcu-internal.h"

typedef struct {
    struct rcu_head rcu;
    Coroutine *co;
} RcuDrainCo;

static void drain_call_rcu_co_bh(void *opaque)
{
    RcuDrainCo *data = opaque;

    /* Re-enter drain_call_rcu_co() where it yielded */
    aio_co_wake(data->co);
}

static void drain_call_rcu_co_cb(struct rcu_head *node)
{
    RcuDrainCo *data = container_of(node, RcuDrainCo, rcu);
    AioContext *ctx = qemu_coroutine_get_aio_context(data->co);

    /*
     * drain_call_rcu_co() might still be running in its thread, so schedule a
     * BH in its thread. The BH only runs after the coroutine has yielded.
     */
    aio_bh_schedule_oneshot(ctx, drain_call_rcu_co_bh, data);
}

void coroutine_fn drain_call_rcu_co(void)
{
    RcuDrainCo data = {
        .co = qemu_coroutine_self(),
    };

    qatomic_inc(&in_drain_call_rcu);
    call_rcu1(&data.rcu, drain_call_rcu_co_cb);
    qemu_coroutine_yield(); /* wait for drain_rcu_co_bh() */
    qatomic_dec(&in_drain_call_rcu);
}
