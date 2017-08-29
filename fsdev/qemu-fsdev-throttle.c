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
#include "qemu/main-loop.h"
#include "qemu/throttle-options.h"

static void fsdev_throttle_read_timer_cb(void *opaque)
{
    FsThrottle *fst = opaque;
    qemu_co_enter_next(&fst->throttled_reqs[false]);
}

static void fsdev_throttle_write_timer_cb(void *opaque)
{
    FsThrottle *fst = opaque;
    qemu_co_enter_next(&fst->throttled_reqs[true]);
}

void fsdev_set_io_throttle(IOThrottle *arg, FsThrottle *fst, Error **errp)
{
    ThrottleConfig cfg;

    throttle_set_io_limits(&cfg, arg);

    if (throttle_is_valid(&cfg, errp)) {
        fst->cfg = cfg;
        fsdev_throttle_init(fst);
    }
}

void fsdev_get_io_throttle(FsThrottle *fst, IOThrottle **fs9pcfg,
                           char *fsdevice, Error **errp)
{

    ThrottleConfig cfg = fst->cfg;
    IOThrottle *fscfg = g_malloc0(sizeof(*fscfg));

    fscfg->has_id = true;
    fscfg->id = g_strdup(fsdevice);
    fscfg->bps = cfg.buckets[THROTTLE_BPS_TOTAL].avg;
    fscfg->bps_rd = cfg.buckets[THROTTLE_BPS_READ].avg;
    fscfg->bps_wr = cfg.buckets[THROTTLE_BPS_WRITE].avg;

    fscfg->iops = cfg.buckets[THROTTLE_OPS_TOTAL].avg;
    fscfg->iops_rd = cfg.buckets[THROTTLE_OPS_READ].avg;
    fscfg->iops_wr = cfg.buckets[THROTTLE_OPS_WRITE].avg;

    fscfg->has_bps_max     = cfg.buckets[THROTTLE_BPS_TOTAL].max;
    fscfg->bps_max         = cfg.buckets[THROTTLE_BPS_TOTAL].max;
    fscfg->has_bps_rd_max  = cfg.buckets[THROTTLE_BPS_READ].max;
    fscfg->bps_rd_max      = cfg.buckets[THROTTLE_BPS_READ].max;
    fscfg->has_bps_wr_max  = cfg.buckets[THROTTLE_BPS_WRITE].max;
    fscfg->bps_wr_max      = cfg.buckets[THROTTLE_BPS_WRITE].max;

    fscfg->has_iops_max    = cfg.buckets[THROTTLE_OPS_TOTAL].max;
    fscfg->iops_max        = cfg.buckets[THROTTLE_OPS_TOTAL].max;
    fscfg->has_iops_rd_max = cfg.buckets[THROTTLE_OPS_READ].max;
    fscfg->iops_rd_max     = cfg.buckets[THROTTLE_OPS_READ].max;
    fscfg->has_iops_wr_max = cfg.buckets[THROTTLE_OPS_WRITE].max;
    fscfg->iops_wr_max     = cfg.buckets[THROTTLE_OPS_WRITE].max;

    fscfg->has_bps_max_length     = fscfg->has_bps_max;
    fscfg->bps_max_length         =
         cfg.buckets[THROTTLE_BPS_TOTAL].burst_length;
    fscfg->has_bps_rd_max_length  = fscfg->has_bps_rd_max;
    fscfg->bps_rd_max_length      =
         cfg.buckets[THROTTLE_BPS_READ].burst_length;
    fscfg->has_bps_wr_max_length  = fscfg->has_bps_wr_max;
    fscfg->bps_wr_max_length      =
         cfg.buckets[THROTTLE_BPS_WRITE].burst_length;

    fscfg->has_iops_max_length    = fscfg->has_iops_max;
    fscfg->iops_max_length        =
         cfg.buckets[THROTTLE_OPS_TOTAL].burst_length;
    fscfg->has_iops_rd_max_length = fscfg->has_iops_rd_max;
    fscfg->iops_rd_max_length     =
         cfg.buckets[THROTTLE_OPS_READ].burst_length;
    fscfg->has_iops_wr_max_length = fscfg->has_iops_wr_max;
    fscfg->iops_wr_max_length     =
         cfg.buckets[THROTTLE_OPS_WRITE].burst_length;

    fscfg->bps_max_length = cfg.buckets[THROTTLE_BPS_TOTAL].burst_length;
    fscfg->bps_rd_max_length = cfg.buckets[THROTTLE_BPS_READ].burst_length;
    fscfg->bps_wr_max_length = cfg.buckets[THROTTLE_BPS_WRITE].burst_length;
    fscfg->iops_max_length = cfg.buckets[THROTTLE_OPS_TOTAL].burst_length;
    fscfg->iops_rd_max_length = cfg.buckets[THROTTLE_OPS_READ].burst_length;
    fscfg->iops_wr_max_length = cfg.buckets[THROTTLE_OPS_WRITE].burst_length;

    fscfg->iops_size = cfg.op_size;

    *fs9pcfg = fscfg;
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
