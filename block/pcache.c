/*
 * Prefetch cache driver filter
 *
 * Copyright (C) 2015-2016 Parallels IP Holdings GmbH.
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
#include "qemu/rbcache.h"

#define PCACHE_OPT_STATS_SIZE "pcache-stats-size"
#define PCACHE_OPT_MAX_AIO_SIZE "pcache-max-aio-size"

static QemuOptsList runtime_opts = {
    .name = "pcache",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = PCACHE_OPT_STATS_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Total volume of requests for statistics",
        },
        {
            .name = PCACHE_OPT_MAX_AIO_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Maximum size of aio which is handled by pcache",
        },
        { /* end of list */ }
    },
};

#define KB_BITS 10
#define MB_BITS 20
#define PCACHE_DEFAULT_STATS_SIZE (3 << MB_BITS)
#define PCACHE_DEFAULT_MAX_AIO_SIZE (64 << KB_BITS)

typedef struct BDRVPCacheState {
    RBCache *req_stats;
    uint64_t max_aio_size;
} BDRVPCacheState;

static coroutine_fn int pcache_co_preadv(BlockDriverState *bs, uint64_t offset,
                                         uint64_t bytes, QEMUIOVector *qiov,
                                         int flags)
{
    BDRVPCacheState *s = bs->opaque;

    if (s->max_aio_size >= bytes) {
        rbcache_search_and_insert(s->req_stats, offset, bytes);
    }

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static coroutine_fn int pcache_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                          uint64_t bytes, QEMUIOVector *qiov,
                                          int flags)
{
    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}

static void pcache_state_init(QemuOpts *opts, BDRVPCacheState *s)
{
    uint64_t stats_size = qemu_opt_get_size(opts, PCACHE_OPT_STATS_SIZE,
                                            PCACHE_DEFAULT_STATS_SIZE);
    s->req_stats = rbcache_create(NULL, NULL, stats_size, RBCACHE_FIFO, s);

    s->max_aio_size = qemu_opt_get_size(opts, PCACHE_OPT_MAX_AIO_SIZE,
                                        PCACHE_DEFAULT_MAX_AIO_SIZE);
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
    bs->file = bdrv_open_child(NULL, options, "image", bs, &child_format,
                               false, &local_err);
    if (local_err) {
        ret = -EINVAL;
        error_propagate(errp, local_err);
        goto fail;
    }
    pcache_state_init(opts, bs->opaque);
fail:
    qemu_opts_del(opts);
    return ret;
}

static void pcache_close(BlockDriverState *bs)
{
    BDRVPCacheState *s = bs->opaque;

    rbcache_destroy(s->req_stats);
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
    .instance_size                      = sizeof(BDRVPCacheState),

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
