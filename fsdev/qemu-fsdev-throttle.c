/*
 * Fsdev Throttle
 *
 * Copyright (C) 2016 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Pradeep Jagadeesh <pradeep.jagadeesh@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu-fsdev-throttle.h"
#include "qemu/iov.h"
#include "qemu/option.h"
#include "qemu/throttle-options.h"

static void fsdev_throttle_read_timer_cb(void *opaque)
{
    FsThrottle *fst = opaque;
    qemu_co_enter_next(&fst->throttled_reqs[false], NULL);
}

static void fsdev_throttle_write_timer_cb(void *opaque)
{
    FsThrottle *fst = opaque;
    qemu_co_enter_next(&fst->throttled_reqs[true], NULL);
}

void fsdev_throttle_parse_opts(QemuOpts *opts, FsThrottle *fst, Error **errp)
{
    throttle_parse_options(&fst->cfg, opts);
    throttle_is_valid(&fst->cfg, errp);
}

void fsdev_throttle_init(FsThrottle *fst)
{
    if (throttle_enabled(&fst->cfg)) {
        throttle_init(&fst->ts);
        throttle_timers_init(&fst->tt,
                             qemu_get_aio_context(),
                             QEMU_CLOCK_REALTIME,
                             fsdev_throttle_read_timer_cb,
                             fsdev_throttle_write_timer_cb,
                             fst);
        throttle_config(&fst->ts, QEMU_CLOCK_REALTIME, &fst->cfg);
        qemu_co_queue_init(&fst->throttled_reqs[0]);
        qemu_co_queue_init(&fst->throttled_reqs[1]);
    }
}

void coroutine_fn fsdev_co_throttle_request(FsThrottle *fst, bool is_write,
                                            struct iovec *iov, int iovcnt)
{
    if (throttle_enabled(&fst->cfg)) {
        if (throttle_schedule_timer(&fst->ts, &fst->tt, is_write) ||
            !qemu_co_queue_empty(&fst->throttled_reqs[is_write])) {
            qemu_co_queue_wait(&fst->throttled_reqs[is_write], NULL);
        }

        throttle_account(&fst->ts, is_write, iov_size(iov, iovcnt));

        if (!qemu_co_queue_empty(&fst->throttled_reqs[is_write]) &&
            !throttle_schedule_timer(&fst->ts, &fst->tt, is_write)) {
            qemu_co_queue_next(&fst->throttled_reqs[is_write]);
        }
    }
}

void fsdev_throttle_cleanup(FsThrottle *fst)
{
    if (throttle_enabled(&fst->cfg)) {
        throttle_timers_destroy(&fst->tt);
    }
}
