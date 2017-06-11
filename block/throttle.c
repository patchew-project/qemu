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
#include "block/throttle-groups.h"
#include "qemu/throttle-options.h"
#include "qapi/error.h"


static QemuOptsList throttle_opts = {
    .name = "throttle",
    .head = QTAILQ_HEAD_INITIALIZER(throttle_opts.head),
    .desc = {
        {
            .name = QEMU_OPT_IOPS_TOTAL,
            .type = QEMU_OPT_NUMBER,
            .help = "limit total I/O operations per second",
        },{
            .name = QEMU_OPT_IOPS_READ,
            .type = QEMU_OPT_NUMBER,
            .help = "limit read operations per second",
        },{
            .name = QEMU_OPT_IOPS_WRITE,
            .type = QEMU_OPT_NUMBER,
            .help = "limit write operations per second",
        },{
            .name = QEMU_OPT_BPS_TOTAL,
            .type = QEMU_OPT_NUMBER,
            .help = "limit total bytes per second",
        },{
            .name = QEMU_OPT_BPS_READ,
            .type = QEMU_OPT_NUMBER,
            .help = "limit read bytes per second",
        },{
            .name = QEMU_OPT_BPS_WRITE,
            .type = QEMU_OPT_NUMBER,
            .help = "limit write bytes per second",
        },{
            .name = QEMU_OPT_IOPS_TOTAL_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "I/O operations burst",
        },{
            .name = QEMU_OPT_IOPS_READ_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "I/O operations read burst",
        },{
            .name = QEMU_OPT_IOPS_WRITE_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "I/O operations write burst",
        },{
            .name = QEMU_OPT_BPS_TOTAL_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "total bytes burst",
        },{
            .name = QEMU_OPT_BPS_READ_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "total bytes read burst",
        },{
            .name = QEMU_OPT_BPS_WRITE_MAX,
            .type = QEMU_OPT_NUMBER,
            .help = "total bytes write burst",
        },{
            .name = QEMU_OPT_IOPS_TOTAL_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "length of the iopstotalmax burst period, in seconds",
        },{
            .name = QEMU_OPT_IOPS_READ_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "length of the iopsreadmax burst period, in seconds",
        },{
            .name = QEMU_OPT_IOPS_WRITE_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "length of the iopswritemax burst period, in seconds",
        },{
            .name = QEMU_OPT_BPS_TOTAL_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "length of the bpstotalmax burst period, in seconds",
        },{
            .name = QEMU_OPT_BPS_READ_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "length of the bpsreadmax burst period, in seconds",
        },{
            .name = QEMU_OPT_BPS_WRITE_MAX_LENGTH,
            .type = QEMU_OPT_NUMBER,
            .help = "length of the bpswritemax burst period, in seconds",
        },{
            .name = QEMU_OPT_IOPS_SIZE,
            .type = QEMU_OPT_NUMBER,
            .help = "when limiting by iops max size of an I/O in bytes",
        },
        {
            .name = QEMU_OPT_THROTTLE_GROUP_NAME,
            .type = QEMU_OPT_STRING,
            .help = "throttle group name",
        },
        { /* end of list */ }
    },
};

static int throttle_open(BlockDriverState *bs, QDict *options,
                            int flags, Error **errp)
{
    int ret = 0;
    ThrottleGroupMember *tgm = bs->opaque;
    Error *local_err = NULL;
    QemuOpts *opts = NULL;
    const char *group_name = NULL;

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
        goto fin;
    }

    group_name = qemu_opt_get(opts, QEMU_OPT_THROTTLE_GROUP_NAME);
    if (!group_name) {
        group_name = bdrv_get_device_or_node_name(bs);
        if (!strlen(group_name)) {
            error_setg(&local_err,
                       "A group name must be specified for this device.");
            error_propagate(errp, local_err);
            ret = -EINVAL;
            goto fin;
        }
    }

    tgm->aio_context = bdrv_get_aio_context(bs);
    throttle_group_register_tgm(tgm, group_name);
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    ThrottleConfig *throttle_cfg = &ts->cfg;


    qemu_mutex_lock(&tg->lock);
    if (qemu_opt_get(opts, QEMU_OPT_BPS_TOTAL)) {
        throttle_cfg->buckets[THROTTLE_BPS_TOTAL].avg =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_TOTAL, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_BPS_READ)) {
        throttle_cfg->buckets[THROTTLE_BPS_READ].avg  =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_READ, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_BPS_WRITE)) {
        throttle_cfg->buckets[THROTTLE_BPS_WRITE].avg =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_WRITE, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_TOTAL)) {
        throttle_cfg->buckets[THROTTLE_OPS_TOTAL].avg =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_TOTAL, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_READ)) {
        throttle_cfg->buckets[THROTTLE_OPS_READ].avg =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_READ, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_WRITE)) {
        throttle_cfg->buckets[THROTTLE_OPS_WRITE].avg =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_WRITE, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_BPS_TOTAL_MAX)) {
        throttle_cfg->buckets[THROTTLE_BPS_TOTAL].max =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_TOTAL_MAX, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_BPS_READ_MAX)) {
        throttle_cfg->buckets[THROTTLE_BPS_READ].max  =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_READ_MAX, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_BPS_WRITE_MAX)) {
        throttle_cfg->buckets[THROTTLE_BPS_WRITE].max =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_WRITE_MAX, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_TOTAL_MAX)) {
        throttle_cfg->buckets[THROTTLE_OPS_TOTAL].max =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_TOTAL_MAX, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_READ_MAX)) {
        throttle_cfg->buckets[THROTTLE_OPS_READ].max =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_READ_MAX, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_WRITE_MAX)) {
        throttle_cfg->buckets[THROTTLE_OPS_WRITE].max =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_WRITE_MAX, 0);
    }
    if (qemu_opt_get(opts, QEMU_OPT_BPS_TOTAL_MAX_LENGTH)) {
        throttle_cfg->buckets[THROTTLE_BPS_TOTAL].burst_length =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_TOTAL_MAX_LENGTH, 1);
    }
    if (qemu_opt_get(opts, QEMU_OPT_BPS_READ_MAX_LENGTH)) {
        throttle_cfg->buckets[THROTTLE_BPS_READ].burst_length  =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_READ_MAX_LENGTH, 1);
    }
    if (qemu_opt_get(opts, QEMU_OPT_BPS_WRITE_MAX_LENGTH)) {
        throttle_cfg->buckets[THROTTLE_BPS_WRITE].burst_length =
            qemu_opt_get_number(opts, QEMU_OPT_BPS_WRITE_MAX_LENGTH, 1);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_TOTAL_MAX_LENGTH)) {
        throttle_cfg->buckets[THROTTLE_OPS_TOTAL].burst_length =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_TOTAL_MAX_LENGTH, 1);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_READ_MAX_LENGTH)) {
        throttle_cfg->buckets[THROTTLE_OPS_READ].burst_length =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_READ_MAX_LENGTH, 1);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_WRITE_MAX_LENGTH)) {
        throttle_cfg->buckets[THROTTLE_OPS_WRITE].burst_length =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_WRITE_MAX_LENGTH, 1);
    }
    if (qemu_opt_get(opts, QEMU_OPT_IOPS_SIZE)) {
        throttle_cfg->op_size =
            qemu_opt_get_number(opts, QEMU_OPT_IOPS_SIZE, 0);
    }

    if (!throttle_is_valid(throttle_cfg, &local_err)) {
        error_propagate(errp, local_err);
        throttle_group_unregister_tgm(tgm);
        ret = -EINVAL;
        goto fin;
    }

    qemu_mutex_unlock(&tg->lock);

    qemu_co_queue_init(&tgm->throttled_reqs[0]);
    qemu_co_queue_init(&tgm->throttled_reqs[1]);

fin:
    qemu_opts_del(opts);
    return ret;
}

static void throttle_close(BlockDriverState *bs)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_unregister_tgm(tgm);
    return;
}


static int64_t throttle_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}


static int coroutine_fn throttle_co_preadv(BlockDriverState *bs, uint64_t offset,
                                            uint64_t bytes, QEMUIOVector *qiov,
                                            int flags)
{

    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, false);

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn throttle_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                            uint64_t bytes, QEMUIOVector *qiov,
                                            int flags)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn throttle_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int bytes, BdrvRequestFlags flags)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn throttle_co_pdiscard(BlockDriverState *bs,
        int64_t offset, int bytes)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_pdiscard(bs->file->bs, offset, bytes);
}

static int throttle_co_flush(BlockDriverState *bs)
{
    return bdrv_co_flush(bs->file->bs);
}

static void throttle_detach_aio_context(BlockDriverState *bs)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_timers_detach_aio_context(&tgm->throttle_timers);
}

static void throttle_attach_aio_context(BlockDriverState *bs,
                                    AioContext *new_context)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_timers_attach_aio_context(&tgm->throttle_timers, new_context);
}
static BlockDriver bdrv_throttle = {
    .format_name                        =   "throttle",
    .protocol_name                      =   "throttle",
    .instance_size                      =   sizeof(ThrottleGroupMember),

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

    .is_filter                          =   true,
};

static void bdrv_throttle_init(void)
{
    bdrv_register(&bdrv_throttle);
}

block_init(bdrv_throttle_init);
