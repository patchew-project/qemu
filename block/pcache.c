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
#define PCACHE_OPT_CACHE_SIZE "pcache-full-size"
#define PCACHE_OPT_READAHEAD_SIZE "pcache-readahead-size"

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
        NODE_STATUS_NEW       = 0x01,
        NODE_STATUS_INFLIGHT  = 0x02,
        NODE_STATUS_COMPLETED = 0x04,
        NODE_STATUS_REMOVE    = 0x08,
        NODE_STATUS_DELETED   = 0x10, /* only for debugging */
    } status;
    int ref;
} PCacheNode;

static inline void pcache_node_ref(PCacheNode *node)
{
    node->ref++;
}

static void pcache_node_unref(PCacheNode *node)
{
    assert(node->ref > 0);
    if (--node->ref == 0) {
        assert(node->status & NODE_STATUS_REMOVE);
        node->status |= NODE_STATUS_DELETED;

        g_free(node->data);
        g_free(node);
    }
}

static uint64_t ranges_overlap_size(uint64_t offset1, uint64_t size1,
                                    uint64_t offset2, uint32_t size2)
{
    return MIN(offset1 + size1, offset2 + size2) - MAX(offset1, offset2);
}

static void read_cache_data_direct(PCacheNode *node, uint64_t offset,
                                   uint64_t bytes, QEMUIOVector *qiov)
{
    uint64_t qiov_offs = 0, node_offs = 0;
    uint64_t size;
    uint64_t copy;

    if (offset < node->common.offset) {
        qiov_offs = node->common.offset - offset;
    } else {
        node_offs = offset - node->common.offset;
    }
    size = ranges_overlap_size(offset, bytes, node->common.offset,
                               node->common.bytes);
    copy = qemu_iovec_from_buf(qiov, qiov_offs, node->data + node_offs, size);
    assert(copy == size);
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

    node->status |= NODE_STATUS_REMOVE;
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

static bool check_allocated_clusters(BlockDriverState *bs, uint64_t offset,
                                     uint64_t bytes)
{
    int64_t sector_num = offset >> BDRV_SECTOR_BITS;
    int32_t nb_sectors = bytes >> BDRV_SECTOR_BITS;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);

    do {
        int num, ret = bdrv_is_allocated(bs, sector_num, nb_sectors, &num);
        if (ret <= 0) {
            return false;
        }
        sector_num += num;
        nb_sectors -= num;

    } while (nb_sectors);

    return true;
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

        if (!check_allocated_clusters(bs, offset, bytes)) {
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

typedef struct PCacheReadaheadCo {
    BlockDriverState *bs;
    int64_t offset;
    uint64_t bytes;
} PCacheReadaheadCo;

static void coroutine_fn pcache_co_readahead(BlockDriverState *bs,
                                             uint64_t offset, uint64_t bytes)
{
    BDRVPCacheState *s = bs->opaque;
    QEMUIOVector qiov;
    PCacheNode *node;
    uint64_t readahead_offset;
    uint64_t readahead_bytes;
    int ret;

    if (!check_request_sequence(s, offset)) {
        return;
    }

    readahead_offset = offset + bytes;
    readahead_bytes = s->readahead_size;

    node = get_readahead_node(bs, s->cache, readahead_offset, readahead_bytes);
    if (node == NULL) {
        return;
    }
    node->status = NODE_STATUS_INFLIGHT;
    qemu_iovec_init(&qiov, 1);
    qemu_iovec_add(&qiov, node->data, node->common.bytes);
    pcache_node_ref(node);

    ret = bdrv_co_preadv(bs->file, node->common.offset,
                         node->common.bytes, &qiov, 0);
    assert(node->status & NODE_STATUS_INFLIGHT);
    node->status &= ~NODE_STATUS_INFLIGHT;
    node->status |= NODE_STATUS_COMPLETED;

    if (ret < 0) {
        rbcache_remove(s->cache, &node->common);
    }
    pcache_node_unref(node);
}

static void pcache_readahead_entry(void *opaque)
{
    PCacheReadaheadCo *readahead_co = opaque;

    pcache_co_readahead(readahead_co->bs, readahead_co->offset,
                        readahead_co->bytes);
}

enum {
    CACHE_MISS,
    CACHE_HIT,
};

static int pcache_lookup_data(BlockDriverState *bs, uint64_t offset,
                              uint64_t bytes, QEMUIOVector *qiov)
{
    BDRVPCacheState *s = bs->opaque;

    PCacheNode *node = rbcache_search(s->cache, offset, bytes);
    if (node == NULL || node->status & NODE_STATUS_INFLIGHT) {
        return CACHE_MISS;
    }

    /* Node covers the whole request */
    if (node->common.offset <= offset &&
        node->common.offset + node->common.bytes >= offset + bytes)
    {
        read_cache_data_direct(node, offset, bytes, qiov);
        return CACHE_HIT;
    }

    return CACHE_MISS;
}

static coroutine_fn int pcache_co_preadv(BlockDriverState *bs, uint64_t offset,
                                         uint64_t bytes, QEMUIOVector *qiov,
                                         int flags)
{
    BDRVPCacheState *s = bs->opaque;
    PCacheReadaheadCo readahead_co;
    Coroutine *co;
    int status;

    if (bytes > s->max_aio_size) {
        goto skip_large_request;
    }

    update_req_stats(s->req_stats, offset, bytes);

    readahead_co = (PCacheReadaheadCo) {
        .bs = bs,
        .offset = offset,
        .bytes = bytes,
    };
    co = qemu_coroutine_create(pcache_readahead_entry, &readahead_co);
    qemu_coroutine_enter(co);

    status = pcache_lookup_data(bs, offset, bytes, qiov);
    if (status == CACHE_HIT) {
        return 0;
    }

skip_large_request:
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static void pcache_invalidation(BlockDriverState *bs, uint64_t offset,
                                uint64_t bytes)
{
    BDRVPCacheState *s = bs->opaque;
    uint64_t chunk_offset = offset, chunk_bytes = bytes;
    uint64_t end_offs = offset + bytes;

    do {
        PCacheNode *node = rbcache_search(s->cache, chunk_offset, chunk_bytes);
        if (node == NULL) {
            break;
        }
        assert(node->status == NODE_STATUS_COMPLETED ||
               node->status == NODE_STATUS_INFLIGHT);

        chunk_offset = node->common.offset + node->common.bytes;
        chunk_bytes = end_offs - chunk_offset;

        if (node->status & NODE_STATUS_COMPLETED) {
            rbcache_remove(s->cache, &node->common);
        }
    } while (end_offs > chunk_offset);
}

static coroutine_fn int pcache_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                          uint64_t bytes, QEMUIOVector *qiov,
                                          int flags)
{
    int ret = bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
    if (ret < 0) {
        return ret;
    }
    pcache_invalidation(bs, offset, bytes);

    return ret;
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
    rbcache_destroy(s->cache);
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
