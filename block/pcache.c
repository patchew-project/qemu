/*
 * Prefetch cache driver filter
 *
 * Copyright (C) 2015-2016 Parallels IP Holdings GmbH. All rights reserved.
 *
 * Author: Pavel Butsykin <pbutsykin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qapi/error.h"
#include "qapi/qmp/qstring.h"


static QemuOptsList runtime_opts = {
    .name = "pcache",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "x-image",
            .type = QEMU_OPT_STRING,
            .help = "[internal use only, will be removed]",
        },
        { /* end of list */ }
    },
};

typedef struct PCacheAIOCB {
    Coroutine *co;
    int ret;
} PCacheAIOCB;

static void pcache_aio_cb(void *opaque, int ret)
{
    PCacheAIOCB *acb = opaque;

    acb->ret = ret;
    qemu_coroutine_enter(acb->co);
}

static coroutine_fn int pcache_co_preadv(BlockDriverState *bs, uint64_t offset,
                                         uint64_t bytes, QEMUIOVector *qiov,
                                         int flags)
{
    PCacheAIOCB acb = {
        .co = qemu_coroutine_self(),
    };

    bdrv_aio_preadv(bs->file, offset, qiov, bytes, pcache_aio_cb, &acb);

    qemu_coroutine_yield();

    return acb.ret;
}

static coroutine_fn int pcache_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                          uint64_t bytes, QEMUIOVector *qiov,
                                          int flags)
{
    PCacheAIOCB acb = {
        .co = qemu_coroutine_self(),
    };

    bdrv_aio_pwritev(bs->file, offset, qiov, bytes, pcache_aio_cb, &acb);

    qemu_coroutine_yield();

    return acb.ret;
}

static int pcache_file_open(BlockDriverState *bs, QDict *options, int flags,
                            Error **errp)
{
    QemuOpts *opts;
    Error *local_err = NULL;
    int ret = 0;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    assert(bs->file == NULL);
    bs->file = bdrv_open_child(qemu_opt_get(opts, "x-image"), options,
                               "image", bs, &child_format, false, &local_err);
    if (local_err) {
        ret = -EINVAL;
        error_propagate(errp, local_err);
    }
fail:
    qemu_opts_del(opts);
    return ret;
}

static void pcache_close(BlockDriverState *bs)
{
}

static void pcache_parse_filename(const char *filename, QDict *options,
                                  Error **errp)
{
    qdict_put(options, "x-image", qstring_from_str(filename));
}

static int64_t pcache_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}

static bool pcache_recurse_is_first_non_filter(BlockDriverState *bs,
                                               BlockDriverState *candidate)
{
    return bdrv_recurse_is_first_non_filter(bs->file->bs, candidate);
}

static BlockDriver bdrv_pcache = {
    .format_name                        = "pcache",
    .protocol_name                      = "pcache",
    .instance_size                      = 0,

    .bdrv_parse_filename                = pcache_parse_filename,
    .bdrv_file_open                     = pcache_file_open,
    .bdrv_close                         = pcache_close,
    .bdrv_getlength                     = pcache_getlength,

    .bdrv_co_preadv                     = pcache_co_preadv,
    .bdrv_co_pwritev                    = pcache_co_pwritev,

    .is_filter                          = true,
    .bdrv_recurse_is_first_non_filter   = pcache_recurse_is_first_non_filter,
};

static void bdrv_cache_init(void)
{
    bdrv_register(&bdrv_pcache);
}

block_init(bdrv_cache_init);
