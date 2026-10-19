/*
 * Keep track of ongoing BlockBackend requests, notify about excessive
 * latency (i.e. delays), and potentially even pause the VM until such a
 * request is done.
 *
 * Copyright (c) 2026 Hanna Czenczek <hreitz@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "block/block-backend-delay.h"
#include "qapi/qapi-events-block.h"
#include "system/block-backend.h"
#include "system/qtest.h"
#include "system/runstate.h"

/**
 * Information for BLOCK_IO_DELAY/DONE events so that:
 * - delay_track_timer() can collect all of the events to emit in a list and
 *   emit them outside of the BlockDelayTracker lock.
 * - blk_track_delay_end() can move the event emission to the main loop for
 *   ordering
 */
typedef struct DelayEvent DelayEvent;
struct DelayEvent {
    char *device_path;
    IoAccountingOperation op_type;
    uint64_t duration_ns;
    union {
        BlockErrorAction action; /* for BLOCK_IO_DELAY */
        int ret;                 /* for BLOCK_IO_DONE */
    };
    bool has_offset;
    uint64_t offset;
    uint64_t bytes;
    QTAILQ_ENTRY(DelayEvent) next;
};

static void delay_track_timer(void *opaque);
static void send_done_event(void *opaque);

void blk_delay_tracker_init(BlockBackend *blk)
{
    BlockDelayTracker *bdt = blk_delay_tracker(blk);

    assert(bdt != NULL);

    bdt->clock_type = qtest_enabled() ? QEMU_CLOCK_VIRTUAL
                                      : QEMU_CLOCK_REALTIME;

    qemu_mutex_init(&bdt->lock);
    QTAILQ_INIT(&bdt->requests);

    bdt->timer = aio_timer_new(qemu_get_aio_context(), bdt->clock_type,
                               SCALE_NS, delay_track_timer, blk);
}

void blk_delay_tracker_destroy(BlockDelayTracker *bdt)
{
    timer_free(bdt->timer);
    qemu_mutex_destroy(&bdt->lock);
    assert(QTAILQ_EMPTY(&bdt->requests));
    g_free(bdt);
}

void blk_set_delay_report_threshold(BlockBackend *blk, uint64_t ns)
{
    BlockDelayTracker *bdt = blk_delay_tracker(blk);

    WITH_QEMU_LOCK_GUARD(&bdt->lock) {
        bdt->threshold_ns[BLOCK_DELAY_REPORT] = ns;
    }

    /* Wake the timer immediately to adjust to the new threshold */
    timer_mod(bdt->timer, 0);
}

void blk_set_delay_stop_threshold(BlockBackend *blk, uint64_t ns)
{
    BlockDelayTracker *bdt = blk_delay_tracker(blk);

    WITH_QEMU_LOCK_GUARD(&bdt->lock) {
        bdt->threshold_ns[BLOCK_DELAY_STOP] = ns;
    }

    /* Wake the timer immediately to adjust to the new threshold */
    timer_mod(bdt->timer, 0);
}

void blk_track_delay_begin(BlockBackend *blk,
                           BlockDelayTrackerRequest *request,
                           int64_t offset,
                           uint64_t bytes,
                           IoAccountingOperation op_type)
{
    BlockDelayTracker *bdt = blk_delay_tracker(blk);

    *request = (BlockDelayTrackerRequest) {
        .offset = offset,
        .bytes = bytes,
        .op_type = op_type,
    };

    WITH_QEMU_LOCK_GUARD(&bdt->lock) {
        if (!bdt->threshold_ns[BLOCK_DELAY_REPORT] &&
            !bdt->threshold_ns[BLOCK_DELAY_STOP]) {
            /* Must be so by 0-initialization of .list */
            assert(!QTAILQ_IN_USE(request, list));
            return;
        }

        /* Set this under the lock so `bdt->requests` is correctly ordered */
        request->start_time_ns = qemu_clock_get_ns(bdt->clock_type);
        QTAILQ_INSERT_TAIL(&bdt->requests, request, list);
    }

}

void blk_track_delay_end(BlockBackend *blk,
                         BlockDelayTrackerRequest *request,
                         int ret)
{
    BlockDelayTracker *bdt = blk_delay_tracker(blk);
    bool need_done_event = false;

    if (!QTAILQ_IN_USE(request, list)) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&bdt->lock) {
        for (int i = 0; i < BLOCK_DELAY__MAX; i++) {
            need_done_event |= request->threshold_exceeded[i];
        }
        QTAILQ_REMOVE(&bdt->requests, request, list);
    }

    /* `device_path` is set if and only if any `threshold_exceeded` are set */
    assert(!!request->device_path == need_done_event);

    if (need_done_event) {
        DelayEvent *event = g_new(DelayEvent, 1);
        uint64_t now = qemu_clock_get_ns(bdt->clock_type);

        *event = (DelayEvent) {
            .device_path = request->device_path,
            .op_type = request->op_type,
            .duration_ns = usub64_saturate(now, request->start_time_ns),
            .ret = ret,
            .has_offset = request->offset >= 0,
            .offset = request->offset,
            .bytes = request->bytes,
        };

        /*
         * Emit the event in the main loop, where the timer runs, too, so we
         * always emit BLOCK_IO_DONE after all corresponding BLOCK_IO_DELAY
         * events.
         */
        aio_bh_schedule_oneshot(qemu_get_aio_context(), send_done_event, event);
    }
}

/**
 * Send a BLOCK_IO_DONE event from the `DelayEvent` given via `opaque`.
 *
 * This event, and the `device_path` in it, are freed via `g_free()`.
 */
static void send_done_event(void *opaque)
{
    DelayEvent *event = opaque;

    qapi_event_send_block_io_done(event->device_path,
                                  event->op_type,
                                  event->duration_ns,
                                  event->has_offset, event->offset,
                                  event->bytes,
                                  event->ret < 0);
    g_free(event->device_path);
    g_free(event);
}

/**
 * Track and periodically check for stalled requests.
 *
 * @opaque is the `BlockBackend`.
 *
 * Emits events when requests take longer than the BLOCK_DELAY_REPORT
 * threshold, and stops the VM if one takes longer than the BLOCK_DELAY_STOP
 * threshold.
 *
 * Handles spurious wakes.
 */
static void delay_track_timer(void *opaque)
{
    BlockBackend *blk = opaque;
    BlockDelayTracker *bdt = blk_delay_tracker(blk);
    g_autofree char *dev_path = NULL;
    bool stop_vm = false;
    uint64_t now;
    /* When to wake up next */
    uint64_t timer_time = INT64_MAX;
    /* List of events to emit */
    QTAILQ_HEAD(, DelayEvent) delay_events =
        QTAILQ_HEAD_INITIALIZER(delay_events);

    WITH_QEMU_LOCK_GUARD(&bdt->lock) {
        now = qemu_clock_get_ns(bdt->clock_type);

        for (int i = 0; i < BLOCK_DELAY__MAX; i++) {
            BlockDelayTrackerRequest *request;
            uint64_t threshold_ns = bdt->threshold_ns[i];

            if (!threshold_ns) {
                continue;
            }

            /*
             * Sleep at most for the duration of this threshold, so that we
             * will regularly check new requests as they come in.
             */
            timer_time = MIN(timer_time, uadd64_saturate(now, threshold_ns));

            QTAILQ_FOREACH(request, &bdt->requests, list) {
                DelayEvent *evt;
                char *req_dev_path;
                uint64_t duration_ns;

                /*
                 * New requests are entered at the end of the list, so it
                 * is always sorted by start_time_ns (ascending).  We can
                 * stop iterating on the first request that is not yet older
                 * than the delay threshold.
                 */
                duration_ns = usub64_saturate(now, request->start_time_ns);
                if (duration_ns < threshold_ns) {
                    /*
                     * Ensure waking when we need to emit a delay event for
                     * this request, the one closest to the delay threshold.
                     */
                    timer_time = MIN(timer_time,
                                     uadd64_saturate(request->start_time_ns,
                                                     threshold_ns));
                    break;
                }

                /* Only emit a delay event once */
                if (request->threshold_exceeded[i]) {
                    continue;
                }

                /*
                 * Always use the same device path for a single request, so use
                 * the cached path if it exists already.  Otherwise, use the
                 * current device path.
                 */
                req_dev_path = request->device_path ?: dev_path;
                if (!req_dev_path) {
                    assert(!request->device_path && !dev_path);

                    /*
                     * Fine to call under `BlockDelayTracker.lock` because this
                     * will not take a lock itself.
                     * The alternative would be to either call it before the
                     * lock (not yet knowing if there will be delayed requests),
                     * or call it after, at which point `request` may no longer
                     * be alive, which would disallow putting the result into
                     * `request`. We want to put it into `request`, though, so
                     * the `BLOCK_IO_DONE` event will always emit the same path
                     * as the `BLOCK_IO_DELAY` event(s) before.
                     * So, in order to call `blk_get_attached_dev_path()` only
                     * when needed and to make `request->device_path` work as a
                     * per-request cache, we have to call it under the lock.
                     */
                    req_dev_path = dev_path = blk_get_attached_dev_path(blk);
                }

                evt = g_new(DelayEvent, 1);
                *evt = (DelayEvent) {
                    .device_path = g_strdup(req_dev_path),
                    .op_type = request->op_type,
                    .duration_ns = duration_ns,
                    .action = i == BLOCK_DELAY_REPORT
                        ? BLOCK_ERROR_ACTION_REPORT
                        : BLOCK_ERROR_ACTION_STOP,
                    .has_offset = request->offset >= 0,
                    .offset = request->offset,
                    .bytes = request->bytes,
                };
                QTAILQ_INSERT_TAIL(&delay_events, evt, next);

                /* For BLOCK_IO_DONE */
                if (!request->device_path) {
                    request->device_path = g_strdup(req_dev_path);
                }
                request->threshold_exceeded[i] = true;

                if (i == BLOCK_DELAY_STOP) {
                    stop_vm = true;
                }
            }
        }
    }

    if (stop_vm) {
        qemu_system_vmstop_request_prepare();
    }
    while (!QTAILQ_EMPTY(&delay_events)) {
        DelayEvent *evt = QTAILQ_FIRST(&delay_events);
        QTAILQ_REMOVE(&delay_events, evt, next);

        qapi_event_send_block_io_delay(evt->device_path, evt->op_type,
                                       evt->duration_ns, evt->action,
                                       evt->has_offset, evt->offset,
                                       evt->bytes);
        g_free(evt->device_path);
        g_free(evt);
    }
    if (stop_vm) {
        qemu_system_vmstop_request(RUN_STATE_IO_HANG);
    }

    if (timer_time < INT64_MAX) {
        timer_mod_ns(bdt->timer, timer_time);
    }
}
