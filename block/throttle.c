/*
 * QEMU block throttling filter driver infrastructure
 *
 * Copyright (C) Nodalink, EURL. 2014
 * Copyright (C) Igalia, S.L. 2015
 *
 * Authors:
 *   Benoît Canet <benoit.canet@nodalink.com>
 *   Alberto Garcia <berto@igalia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "block/throttle-groups.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "sysemu/qtest.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "block/throttle.h"
#include "qapi/qmp/qdict.h"

#define QEMU_OPT_IOPS_TOTAL "iops-total"
#define QEMU_OPT_IOPS_TOTAL_MAX "iops-total-max"
#define QEMU_OPT_IOPS_TOTAL_MAX_LENGTH "iops-total-max-length"
#define QEMU_OPT_IOPS_READ "iops-read"
#define QEMU_OPT_IOPS_READ_MAX "iops-read-max"
#define QEMU_OPT_IOPS_READ_MAX_LENGTH "iops-read-max-length"
#define QEMU_OPT_IOPS_WRITE "iops-write"
#define QEMU_OPT_IOPS_WRITE_MAX "iops-write-max"
#define QEMU_OPT_IOPS_WRITE_MAX_LENGTH "iops-write-max-length"
#define QEMU_OPT_BPS_TOTAL "bps-total"
#define QEMU_OPT_BPS_TOTAL_MAX "bps-total-max"
#define QEMU_OPT_BPS_TOTAL_MAX_LENGTH "bps-total-max-length"
#define QEMU_OPT_BPS_READ "bps-read"
#define QEMU_OPT_BPS_READ_MAX "bps-read-max"
#define QEMU_OPT_BPS_READ_MAX_LENGTH "bps-read-max-length"
#define QEMU_OPT_BPS_WRITE "bps-write"
#define QEMU_OPT_BPS_WRITE_MAX "bps-write-max"
#define QEMU_OPT_BPS_WRITE_MAX_LENGTH "bps-write-max-length"
#define QEMU_OPT_IOPS_SIZE "iops-size"
#define QEMU_OPT_THROTTLE_GROUP_NAME "throttle-group"


static QemuMutex throttle_groups_lock;
static QTAILQ_HEAD(, ThrottleGroup) throttle_groups =
    QTAILQ_HEAD_INITIALIZER(throttle_groups);

static QemuOptsList throttle_opts = {
    .name = "throttle",
    .head = QTAILQ_HEAD_INITIALIZER(throttle_opts.head),
    .desc = {
        {
            .name = QEMU_OPT_IOPS_TOTAL,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-total",
        },
        {
            .name = QEMU_OPT_IOPS_TOTAL_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-total-max",
        },
        {
            .name = QEMU_OPT_IOPS_TOTAL_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-total-max-length",
        },
        {
            .name = QEMU_OPT_IOPS_READ,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-read",
        },
        {
            .name = QEMU_OPT_IOPS_READ_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-read-max",
        },
        {
            .name = QEMU_OPT_IOPS_READ_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-read-max-length",
        },
        {
            .name = QEMU_OPT_IOPS_WRITE,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-write",
        },
        {
            .name = QEMU_OPT_IOPS_WRITE_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-write-max",
        },
        {
            .name = QEMU_OPT_IOPS_WRITE_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-write-max-length",
        },
        {
            .name = QEMU_OPT_BPS_TOTAL,
            .type = QEMU_OPT_NUMBER,
            .help = "trottling.bps-total",
        },
        {
            .name = QEMU_OPT_BPS_TOTAL_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "trottling.bps-total-max",
        },
        {
            .name = QEMU_OPT_BPS_TOTAL_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "trottling.bps-total-max-length",
        },
        {
            .name = QEMU_OPT_BPS_READ,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.bps-read",
        },
        {
            .name = QEMU_OPT_BPS_READ_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.bps-read-max",
        },
        {
            .name = QEMU_OPT_BPS_READ_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.bps-read-max-length",
        },
        {
            .name = QEMU_OPT_BPS_WRITE,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.bps-write",
        },
        {
            .name = QEMU_OPT_BPS_WRITE_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.bps-write-max",
        },
        {
            .name = QEMU_OPT_BPS_WRITE_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.bps-write-max-length",
        },
        {
            .name = QEMU_OPT_IOPS_SIZE,
            .type = QEMU_OPT_NUMBER,
            .help = "throttling.iops-size",
        },
        {
            .name = QEMU_OPT_THROTTLE_GROUP_NAME,
            .type = QEMU_OPT_STRING,
            .help = "Throttle group name",
        },
            { /* end of list */ }
        },
};

/* Return the next BDRVThrottleNodeState in the round-robin sequence,
 * simulating a circular list.
 *
 * This assumes that tg->lock is held.
 *
 * @s: the current BlockDriverstate's BDRVThrottleNodeState
 * @ret: the next BDRVThrottleNodeState in the sequence
 */
static BDRVThrottleNodeState *throttle_group_next_bds(BDRVThrottleNodeState *s)
{
    ThrottleGroup *tg = s->throttle_group;
    BDRVThrottleNodeState *next = QLIST_NEXT(s, round_robin);

    if (!next) {
        next = QLIST_FIRST(&tg->head);
    }

    return next;
}

/*
 * Return whether a BlockDriverState has pending requests.
 *
 * This assumes that tg->lock is held.
 *
 * @s: the BlockDriverState's BDRVThrottleNodeState
 * @is_write:  the type of operation (read/write)
 * @ret:       whether the BDRVThrottleNodeState has pending requests.
 */
static inline bool bds_has_pending_reqs(BDRVThrottleNodeState *s,
                                        bool is_write)
{
    return s->pending_reqs[is_write];
}

/* Return the next BlockDriverState in the round-robin sequence with pending
 * I/O requests.
 *
 * This assumes that tg->lock is held.
 *
 * @bs:        the current BlockDriverState's BDRVThrottleNodeState
 * @is_write:  the type of operation (read/write)
 * @ret:       the next BDRVThrottleNodeState with pending requests, or bs if
 * there is none.
 */
static BDRVThrottleNodeState *next_throttle_token(BDRVThrottleNodeState *s,
                                                    bool is_write)
{
    ThrottleGroup *tg = s->throttle_group;
    BDRVThrottleNodeState *token, *start;

    start = token = tg->tokens[is_write];

    /* get next bs round in round robin style */
    token = throttle_group_next_bds(token);
    while (token != start && !bds_has_pending_reqs(token, is_write)) {
        token = throttle_group_next_bds(token);
    }

    /* If no IO are queued for scheduling on the next round robin token
     * then decide the token is the current bs because chances are
     * the current bs get the current request queued.
     */
    if (token == start && !bds_has_pending_reqs(token, is_write)) {
        token = s;
    }

    /* Either we return the original BB, or one with pending requests */
    assert(token == s || bds_has_pending_reqs(token, is_write));

    return token;
}

/* Increments the reference count of a ThrottleGroup given its name.
 *
 * If no ThrottleGroup is found with the given name a new one is
 * created.
 *
 * @name: the name of the ThrottleGroup
 * @ret:  the ThrottleState member of the ThrottleGroup
 */
static ThrottleState *bdrv_throttle_group_incref(const char *name)
{
    ThrottleGroup *tg = NULL;
    ThrottleGroup *iter;

    qemu_mutex_lock(&throttle_groups_lock);

    /* Look for an existing group with that name */
    QTAILQ_FOREACH(iter, &throttle_groups, list) {
        if (!strcmp(name, iter->name)) {
            tg = iter;
            break;
        }
    }

    /* Create a new one if not found */
    if (!tg) {
        tg = g_new0(ThrottleGroup, 1);
        tg->name = g_strdup(name);
        qemu_mutex_init(&tg->lock);
        throttle_init(&tg->ts);
        QLIST_INIT(&tg->head);

        QTAILQ_INSERT_TAIL(&throttle_groups, tg, list);
    }

    tg->refcount++;

    qemu_mutex_unlock(&throttle_groups_lock);

    return &tg->ts;
}

/* Decrease the reference count of a ThrottleGroup.
 *
 * When the reference count reaches zero the ThrottleGroup is
 * destroyed.
 *
 * @ts:  The ThrottleGroup to unref, given by its ThrottleState member
 */
static void bdrv_throttle_group_unref(ThrottleState *ts)
{
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);

    qemu_mutex_lock(&throttle_groups_lock);
    if (--tg->refcount == 0) {
        QTAILQ_REMOVE(&throttle_groups, tg, list);
        qemu_mutex_destroy(&tg->lock);
        g_free(tg->name);
        g_free(tg);
    }
    qemu_mutex_unlock(&throttle_groups_lock);
}


/* Check if the next I/O request for a BDRVThrottleNodeState needs to be
 * throttled or not. If there's no timer set in this group, set one and
 * update the token accordingly.
 *
 * This assumes that tg->lock is held.
 *
 * @s:        the current BDRVThrottleNodeState
 * @is_write:   the type of operation (read/write)
 * @ret:        whether the I/O request needs to be throttled or not
 */
static bool throttle_group_schedule_timer(BDRVThrottleNodeState *s,
                                                    bool is_write)
{
    ThrottleGroup *tg = s->throttle_group;
    ThrottleTimers *tt = &s->throttle_timers;
    ThrottleState *ts = &tg->ts;
    bool must_wait;

    if (s->io_limits_disabled) {
        return false;
    }

    /* Check if any of the timers in this group is already armed */
    if (tg->any_timer_armed[is_write]) {
        return true;
    }

    must_wait = throttle_schedule_timer(ts, tt, is_write);

    /* If a timer just got armed, set s as the current token */
    if (must_wait) {
        tg->tokens[is_write] = s;
        tg->any_timer_armed[is_write] = true;
    }

    return must_wait;
}

/* Look for the next pending I/O request and schedule it.
 *
 * This assumes that tg->lock is held.
 *
 * @bs:       the current BlockDriverState
 * @is_write:  the type of operation (read/write)
 */
static void schedule_next_request(BlockDriverState *bs, bool is_write)
{
    BDRVThrottleNodeState *s = bs->opaque, *token = NULL;
    ThrottleGroup *tg = s->throttle_group;
    bool must_wait;

    /* Check if there's any pending request to schedule next */
    token = next_throttle_token(s, is_write);
    if (!bds_has_pending_reqs(token, is_write)) {
        return;
    }

    /* Set a timer for the request if it needs to be throttled */
    must_wait = throttle_group_schedule_timer(token, is_write);

    /* If it doesn't have to wait, queue it for immediate execution */
    if (!must_wait) {
        /* Give preference to requests from the current BDS */
        if (qemu_in_coroutine() &&
            qemu_co_queue_next(&s->throttled_reqs[is_write])) {
            token = s;
        } else {
            ThrottleTimers *tt = &token->throttle_timers;
            int64_t now = qemu_clock_get_ns(tt->clock_type);
            timer_mod(tt->timers[is_write], now + 1);
            tg->any_timer_armed[is_write] = true;
        }
        tg->tokens[is_write] = token;
    }
}

/* ThrottleTimers callback. This wakes up a request that was waiting
 * because it had been throttled.
 *
 * @bs:       the BlockDriverState whose request had been throttled
 * @is_write:  the type of operation (read/write)
 */
static void timer_cb(BlockDriverState *bs, bool is_write)
{
    BDRVThrottleNodeState *s = bs->opaque;
    ThrottleGroup *tg = s->throttle_group;
    bool empty_queue;

    /* The timer has just been fired, so we can update the flag */
    qemu_mutex_lock(&tg->lock);
    tg->any_timer_armed[is_write] = false;
    qemu_mutex_unlock(&tg->lock);

    /* Run the request that was waiting for this timer */
    aio_context_acquire(bdrv_get_aio_context(bs));
    empty_queue = !qemu_co_enter_next(&s->throttled_reqs[is_write]);
    aio_context_release(bdrv_get_aio_context(bs));

    /* If the request queue was empty then we have to take care of
     * scheduling the next one */
    if (empty_queue) {
        qemu_mutex_lock(&tg->lock);
        schedule_next_request(bs, is_write);
        qemu_mutex_unlock(&tg->lock);
    }
}

static void read_timer_cb(void *opaque)
{
    timer_cb(opaque, false);
}

static void write_timer_cb(void *opaque)
{
    timer_cb(opaque, true);
}

/* Unregister a BlockDriverState from its group, removing it from the list,
 * destroying the timers and setting the throttle_state pointer to NULL.
 *
 * The BlockDriverState must not have pending throttled requests, so the caller
 * has to drain them first.
 *
 * The group will be destroyed if it's empty after this operation.
 *
 * @bs: the BlockDriverState to remove
 */
static void throttle_node_unregister_bs(BlockDriverState *bs)
{
    BDRVThrottleNodeState *s = bs->opaque;
    ThrottleGroup *tg = s->throttle_group;
    int i;

    assert(s->pending_reqs[0] == 0 && s->pending_reqs[1] == 0);
    assert(qemu_co_queue_empty(&s->throttled_reqs[0]));
    assert(qemu_co_queue_empty(&s->throttled_reqs[1]));

    qemu_mutex_lock(&tg->lock);
    for (i = 0; i < 2; i++) {
        if (tg->tokens[i] == s) {
            BDRVThrottleNodeState *token = throttle_group_next_bds(s);
            /* Take care of the case where this is the last BlockDriverState in
             * the group */
            if (token == s) {
                token = NULL;
            }
            tg->tokens[i] = token;
        }
    }

    /* remove the current BDS from the list */
    QLIST_REMOVE(s, round_robin);
    throttle_timers_destroy(&s->throttle_timers);
    qemu_mutex_unlock(&tg->lock);

    bdrv_throttle_group_unref(&tg->ts);
    s->throttle_state = NULL;
}


/* Register a BlockDriverState in the throttling group, also initializing its
 * timers and updating its throttle_state pointer to point to it. If a
 * throttling group with that name does not exist yet, it will be created.
 *
 * @bs:       the BlockDriverState to insert
 * @groupname: the name of the group
 */
static void throttle_node_register_bs(BlockDriverState *bs,
                                      const char *groupname)
{
    int i;
    BDRVThrottleNodeState *s = bs->opaque;
    ThrottleState *ts = bdrv_throttle_group_incref(groupname);
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    int clock_type = QEMU_CLOCK_REALTIME;

    if (qtest_enabled()) {
        /* For testing block IO throttling only */
        clock_type = QEMU_CLOCK_VIRTUAL;
    }

    s->throttle_state = ts;
    s->throttle_group = tg;

    qemu_mutex_lock(&tg->lock);
    /* If the ThrottleGroup is new set this BlockDriverState as the token */
    for (i = 0; i < 2; i++) {
        if (!tg->tokens[i]) {
            tg->tokens[i] = s;
        }
    }

    QLIST_INSERT_HEAD(&tg->head, s, round_robin);
    throttle_timers_init(&s->throttle_timers,
                         bdrv_get_aio_context(bs),
                         clock_type,
                         read_timer_cb,
                         write_timer_cb,
                         bs);
    qemu_mutex_unlock(&tg->lock);
}

static int throttle_open(BlockDriverState *bs, QDict *options,
                            int flags, Error **errp)
{
    int ret = 0;
    BDRVThrottleNodeState *s = bs->opaque;
    Error *local_err = NULL;
    QemuOpts *opts = NULL;
    const char *groupname = NULL;

    bs->file = bdrv_open_child(NULL, options, "file",
                                           bs, &child_file, false, &local_err);

    if (local_err) {
        ret = -EINVAL;
        error_propagate(errp, local_err);
        return ret;
    }

    qdict_flatten(options);
    opts = qemu_opts_create(&throttle_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto open_ret;
    }

    groupname = qemu_opt_get(opts, QEMU_OPT_THROTTLE_GROUP_NAME);
    if (!groupname) {
        groupname = bdrv_get_device_or_node_name(bs);
        if (!groupname) {
            error_setg(&local_err,
                       "A group name must be specified for this device.");
            error_propagate(errp, local_err);
            ret = -EINVAL;
            goto open_ret;
        }
    }

    throttle_node_register_bs(bs, groupname);
    ThrottleConfig *throttle_cfg = &s->throttle_state->cfg;

    throttle_config_init(throttle_cfg);
    throttle_cfg->buckets[THROTTLE_BPS_TOTAL].avg =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_TOTAL, 0);
    throttle_cfg->buckets[THROTTLE_BPS_READ].avg  =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_READ, 0);
    throttle_cfg->buckets[THROTTLE_BPS_WRITE].avg =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_WRITE, 0);
    throttle_cfg->buckets[THROTTLE_OPS_TOTAL].avg =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_TOTAL, 0);
    throttle_cfg->buckets[THROTTLE_OPS_READ].avg =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_READ, 0);
    throttle_cfg->buckets[THROTTLE_OPS_WRITE].avg =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_WRITE, 0);

    throttle_cfg->buckets[THROTTLE_BPS_TOTAL].max =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_TOTAL_MAX, 0);
    throttle_cfg->buckets[THROTTLE_BPS_READ].max  =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_READ_MAX, 0);
    throttle_cfg->buckets[THROTTLE_BPS_WRITE].max =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_WRITE_MAX, 0);
    throttle_cfg->buckets[THROTTLE_OPS_TOTAL].max =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_TOTAL_MAX, 0);
    throttle_cfg->buckets[THROTTLE_OPS_READ].max =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_READ_MAX, 0);
    throttle_cfg->buckets[THROTTLE_OPS_WRITE].max =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_WRITE_MAX, 0);

    throttle_cfg->buckets[THROTTLE_BPS_TOTAL].burst_length =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_TOTAL_MAX_LENGTH, 1);
    throttle_cfg->buckets[THROTTLE_BPS_READ].burst_length  =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_READ_MAX_LENGTH, 1);
    throttle_cfg->buckets[THROTTLE_BPS_WRITE].burst_length =
        qemu_opt_get_number(opts, QEMU_OPT_BPS_WRITE_MAX_LENGTH, 1);
    throttle_cfg->buckets[THROTTLE_OPS_TOTAL].burst_length =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_TOTAL_MAX_LENGTH, 1);
    throttle_cfg->buckets[THROTTLE_OPS_READ].burst_length =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_READ_MAX_LENGTH, 1);
    throttle_cfg->buckets[THROTTLE_OPS_WRITE].burst_length =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_WRITE_MAX_LENGTH, 1);

    throttle_cfg->op_size =
        qemu_opt_get_number(opts, QEMU_OPT_IOPS_SIZE, 0);

    if (!throttle_is_valid(throttle_cfg, &local_err)) {
        error_propagate(errp, local_err);
        throttle_node_unregister_bs(bs);
        ret = -EINVAL;
        goto open_ret;
    }

    qemu_co_queue_init(&s->throttled_reqs[0]);
    qemu_co_queue_init(&s->throttled_reqs[1]);

    throttle_timers_attach_aio_context(&s->throttle_timers,
                                       bdrv_get_aio_context(bs));

open_ret:
    qemu_opts_del(opts);
    return ret;
}

static void throttle_close(BlockDriverState *bs)
{
    throttle_node_unregister_bs(bs);
    return;
}


static int64_t throttle_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);;
}

/* Check if an I/O request needs to be throttled, wait and set a timer
 * if necessary, and schedule the next request using a round robin
 * algorithm.
 *
 * @bs:       the current BlockDriverState
 * @bytes:     the number of bytes for this I/O
 * @is_write:  the type of operation (read/write)
 */
static void coroutine_fn throttle_co_io_limits_intercept(BlockDriverState *bs,
                                                    unsigned int bytes,
                                                    bool is_write)
{
    bool must_wait;
    BDRVThrottleNodeState *s = bs->opaque, *token;
    ThrottleGroup *tg = s->throttle_group;
    qemu_mutex_lock(&tg->lock);

    /* First we check if this I/O has to be throttled. */
    token = next_throttle_token(s, is_write);
    must_wait = throttle_group_schedule_timer(token, is_write);

    /* Wait if there's a timer set or queued requests of this type */
    if (must_wait || s->pending_reqs[is_write]) {
        s->pending_reqs[is_write]++;
        qemu_mutex_unlock(&tg->lock);
        qemu_co_queue_wait(&s->throttled_reqs[is_write], NULL);
        qemu_mutex_lock(&tg->lock);
        s->pending_reqs[is_write]--;
    }

    /* The I/O will be executed, so do the accounting */
    throttle_account(s->throttle_state, is_write, bytes);

    /* Schedule the next request */
    schedule_next_request(bs, is_write);

    qemu_mutex_unlock(&tg->lock);
}

static int coroutine_fn throttle_co_preadv(BlockDriverState *bs, uint64_t offset,
                                            uint64_t bytes, QEMUIOVector *qiov,
                                            int flags)
{

    throttle_co_io_limits_intercept(bs, bytes, false);

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn throttle_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                            uint64_t bytes, QEMUIOVector *qiov,
                                            int flags)
{
    throttle_co_io_limits_intercept(bs, bytes, true);

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn throttle_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int count, BdrvRequestFlags flags)
{
    throttle_co_io_limits_intercept(bs, count, true);

    return bdrv_co_pwrite_zeroes(bs->file, offset, count, flags);
}

static int coroutine_fn throttle_co_pdiscard(BlockDriverState *bs,
        int64_t offset, int count)
{
    throttle_co_io_limits_intercept(bs, count, true);

    return bdrv_co_pdiscard(bs->file->bs, offset, count);
}

static int throttle_co_flush(BlockDriverState *bs)
{
    return bdrv_co_flush(bs->file->bs);
}

static void throttle_detach_aio_context(BlockDriverState *bs)
{
    BDRVThrottleNodeState *s = bs->opaque;
    throttle_timers_detach_aio_context(&s->throttle_timers);
}

static void throttle_attach_aio_context(BlockDriverState *bs,
                                    AioContext *new_context)
{
    BDRVThrottleNodeState *s = bs->opaque;
    throttle_timers_attach_aio_context(&s->throttle_timers, new_context);
}
static BlockDriver bdrv_throttle = {
    .format_name                        =   "throttle",
    .protocol_name                      =   "throttle",
    .instance_size                      =   sizeof(BDRVThrottleNodeState),

    .bdrv_file_open                     =   throttle_open,
    .bdrv_close                         =   throttle_close,
    .bdrv_co_flush                      =   throttle_co_flush,

    .bdrv_child_perm                    =   bdrv_filter_default_perms,

    .bdrv_getlength                     =   throttle_getlength,

    .bdrv_co_preadv                     =   throttle_co_preadv,
    .bdrv_co_pwritev                    =   throttle_co_pwritev,

    .bdrv_co_pwrite_zeroes              =   throttle_co_pwrite_zeroes,
    .bdrv_co_pdiscard                   =   throttle_co_pdiscard,

    .bdrv_recurse_is_first_non_filter   =   bdrv_recurse_is_first_non_filter,

    .bdrv_attach_aio_context            =   throttle_attach_aio_context,
    .bdrv_detach_aio_context            =   throttle_detach_aio_context,

    .is_filter                          = true,
};

static void bdrv_throttle_init(void)
{
    qemu_mutex_init(&throttle_groups_lock);
    bdrv_register(&bdrv_throttle);
}

block_init(bdrv_throttle_init);
