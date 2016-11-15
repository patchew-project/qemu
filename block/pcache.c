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
#include "qemu/rbcache.h"

#define PCACHE_OPT_STATS_SIZE "pcache-stats-size"
#define PCACHE_OPT_MAX_AIO_SIZE "pcache-max-aio-size"
#define PCACHE_OPT_CACHE_SIZE "pcache-full-size"
#define PCACHE_OPT_READAHEAD_SIZE "pcache-readahead-size"

static QemuOptsList runtime_opts = {
    .name = "pcache",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "x-image",
            .type = QEMU_OPT_STRING,
            .help = "[internal use only, will be removed]",
        },
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
        {
            .name = PCACHE_OPT_CACHE_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Total cache size",
        },
        {
            .name = PCACHE_OPT_READAHEAD_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Prefetch cache readahead size",
        },
        { /* end of list */ }
    },
};

#define KB_BITS 10
#define MB_BITS 20
#define PCACHE_DEFAULT_STATS_SIZE (3 << MB_BITS)
#define PCACHE_DEFAULT_MAX_AIO_SIZE (64 << KB_BITS)
#define PCACHE_DEFAULT_CACHE_SIZE (4 << MB_BITS)
#define PCACHE_DEFAULT_READAHEAD_SIZE (128 << KB_BITS)

typedef struct BDRVPCacheState {
    RBCache *req_stats;
    RBCache *cache;
    uint64_t max_aio_size;
    uint64_t readahead_size;
} BDRVPCacheState;

typedef struct PCacheNode {
    RBCacheNode common;
    uint8_t *data;
    enum {
        NODE_STATUS_NEW       = 0,
        NODE_STATUS_INFLIGHT  = 1,
        NODE_STATUS_COMPLETED = 2,
        NODE_STATUS_REMOVE    = 3,
        NODE_STATUS_DELETED   = 4, /* only for debugging */
    } status;
    int ref;
} PCacheNode;

typedef struct PCacheAIOCB {
    BlockDriverState *bs;
    Coroutine *co;
    uint64_t offset;
    uint64_t bytes;
    int ret;
} PCacheAIOCB;

typedef struct PCacheAIOCBReadahead {
    BlockDriverState *bs;
    Coroutine *co;
    QEMUIOVector qiov;
    PCacheNode *node;
} PCacheAIOCBReadahead;

static inline void pcache_node_ref(PCacheNode *node)
{
    node->ref++;
}

static void pcache_node_unref(PCacheNode *node)
{
    assert(node->ref > 0);
    if (--node->ref == 0) {
        assert(node->status == NODE_STATUS_REMOVE);
        node->status = NODE_STATUS_DELETED;

        g_free(node->data);
        g_free(node);
    }
}

static void pcache_aio_cb(void *opaque, int ret)
{
    PCacheAIOCB *acb = opaque;

    acb->ret = ret;
    qemu_coroutine_enter(acb->co);
}

static void pcache_aio_readahead_cb(void *opaque, int ret)
{
    PCacheAIOCBReadahead *acb = opaque;
    PCacheNode *node = acb->node;

    assert(node->status == NODE_STATUS_INFLIGHT ||
           node->status == NODE_STATUS_REMOVE);

    if (node->status == NODE_STATUS_INFLIGHT) {
        if (ret == 0) {
            node->status = NODE_STATUS_COMPLETED;
        } else {
            BDRVPCacheState *s = acb->bs->opaque;
            rbcache_remove(s->cache, &node->common);
        }
    }
    pcache_node_unref(node);

    qemu_coroutine_enter(acb->co);
}

static void update_req_stats(RBCache *rbcache, uint64_t offset, uint64_t bytes)
{
    do {
        RBCacheNode *node = rbcache_search_and_insert(rbcache, offset, bytes);
        /* The node was successfully added or already exists */
        if (node->offset <= offset &&
            node->offset + node->bytes >= offset + bytes)
        {
            break;
        }

        /* Request covers the whole node */
        if (offset <= node->offset &&
            offset + bytes >= node->offset + node->bytes)
        {
            rbcache_remove(rbcache, node);
            continue;
        }

        if (offset < node->offset) {
            RBCacheNode *new_node =
                rbcache_node_alloc(rbcache, offset, node->offset - offset);
            if (new_node != rbcache_insert(rbcache, new_node)) {
                /* The presence of the node in this range is impossible */
                abort();
            }
            break;
        }

        bytes = (offset + bytes) - (node->offset + node->bytes);
        offset = node->offset + node->bytes;
    } while (true);
}

static bool check_request_sequence(BDRVPCacheState *s, uint64_t offset)
{
    uint64_t cache_line_size = s->readahead_size;
    uint64_t check_offset;

    if (offset <= cache_line_size) {
        return false;
    }
    check_offset = offset - cache_line_size;

    do {
        RBCacheNode *node = rbcache_search(s->req_stats, check_offset,
                                           offset - check_offset);
        if (node == NULL) {
            return false;
        }
        if (node->offset > check_offset) {
            return false;
        }
        check_offset = node->offset + node->bytes;
    } while (check_offset < offset);

    return true;
}

static void pcache_node_free(RBCacheNode *rbnode, void *opaque)
{
    PCacheNode *node = container_of(rbnode, PCacheNode, common);

    assert(node->status == NODE_STATUS_INFLIGHT ||
           node->status == NODE_STATUS_COMPLETED);

    node->status = NODE_STATUS_REMOVE;
    pcache_node_unref(node);
}

static RBCacheNode *pcache_node_alloc(uint64_t offset, uint64_t bytes,
                                      void *opaque)
{
    PCacheNode *node = g_malloc(sizeof(*node));

    node->data = g_malloc(bytes);
    node->status = NODE_STATUS_NEW;
    node->ref = 1;

    return &node->common;
}

#define PCACHE_STEPS_FORWARD 2

static PCacheNode *get_readahead_node(BlockDriverState *bs, RBCache *rbcache,
                                      uint64_t offset, uint64_t bytes)
{
    uint32_t count = PCACHE_STEPS_FORWARD;

    int64_t total_bytes = bdrv_getlength(bs);
    if (total_bytes < 0) {
        return NULL;
    }

    while (count--) {
        PCacheNode *node;

        if (total_bytes <= offset + bytes) {
            break;
        }

        node = rbcache_search_and_insert(rbcache, offset, bytes);
        if (node->status == NODE_STATUS_NEW) {
            return node;
        }
         /* The range less than the readahead size is not cached to reduce
          * fragmentation of the cache. If the data is already cached, then we
          * just step over it.
          */
        if (offset <= node->common.offset && !count--) {
            break;
        }
        offset = node->common.offset + node->common.bytes;
    };

    return NULL;
}

static void coroutine_fn pcache_co_readahead(void *opaque)
{
    PCacheAIOCB *acb = g_memdup(opaque, sizeof(*acb));
    BlockDriverState *bs = acb->bs;
    BDRVPCacheState *s = bs->opaque;
    uint64_t offset;
    uint64_t bytes;
    PCacheAIOCBReadahead readahead_acb;
    PCacheNode *node;

    if (!check_request_sequence(s, acb->offset)) {
        goto out;
    }

    offset = acb->offset + acb->bytes;
    bytes = s->readahead_size;

    node = get_readahead_node(bs, s->cache, offset, bytes);
    if (node == NULL) {
        goto out;
    }

    readahead_acb = (PCacheAIOCBReadahead) {
        .co = qemu_coroutine_self(),
        .bs = bs,
        .node = node,
    };

    node->status = NODE_STATUS_INFLIGHT;
    qemu_iovec_init(&readahead_acb.qiov, 1);
    qemu_iovec_add(&readahead_acb.qiov, node->data, node->common.bytes);

    pcache_node_ref(node);

    bdrv_aio_preadv(bs->file, node->common.offset, &readahead_acb.qiov,
                    node->common.bytes, pcache_aio_readahead_cb,
                    &readahead_acb);
    qemu_coroutine_yield();
out:
    free(acb);
}

static void pcache_readahead_request(PCacheAIOCB *acb)
{
    Coroutine *co = qemu_coroutine_create(pcache_co_readahead, acb);
    qemu_coroutine_enter(co);
}

static coroutine_fn int pcache_co_preadv(BlockDriverState *bs, uint64_t offset,
                                         uint64_t bytes, QEMUIOVector *qiov,
                                         int flags)
{
    BDRVPCacheState *s = bs->opaque;
    PCacheAIOCB acb = {
        .co = qemu_coroutine_self(),
        .bs = bs,
        .offset = offset,
        .bytes = bytes,
    };

    if (bytes > s->max_aio_size) {
        bdrv_aio_preadv(bs->file, offset, qiov, bytes, pcache_aio_cb, &acb);
        goto out;
    }

    update_req_stats(s->req_stats, offset, bytes);

    bdrv_aio_preadv(bs->file, offset, qiov, bytes, pcache_aio_cb, &acb);

    pcache_readahead_request(&acb);

out:
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

static void pcache_state_init(QemuOpts *opts, BDRVPCacheState *s)
{
    uint64_t stats_size = qemu_opt_get_size(opts, PCACHE_OPT_STATS_SIZE,
                                            PCACHE_DEFAULT_STATS_SIZE);
    uint64_t cache_size = qemu_opt_get_size(opts, PCACHE_OPT_CACHE_SIZE,
                                            PCACHE_DEFAULT_CACHE_SIZE);
    s->req_stats = rbcache_create(NULL, NULL, stats_size, RBCACHE_FIFO, s);

    s->max_aio_size = qemu_opt_get_size(opts, PCACHE_OPT_MAX_AIO_SIZE,
                                        PCACHE_DEFAULT_MAX_AIO_SIZE);
    s->cache = rbcache_create(pcache_node_alloc, pcache_node_free, cache_size,
                              RBCACHE_LRU, s);
    s->readahead_size = qemu_opt_get_size(opts, PCACHE_OPT_READAHEAD_SIZE,
                                          PCACHE_DEFAULT_READAHEAD_SIZE);
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
    rbcache_destroy(s->cache);
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
    .instance_size                      = sizeof(BDRVPCacheState),

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
