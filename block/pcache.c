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
#include "qemu/error-report.h"
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
    QLIST_HEAD(, PCacheNode) remove_node_list;
} BDRVPCacheState;

typedef struct ReqLinkEntry {
    QTAILQ_ENTRY(ReqLinkEntry) entry;
    Coroutine *co;
    int ret;
} ReqLinkEntry;

typedef struct PCacheNode {
    RBCacheNode common;
    uint8_t *data;
    QTAILQ_HEAD(, ReqLinkEntry) wait_list;
    enum {
        NODE_STATUS_NEW       = 0x01,
        NODE_STATUS_INFLIGHT  = 0x02,
        NODE_STATUS_COMPLETED = 0x04,
        NODE_STATUS_REMOVE    = 0x08,
        NODE_STATUS_DELETED   = 0x10, /* only for debugging */
    } status;
    uint64_t rdcnt;
    QLIST_ENTRY(PCacheNode) entry;
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

        QLIST_REMOVE(node, entry);
        g_free(node->data);
        g_free(node);
    }
}

static uint64_t ranges_overlap_size(uint64_t offset1, uint64_t size1,
                                    uint64_t offset2, uint32_t size2)
{
    return MIN(offset1 + size1, offset2 + size2) - MAX(offset1, offset2);
}

static void read_cache_data_direct(BlockDriverState *bs, PCacheNode *node,
                                   uint64_t offset, uint64_t bytes,
                                   QEMUIOVector *qiov)
{
    BDRVPCacheState *s = bs->opaque;
    uint64_t qiov_offs = 0, node_offs = 0;
    uint64_t size;
    uint64_t copy;

    assert(node->status & NODE_STATUS_COMPLETED);

    if (offset < node->common.offset) {
        qiov_offs = node->common.offset - offset;
    } else {
        node_offs = offset - node->common.offset;
    }
    size = ranges_overlap_size(offset, bytes, node->common.offset,
                               node->common.bytes);
    copy = qemu_iovec_from_buf(qiov, qiov_offs, node->data + node_offs, size);
    node->rdcnt += size;
    if (node->rdcnt >= node->common.bytes &&
        !(node->status & NODE_STATUS_REMOVE))
    {
        rbcache_remove(s->cache, &node->common);
    }
    assert(copy == size);
}

static int read_cache_data(BlockDriverState *bs, PCacheNode *node,
                           uint64_t offset, uint64_t bytes, QEMUIOVector *qiov)
{
    if (node->status & NODE_STATUS_INFLIGHT) {
        ReqLinkEntry rlink = {
            .co = qemu_coroutine_self(),
        };

        QTAILQ_INSERT_HEAD(&node->wait_list, &rlink, entry);

        qemu_coroutine_yield();

        if (rlink.ret < 0) {
            return rlink.ret;
        }
    }
    read_cache_data_direct(bs, node, offset, bytes, qiov);

    return 0;
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
    BDRVPCacheState *s = opaque;
    PCacheNode *node = container_of(rbnode, PCacheNode, common);

    assert(node->status == NODE_STATUS_INFLIGHT ||
           node->status == NODE_STATUS_COMPLETED);

    QLIST_INSERT_HEAD(&s->remove_node_list, node, entry);

    node->status |= NODE_STATUS_REMOVE;
    pcache_node_unref(node);
}

static RBCacheNode *pcache_node_alloc(uint64_t offset, uint64_t bytes,
                                      void *opaque)
{
    PCacheNode *node = g_malloc(sizeof(*node));

    node->data = g_malloc(bytes);
    node->status = NODE_STATUS_NEW;
    node->rdcnt = 0;
    node->ref = 1;
    QTAILQ_INIT(&node->wait_list);

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
    ReqLinkEntry *rlink, *next;
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

    QTAILQ_FOREACH_SAFE(rlink, &node->wait_list, entry, next) {
        QTAILQ_REMOVE(&node->wait_list, rlink, entry);
        rlink->ret = ret;
        qemu_coroutine_enter(rlink->co);
    }

    pcache_node_unref(node);
}

static void pcache_readahead_entry(void *opaque)
{
    PCacheReadaheadCo *readahead_co = opaque;

    pcache_co_readahead(readahead_co->bs, readahead_co->offset,
                        readahead_co->bytes);
}

/*
 * Provided that the request size is less or equal than the readahead size,
 * a partial cache hit can occur in the following three cases:
 * 1. The request covers the bottom part of the node.
 * 2. The request covers the upper part of the node.
 * 3. The request is between two nodes and partially covers both of them.
 *
 * Therefore, the request can be divided into no more than 3 parts.
 */
#define PCACHE_MAX_FRAGMENT_NUM 3

typedef struct PartReqEntry {
    ReqLinkEntry rlink;
    PCacheNode *node;
} PartReqEntry;

typedef struct PartReqDesc {
    Coroutine *co;
    PartReqEntry parts[PCACHE_MAX_FRAGMENT_NUM];
    uint32_t cnt;
    uint32_t completed;
} PartReqDesc;

typedef struct PCachePartReqCo {
    BlockDriverState *bs;
    uint64_t offset;
    uint64_t bytes;
    PartReqDesc *desc;
} PCachePartReqCo;

static void coroutine_fn pcache_co_part_req(BlockDriverState *bs,
                                            uint64_t offset, uint64_t bytes,
                                            PartReqDesc *req)
{
    BDRVPCacheState *s = bs->opaque;
    QEMUIOVector qiov;
    PartReqEntry *part = &req->parts[req->cnt];
    PCacheNode *node = container_of(rbcache_node_alloc(s->cache, offset, bytes),
                                    PCacheNode, common);
    node->status = NODE_STATUS_INFLIGHT;
    qemu_iovec_init(&qiov, 1);
    qemu_iovec_add(&qiov, node->data, node->common.bytes);

    part->node = node;
    assert(++req->cnt <= PCACHE_MAX_FRAGMENT_NUM);

    part->rlink.ret = bdrv_co_preadv(bs->file, offset, bytes, &qiov, 0);

    node->status = NODE_STATUS_COMPLETED | NODE_STATUS_REMOVE;
    QLIST_INSERT_HEAD(&s->remove_node_list, node, entry);

    if (!qemu_coroutine_entered(req->co)) {
        qemu_coroutine_enter(req->co);
    } else {
        req->completed++;
    }
}

static void pcache_part_req_entry(void *opaque)
{
    PCachePartReqCo *req_co = opaque;

    pcache_co_part_req(req_co->bs, req_co->offset, req_co->bytes, req_co->desc);
}

static int pickup_parts_of_cache(BlockDriverState *bs, PCacheNode *node,
                                 uint64_t offset, uint64_t bytes,
                                 QEMUIOVector *qiov)
{
    BDRVPCacheState *s = bs->opaque;
    PartReqDesc req = {
        .co = qemu_coroutine_self(),
    };
    PCachePartReqCo req_co = {
        .bs = bs,
        .desc = &req
    };
    uint64_t chunk_offset = offset, chunk_bytes = bytes;
    uint64_t up_size;
    int ret = 0;

    do {
        pcache_node_ref(node);

        if (chunk_offset < node->common.offset) {
            Coroutine *co;

            req_co.offset = chunk_offset;
            req_co.bytes = up_size = node->common.offset - chunk_offset;

            co = qemu_coroutine_create(pcache_part_req_entry, &req_co);
            qemu_coroutine_enter(co);

            chunk_offset += up_size;
            chunk_bytes -= up_size;
        }

        req.parts[req.cnt].node = node;
        if (node->status & NODE_STATUS_INFLIGHT) {
            req.parts[req.cnt].rlink.co = qemu_coroutine_self();
            QTAILQ_INSERT_HEAD(&node->wait_list,
                               &req.parts[req.cnt].rlink, entry);
        } else {
            req.completed++;
        }
        assert(++req.cnt <= PCACHE_MAX_FRAGMENT_NUM);

        up_size = MIN(node->common.offset + node->common.bytes - chunk_offset,
                      chunk_bytes);
        chunk_bytes -= up_size;
        chunk_offset += up_size;

        if (chunk_bytes != 0) {
            node = rbcache_search(s->cache, chunk_offset, chunk_bytes);
            if (node == NULL) {
                Coroutine *co;

                req_co.offset = chunk_offset;
                req_co.bytes = chunk_bytes;

                co = qemu_coroutine_create(pcache_part_req_entry, &req_co);
                qemu_coroutine_enter(co);
                chunk_bytes = 0;
            }
        }
    } while (chunk_bytes != 0);

    while (req.completed < req.cnt) {
        qemu_coroutine_yield();
        req.completed++;
    }

    while (req.cnt--) {
        PartReqEntry *part = &req.parts[req.cnt];
        if (ret == 0) {
            if (part->rlink.ret == 0) {
                read_cache_data_direct(bs, part->node, offset, bytes, qiov);
            } else {
                ret = part->rlink.ret;
            }
        }
        pcache_node_unref(part->node);
    }

    return ret;
}

enum {
    CACHE_MISS,
    CACHE_HIT,
};

static int pcache_lookup_data(BlockDriverState *bs, uint64_t offset,
                              uint64_t bytes, QEMUIOVector *qiov)
{
    BDRVPCacheState *s = bs->opaque;
    int ret;

    PCacheNode *node = rbcache_search(s->cache, offset, bytes);
    if (node == NULL) {
        return CACHE_MISS;
    }

    /* Node covers the whole request */
    if (node->common.offset <= offset &&
        node->common.offset + node->common.bytes >= offset + bytes)
    {
        ret = read_cache_data(bs, node, offset, bytes, qiov);

    } else {
        ret = pickup_parts_of_cache(bs, node, offset, bytes, qiov);
    }

    if (ret < 0) {
        return ret;
    }
    return CACHE_HIT;
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
    if (status != CACHE_MISS) {
        return status < 0 ? status : 0;
    }

skip_large_request:
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static void write_cache_data(PCacheNode *node, uint64_t offset, uint64_t bytes,
                             QEMUIOVector *qiov)
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
    copy = qemu_iovec_to_buf(qiov, qiov_offs, node->data + node_offs, size);
    assert(copy == size);
}

static void pcache_write_through(BlockDriverState *bs, uint64_t offset,
                                 uint64_t bytes, QEMUIOVector *qiov)
{
    BDRVPCacheState *s = bs->opaque;
    PCacheNode *node, *next;
    uint64_t chunk_offset = offset, chunk_bytes = bytes;
    uint64_t end_offs = offset + bytes;

    do {
        node = rbcache_search(s->cache, chunk_offset, chunk_bytes);
        if (node == NULL) {
            break;
        }
        assert(node->status == NODE_STATUS_COMPLETED ||
               node->status == NODE_STATUS_INFLIGHT);

        chunk_offset = node->common.offset + node->common.bytes;
        chunk_bytes = end_offs - chunk_offset;

        if (node->status & NODE_STATUS_COMPLETED) {
            write_cache_data(node, offset, bytes, qiov);
        }
    } while (end_offs > chunk_offset);

    QLIST_FOREACH_SAFE(node, &s->remove_node_list, entry, next) {
        if (node->status & NODE_STATUS_INFLIGHT) {
            continue;
        }
        if (offset >= node->common.offset + node->common.bytes ||
            offset + bytes <= node->common.offset)
        {
            continue;
        }
        write_cache_data(node, offset, bytes, qiov);
    }
}

static coroutine_fn int pcache_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                          uint64_t bytes, QEMUIOVector *qiov,
                                          int flags)
{
    int ret = bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
    if (ret < 0) {
        return ret;
    }
    pcache_write_through(bs, offset, bytes, qiov);

    return ret;
}

static int pcache_state_init(QemuOpts *opts, BDRVPCacheState *s)
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
    QLIST_INIT(&s->remove_node_list);

    if (s->readahead_size < s->max_aio_size) {
        error_report("Readahead size can't be less than maximum request size"
                     "that can be handled by pcache");
        return -ENOTSUP;
    }
    return 0;
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
    ret = pcache_state_init(opts, bs->opaque);
fail:
    qemu_opts_del(opts);
    return ret;
}

static void pcache_close(BlockDriverState *bs)
{
    BDRVPCacheState *s = bs->opaque;

    rbcache_destroy(s->req_stats);
    rbcache_destroy(s->cache);

    assert(QLIST_EMPTY(&s->remove_node_list));
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
