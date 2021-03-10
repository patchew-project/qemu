/*
 * Zeroes block driver
 *
 * Based on block/null.c
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "block/block_int.h"
#include "sysemu/replay.h"

#define NULL_OPT_LATENCY "latency-ns"

typedef struct {
    int64_t length;
    int64_t latency_ns;
} BDRVZeroesState;

static QemuOptsList runtime_opts = {
    .name = "zeroes",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "size of the zeroes block",
        },
        {
            .name = NULL_OPT_LATENCY,
            .type = QEMU_OPT_NUMBER,
            .help = "nanoseconds (approximated) to wait "
                    "before completing request",
        },
        { /* end of list */ }
    },
};

static void zeroes_co_parse_filename(const char *filename, QDict *options,
                                     Error **errp)
{
    /*
     * This functions only exists so that a zeroes-co:// filename
     * is accepted with the zeroes-co driver.
     */
    if (strcmp(filename, "zeroes-co://")) {
        error_setg(errp, "The only allowed filename for this driver is "
                         "'zeroes-co://'");
        return;
    }
}

static void zeroes_aio_parse_filename(const char *filename, QDict *options,
                                      Error **errp)
{
    /*
     * This functions only exists so that a zeroes-aio:// filename
     * is accepted with the zeroes-aio driver.
     */
    if (strcmp(filename, "zeroes-aio://")) {
        error_setg(errp, "The only allowed filename for this driver is "
                         "'zeroes-aio://'");
        return;
    }
}

static int zeroes_file_open(BlockDriverState *bs, QDict *options,
                            int flags, Error **errp)
{
    QemuOpts *opts;
    BDRVZeroesState *s = bs->opaque;
    int ret = 0;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &error_abort);
    s->length = qemu_opt_get_size(opts, BLOCK_OPT_SIZE, 0);
    if (s->length < 0) {
        error_setg(errp, "%s is invalid", BLOCK_OPT_SIZE);
        ret = -EINVAL;
    }
    s->latency_ns = qemu_opt_get_number(opts, NULL_OPT_LATENCY, 0);
    if (s->latency_ns < 0) {
        error_setg(errp, "%s is invalid", NULL_OPT_LATENCY);
        ret = -EINVAL;
    }
    qemu_opts_del(opts);
    bs->supported_write_flags = BDRV_REQ_FUA;
    return ret;
}

static int64_t zeroes_getlength(BlockDriverState *bs)
{
    BDRVZeroesState *s = bs->opaque;
    return s->length;
}

static coroutine_fn int zeroes_co_common(BlockDriverState *bs)
{
    BDRVZeroesState *s = bs->opaque;

    if (s->latency_ns) {
        qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, s->latency_ns);
    }
    return 0;
}

static coroutine_fn int zeroes_co_preadv(BlockDriverState *bs,
                                         uint64_t offset, uint64_t bytes,
                                         QEMUIOVector *qiov, int flags)
{
    qemu_iovec_memset(qiov, 0, 0, bytes);

    return zeroes_co_common(bs);
}

static coroutine_fn int zeroes_co_pwritev(BlockDriverState *bs,
                                          uint64_t offset, uint64_t bytes,
                                          QEMUIOVector *qiov, int flags)
{
    return zeroes_co_common(bs);
}

static coroutine_fn int zeroes_co_flush(BlockDriverState *bs)
{
    return zeroes_co_common(bs);
}

typedef struct {
    BlockAIOCB common;
    QEMUTimer timer;
} ZeroesAIOCB;

static const AIOCBInfo zeroes_aiocb_info = {
    .aiocb_size = sizeof(ZeroesAIOCB),
};

static void zeroes_bh_cb(void *opaque)
{
    ZeroesAIOCB *acb = opaque;
    acb->common.cb(acb->common.opaque, 0);
    qemu_aio_unref(acb);
}

static void zeroes_timer_cb(void *opaque)
{
    ZeroesAIOCB *acb = opaque;
    acb->common.cb(acb->common.opaque, 0);
    timer_deinit(&acb->timer);
    qemu_aio_unref(acb);
}

static inline BlockAIOCB *zeroes_aio_common(BlockDriverState *bs,
                                            BlockCompletionFunc *cb,
                                            void *opaque)
{
    ZeroesAIOCB *acb;
    BDRVZeroesState *s = bs->opaque;

    acb = qemu_aio_get(&zeroes_aiocb_info, bs, cb, opaque);
    /* Only emulate latency after vcpu is running. */
    if (s->latency_ns) {
        aio_timer_init(bdrv_get_aio_context(bs), &acb->timer,
                       QEMU_CLOCK_REALTIME, SCALE_NS,
                       zeroes_timer_cb, acb);
        timer_mod_ns(&acb->timer,
                     qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + s->latency_ns);
    } else {
        replay_bh_schedule_oneshot_event(bdrv_get_aio_context(bs),
                                         zeroes_bh_cb, acb);
    }
    return &acb->common;
}

static BlockAIOCB *zeroes_aio_preadv(BlockDriverState *bs,
                                   uint64_t offset, uint64_t bytes,
                                   QEMUIOVector *qiov, int flags,
                                   BlockCompletionFunc *cb,
                                   void *opaque)
{
    qemu_iovec_memset(qiov, 0, 0, bytes);

    return zeroes_aio_common(bs, cb, opaque);
}

static BlockAIOCB *zeroes_aio_pwritev(BlockDriverState *bs,
                                      uint64_t offset, uint64_t bytes,
                                      QEMUIOVector *qiov, int flags,
                                      BlockCompletionFunc *cb,
                                      void *opaque)
{
    return zeroes_aio_common(bs, cb, opaque);
}

static BlockAIOCB *zeroes_aio_flush(BlockDriverState *bs,
                                    BlockCompletionFunc *cb,
                                    void *opaque)
{
    return zeroes_aio_common(bs, cb, opaque);
}

static int zeroes_reopen_prepare(BDRVReopenState *reopen_state,
                                 BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

static int coroutine_fn zeroes_co_block_status(BlockDriverState *bs,
                                               bool want_zero, int64_t offset,
                                               int64_t bytes, int64_t *pnum,
                                               int64_t *map,
                                               BlockDriverState **file)
{
    *pnum = bytes;
    *map = offset;
    *file = bs;

    return BDRV_BLOCK_OFFSET_VALID | BDRV_BLOCK_ZERO;
}

static void zeroes_refresh_filename(BlockDriverState *bs)
{
    const QDictEntry *e;

    for (e = qdict_first(bs->full_open_options); e;
         e = qdict_next(bs->full_open_options, e))
    {
        /* These options can be ignored */
        if (strcmp(qdict_entry_key(e), "filename") &&
            strcmp(qdict_entry_key(e), "driver") &&
            strcmp(qdict_entry_key(e), NULL_OPT_LATENCY))
        {
            return;
        }
    }

    snprintf(bs->exact_filename, sizeof(bs->exact_filename),
             "%s://", bs->drv->format_name);
}

static int64_t zeroes_allocated_file_size(BlockDriverState *bs)
{
    return 0;
}

static const char *const zeroes_strong_runtime_opts[] = {
    BLOCK_OPT_SIZE,

    NULL
};

static BlockDriver bdrv_zeroes_co = {
    .format_name            = "zeroes-co",
    .protocol_name          = "zeroes-co",
    .instance_size          = sizeof(BDRVZeroesState),

    .bdrv_file_open         = zeroes_file_open,
    .bdrv_parse_filename    = zeroes_co_parse_filename,
    .bdrv_getlength         = zeroes_getlength,
    .bdrv_get_allocated_file_size = zeroes_allocated_file_size,

    .bdrv_co_preadv         = zeroes_co_preadv,
    .bdrv_co_pwritev        = zeroes_co_pwritev,
    .bdrv_co_flush_to_disk  = zeroes_co_flush,
    .bdrv_reopen_prepare    = zeroes_reopen_prepare,

    .bdrv_co_block_status   = zeroes_co_block_status,

    .bdrv_refresh_filename  = zeroes_refresh_filename,
    .strong_runtime_opts    = zeroes_strong_runtime_opts,
};

static BlockDriver bdrv_zeroes_aio = {
    .format_name            = "zeroes-aio",
    .protocol_name          = "zeroes-aio",
    .instance_size          = sizeof(BDRVZeroesState),

    .bdrv_file_open         = zeroes_file_open,
    .bdrv_parse_filename    = zeroes_aio_parse_filename,
    .bdrv_getlength         = zeroes_getlength,
    .bdrv_get_allocated_file_size = zeroes_allocated_file_size,

    .bdrv_aio_preadv        = zeroes_aio_preadv,
    .bdrv_aio_pwritev       = zeroes_aio_pwritev,
    .bdrv_aio_flush         = zeroes_aio_flush,
    .bdrv_reopen_prepare    = zeroes_reopen_prepare,

    .bdrv_co_block_status   = zeroes_co_block_status,

    .bdrv_refresh_filename  = zeroes_refresh_filename,
    .strong_runtime_opts    = zeroes_strong_runtime_opts,
};

static void bdrv_zeroes_init(void)
{
    bdrv_register(&bdrv_zeroes_co);
    bdrv_register(&bdrv_zeroes_aio);
}

block_init(bdrv_zeroes_init);
