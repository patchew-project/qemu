/*
 * 9P Throttle
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
#include "fsdev/qemu-fsdev.h"   /* local_ops */
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include <libgen.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <string.h>
#include "fsdev/file-op-9p.h"
#include "9p-throttle.h"

void throttle9p_enable_io_limits(QemuOpts *opts, FsThrottle *fst)
{
    const float bps = qemu_opt_get_number(opts, "bps", 0);
    const float iops = qemu_opt_get_number(opts, "iops", 0);
    const float rdbps = qemu_opt_get_number(opts, "bps_rd", 0);
    const float wrbps = qemu_opt_get_number(opts, "bps_wr", 0);
    const float rdops = qemu_opt_get_number(opts, "iops_rd", 0);
    const float wrops = qemu_opt_get_number(opts, "iops_wr", 0);

    if (bps > 0 || iops > 0 || rdbps > 0 ||
        wrbps > 0 || rdops > 0 || wrops > 0) {
        fst->io_limits_enabled = true;
    } else {
        fst->io_limits_enabled = false;
    }
}

static bool throttle9p_check_for_wait(FsThrottle *fst, bool is_write)
{
    if (fst->any_timer_armed[is_write]) {
        return true;
    } else {
        return throttle_schedule_timer(&fst->ts, &fst->tt, is_write);
    }
}

static void throttle9p_schedule_next_request(FsThrottle *fst, bool is_write)
{
    bool must_wait = throttle9p_check_for_wait(fst, is_write);
    if (!fst->pending_reqs[is_write]) {
        return;
    }
    if (!must_wait) {
        if (qemu_in_coroutine() &&
            qemu_co_queue_next(&fst->throttled_reqs[is_write])) {
            ;
       } else {
           int64_t now = qemu_clock_get_ns(fst->tt.clock_type);
           timer_mod(fst->tt.timers[is_write], now + 1);
           fst->any_timer_armed[is_write] = true;
       }
   }
}

static void throttle9p_timer_cb(FsThrottle *fst, bool is_write)
{
    bool empty_queue;
    qemu_mutex_lock(&fst->lock);
    fst->any_timer_armed[is_write] = false;
    qemu_mutex_unlock(&fst->lock);
    empty_queue = !qemu_co_enter_next(&fst->throttled_reqs[is_write]);
    if (empty_queue) {
        qemu_mutex_lock(&fst->lock);
        throttle9p_schedule_next_request(fst, is_write);
        qemu_mutex_unlock(&fst->lock);
    }
}


bool throttle9p_get_io_limits_state(FsThrottle *fst)
{

    return fst->io_limits_enabled;
}

static void throttle9p_read_timer_cb(void *opaque)
{
    throttle9p_timer_cb(opaque, false);
}

static void throttle9p_write_timer_cb(void *opaque)
{
    throttle9p_timer_cb(opaque, true);
}

void throttle9p_configure_iolimits(QemuOpts *opts, FsThrottle *fst)
{
    memset(&fst->ts, 1, sizeof(fst->ts));
    memset(&fst->tt, 1, sizeof(fst->tt));
    memset(&fst->cfg, 0, sizeof(fst->cfg));
    fst->aioctx = qemu_get_aio_context();

    if (!fst->aioctx) {
        error_report("Failed to create AIO Context");
        exit(1);
    }
    throttle_init(&fst->ts);
    throttle_timers_init(&fst->tt,
                         fst->aioctx,
                         QEMU_CLOCK_REALTIME,
                         throttle9p_read_timer_cb,
                         throttle9p_write_timer_cb,
                         fst);
    throttle_config_init(&fst->cfg);
    g_assert(throttle_is_valid(&fst->cfg, NULL));

    qemu_co_queue_init(&fst->throttled_reqs[0]);
    qemu_co_queue_init(&fst->throttled_reqs[1]);
    fst->cfg.buckets[THROTTLE_BPS_TOTAL].avg =
          qemu_opt_get_number(opts, "bps", 0);
    fst->cfg.buckets[THROTTLE_BPS_READ].avg  =
          qemu_opt_get_number(opts, "bps_rd", 0);
    fst->cfg.buckets[THROTTLE_BPS_WRITE].avg =
          qemu_opt_get_number(opts, "bps_wr", 0);
    fst->cfg.buckets[THROTTLE_OPS_TOTAL].avg =
          qemu_opt_get_number(opts, "iops", 0);
    fst->cfg.buckets[THROTTLE_OPS_READ].avg =
          qemu_opt_get_number(opts, "iops_rd", 0);
    fst->cfg.buckets[THROTTLE_OPS_WRITE].avg =
          qemu_opt_get_number(opts, "iops_wr", 0);

    fst->cfg.buckets[THROTTLE_BPS_TOTAL].max =
          qemu_opt_get_number(opts, "bps_max", 0);
    fst->cfg.buckets[THROTTLE_BPS_READ].max  =
          qemu_opt_get_number(opts, "bps_rd_max", 0);
    fst->cfg.buckets[THROTTLE_BPS_WRITE].max =
          qemu_opt_get_number(opts, "bps_wr_max", 0);
    fst->cfg.buckets[THROTTLE_OPS_TOTAL].max =
          qemu_opt_get_number(opts, "iops_max", 0);
    fst->cfg.buckets[THROTTLE_OPS_READ].max =
          qemu_opt_get_number(opts, "iops_rd_max", 0);
    fst->cfg.buckets[THROTTLE_OPS_WRITE].max =
          qemu_opt_get_number(opts, "iops_wr_max", 0);

    fst->cfg.buckets[THROTTLE_BPS_TOTAL].burst_length =
          qemu_opt_get_number(opts, "throttling.bps-total-max-length", 1);
    fst->cfg.buckets[THROTTLE_BPS_READ].burst_length  =
          qemu_opt_get_number(opts, "throttling.bps-read-max-length", 1);
    fst->cfg.buckets[THROTTLE_BPS_WRITE].burst_length =
          qemu_opt_get_number(opts, "throttling.bps-write-max-length", 1);
    fst->cfg.buckets[THROTTLE_OPS_TOTAL].burst_length =
          qemu_opt_get_number(opts, "throttling.iops-total-max-length", 1);
    fst->cfg.buckets[THROTTLE_OPS_READ].burst_length =
          qemu_opt_get_number(opts, "throttling.iops-read-max-length", 1);
    fst->cfg.buckets[THROTTLE_OPS_WRITE].burst_length =
          qemu_opt_get_number(opts, "throttling.iops-write-max-length", 1);
    fst->cfg.op_size =
          qemu_opt_get_number(opts, "iops_size", 0);

    throttle_config(&fst->ts, &fst->tt, &fst->cfg);
    if (!throttle_is_valid(&fst->cfg, NULL)) {
        return;
    }

    g_assert(fst->tt.timers[0]);
    g_assert(fst->tt.timers[1]);
    fst->pending_reqs[0] = 0;
    fst->pending_reqs[1] = 0;
    fst->any_timer_armed[0] = false;
    fst->any_timer_armed[1] = false;
    qemu_mutex_init(&fst->lock);
}

void throttle9p_request(FsThrottle *fst, bool is_write, ssize_t bytes)
{
    if (fst->io_limits_enabled) {
        qemu_mutex_lock(&fst->lock);
        bool must_wait = throttle9p_check_for_wait(fst, is_write);
        if (must_wait || fst->pending_reqs[is_write]) {
            fst->pending_reqs[is_write]++;
            qemu_mutex_unlock(&fst->lock);
            qemu_co_queue_wait(&fst->throttled_reqs[is_write]);
            qemu_mutex_lock(&fst->lock);
            fst->pending_reqs[is_write]--;
       }
       throttle_account(&fst->ts, is_write, bytes);
       throttle9p_schedule_next_request(fst, is_write);
       qemu_mutex_unlock(&fst->lock);
    }
}

void throttle9p_cleanup(FsThrottle *fst)
{
    throttle_timers_destroy(&fst->tt);
    qemu_mutex_destroy(&fst->lock);
}
