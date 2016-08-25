/*
 * Prefetch cache driver filter
 *
 * Copyright (c) 2016 Pavel Butsykin <pbutsykin@virtuozzo.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/raw-aio.h"
#include "qapi/error.h"
#include "qapi/qmp/qstring.h"
#include "qemu/rbtree.h"

#define PCACHE_DEBUG

#ifdef PCACHE_DEBUG
#define DPRINTF(fmt, ...) \
        printf("%s:%s:%d "fmt, __FILE__, __func__, __LINE__, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

typedef struct RbNodeKey {
    uint64_t    num;
    uint32_t    size;
} RbNodeKey;

typedef struct BlockNode {
    struct RbNode               rb_node;
    union {
        RbNodeKey               key;
        struct {
            uint64_t            sector_num;
            uint32_t            nb_sectors;
        };
    };
    QTAILQ_ENTRY(BlockNode) entry;
} BlockNode;

typedef struct PCNode {
    BlockNode cm;

    uint32_t                 status;
    uint32_t                 ref;
    uint8_t                  *data;
    CoMutex                  lock;
} PCNode;

typedef struct ReqStor {
    struct {
        struct RbRoot root;
        CoMutex       lock;
    } tree;

    struct {
        QTAILQ_HEAD(lru_head, BlockNode) list;
        CoMutex lock;
    } lru;

    uint32_t curr_size;
} ReqStor;

typedef struct BDRVPCacheState {
    BlockDriverState **bs;

    ReqStor pcache;

    uint32_t cfg_cache_size;

#ifdef PCACHE_DEBUG
    uint64_t shrink_cnt_node;
#endif
} BDRVPCacheState;

typedef struct PrefCacheAIOCB {
    BlockAIOCB common;

    BDRVPCacheState *s;
    QEMUIOVector *qiov;
    uint64_t sector_num;
    uint32_t nb_sectors;
    int      aio_type;
    struct {
        QTAILQ_HEAD(req_head, PrefCachePartReq) list;
        CoMutex lock;
    } requests;
    int      ret;
} PrefCacheAIOCB;

typedef struct PrefCachePartReq {
    uint64_t sector_num;
    uint32_t nb_sectors;

    QEMUIOVector qiov;
    PCNode *node;
    PrefCacheAIOCB *acb;
    QTAILQ_ENTRY(PrefCachePartReq) entry;
} PrefCachePartReq;

static const AIOCBInfo pcache_aiocb_info = {
    .aiocb_size = sizeof(PrefCacheAIOCB),
};

#define PCACHE_OPT_CACHE_SIZE "pcache-full-size"

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
            .name = PCACHE_OPT_CACHE_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Total cache size",
        },
        { /* end of list */ }
    },
};

#define KB_BITS 10
#define MB_BITS 20
#define PCACHE_DEFAULT_CACHE_SIZE (4 << MB_BITS)

enum {
    NODE_SUCCESS_STATUS = 0,
    NODE_WAIT_STATUS    = 1,
    NODE_REMOVE_STATUS  = 2,
    NODE_GHOST_STATUS   = 3 /* only for debugging */
};

#define PCNODE(_n) ((PCNode *)(_n))

static inline void pcache_node_unref(PCNode *node)
{
    assert(node->status == NODE_SUCCESS_STATUS ||
           node->status == NODE_REMOVE_STATUS);

    if (atomic_fetch_dec(&node->ref) == 0) {
        assert(node->status == NODE_REMOVE_STATUS);

        node->status = NODE_GHOST_STATUS;
        g_free(node->data);
        g_slice_free1(sizeof(*node), node);
    }
}

static inline PCNode *pcache_node_ref(PCNode *node)
{
    assert(node->status == NODE_SUCCESS_STATUS ||
           node->status == NODE_WAIT_STATUS);
    assert(atomic_read(&node->ref) == 0);/* XXX: only for sequential requests */
    atomic_inc(&node->ref);

    return node;
}

static int pcache_key_cmp(const RbNodeKey *key1, const RbNodeKey *key2)
{
    assert(key1 != NULL);
    assert(key2 != NULL);

    if (key1->num >= key2->num + key2->size) {
        return 1;
    }
    if (key1->num + key1->size <= key2->num) {
        return -1;
    }

    return 0;
}

static BlockNode *pcache_node_prev(BlockNode* node, RbNodeKey *key)
{
    while (node) {
        struct RbNode *prev_rb_node = rb_prev(&node->rb_node);
        BlockNode *prev_node;
        if (prev_rb_node == NULL) {
            break;
        }
        prev_node = container_of(prev_rb_node, BlockNode, rb_node);
        if (prev_node->sector_num + prev_node->nb_sectors <= key->num) {
            break;
        }
        node = prev_node;
    }

    return node;
}

static void *node_insert(struct RbRoot *root, BlockNode *node)
{
    struct RbNode **new = &(root->rb_node), *parent = NULL;

    /* Figure out where to put new node */
    while (*new) {
        BlockNode *this = container_of(*new, BlockNode, rb_node);
        int result = pcache_key_cmp(&node->key, &this->key);
        if (result == 0) {
            return pcache_node_prev(this, &node->key);
        }
        parent = *new;
        new = result < 0 ? &((*new)->rb_left) : &((*new)->rb_right);
    }
    /* Add new node and rebalance tree. */
    rb_link_node(&node->rb_node, parent, new);
    rb_insert_color(&node->rb_node, root);

    return node;
}

static inline PCNode *pcache_node_insert(struct RbRoot *root, PCNode *node)
{
    return pcache_node_ref(node_insert(root, &node->cm));
}

static inline void *pcache_node_alloc(RbNodeKey* key)
{
    PCNode *node = g_slice_alloc(sizeof(*node));

    node->cm.sector_num = key->num;
    node->cm.nb_sectors = key->size;
    node->ref = 0;
    node->status = NODE_WAIT_STATUS;
    qemu_co_mutex_init(&node->lock);
    node->data = g_malloc(node->cm.nb_sectors << BDRV_SECTOR_BITS);

    return node;
}

static void pcache_node_drop(BDRVPCacheState *s, PCNode *node)
{
    uint32_t prev_status = atomic_xchg(&node->status, NODE_REMOVE_STATUS);
    if (prev_status == NODE_REMOVE_STATUS) {
        return;
    }
    assert(prev_status != NODE_GHOST_STATUS);

    atomic_sub(&s->pcache.curr_size, node->cm.nb_sectors);

    qemu_co_mutex_lock(&s->pcache.lru.lock);
    QTAILQ_REMOVE(&s->pcache.lru.list, &node->cm, entry);
    qemu_co_mutex_unlock(&s->pcache.lru.lock);

    qemu_co_mutex_lock(&s->pcache.tree.lock);
    rb_erase(&node->cm.rb_node, &s->pcache.tree.root);
    qemu_co_mutex_unlock(&s->pcache.tree.lock);

    pcache_node_unref(node);
}

static void pcache_try_shrink(BDRVPCacheState *s)
{
    while (s->pcache.curr_size > s->cfg_cache_size) {
        qemu_co_mutex_lock(&s->pcache.lru.lock);
        assert(!QTAILQ_EMPTY(&s->pcache.lru.list));
        PCNode *rmv_node = PCNODE(QTAILQ_LAST(&s->pcache.lru.list, lru_head));
        qemu_co_mutex_unlock(&s->pcache.lru.lock);

        pcache_node_drop(s, rmv_node);
#ifdef PCACHE_DEBUG
        atomic_inc(&s->shrink_cnt_node);
#endif
    }
}

static PrefCachePartReq *pcache_req_get(PrefCacheAIOCB *acb, PCNode *node)
{
    PrefCachePartReq *req = g_slice_alloc(sizeof(*req));

    req->nb_sectors = node->cm.nb_sectors;
    req->sector_num = node->cm.sector_num;
    req->node = node;
    req->acb = acb;

    assert(acb->sector_num <= node->cm.sector_num + node->cm.nb_sectors);

    qemu_iovec_init(&req->qiov, 1);
    qemu_iovec_add(&req->qiov, node->data,
                   node->cm.nb_sectors << BDRV_SECTOR_BITS);
    return req;
}

static inline void push_node_request(PrefCacheAIOCB *acb, PCNode *node)
{
    PrefCachePartReq *req = pcache_req_get(acb, node);

    QTAILQ_INSERT_HEAD(&acb->requests.list, req, entry);
}

static inline void pcache_lru_node_up(BDRVPCacheState *s, PCNode *node)
{
    qemu_co_mutex_lock(&s->pcache.lru.lock);
    QTAILQ_REMOVE(&s->pcache.lru.list, &node->cm, entry);
    QTAILQ_INSERT_HEAD(&s->pcache.lru.list, &node->cm, entry);
    qemu_co_mutex_unlock(&s->pcache.lru.lock);
}

static bool pcache_node_find_and_create(PrefCacheAIOCB *acb, RbNodeKey *key,
                                        PCNode **out_node)
{
    BDRVPCacheState *s = acb->s;
    PCNode *new_node = pcache_node_alloc(key);
    PCNode *found;

    qemu_co_mutex_lock(&s->pcache.tree.lock);
    found = pcache_node_insert(&s->pcache.tree.root, new_node);
    qemu_co_mutex_unlock(&s->pcache.tree.lock);
    if (found != new_node) {
        g_free(new_node->data);
        g_slice_free1(sizeof(*new_node), new_node);
        if (found->status == NODE_SUCCESS_STATUS) {
            pcache_lru_node_up(s, found);
        }
        *out_node = found;
        return false;
    }
    atomic_add(&s->pcache.curr_size, new_node->cm.nb_sectors);

    push_node_request(acb, new_node);

    pcache_try_shrink(s);

    *out_node = new_node;
    return true;
}

static inline void prefetch_init_key(PrefCacheAIOCB *acb, RbNodeKey* key)
{
    key->num = acb->sector_num;
    key->size = acb->nb_sectors;
}

static void pcache_pickup_parts_of_cache(PrefCacheAIOCB *acb, PCNode *node,
                                         uint64_t num, uint32_t size)
{
    uint32_t up_size;

    do {
        if (num < node->cm.sector_num) {
            PCNode *new_node;
            RbNodeKey lc_key = {
                .num = num,
                .size = node->cm.sector_num - num,
            };
            up_size = lc_key.size;

            if (!pcache_node_find_and_create(acb, &lc_key, &new_node)) {
                pcache_node_unref(node);
                node = new_node;
                continue;
            }
            size -= up_size;
            num += up_size;
        }
        /* XXX: node read */
        up_size = MIN(node->cm.sector_num + node->cm.nb_sectors - num, size);

        pcache_node_unref(node);

        size -= up_size;
        num += up_size;
        if (size != 0) {
            RbNodeKey lc_key = {
                .num = num,
                .size = size,
            };
            if (pcache_node_find_and_create(acb, &lc_key, &node)) {
                size -= lc_key.size;
                assert(size == 0);
            }
        }
    } while (size);
}

enum {
    PREFETCH_NEW_NODE  = 0,
    PREFETCH_FULL_UP   = 1,
    PREFETCH_PART_UP   = 2
};

static int32_t pcache_prefetch(PrefCacheAIOCB *acb)
{
    RbNodeKey key;
    PCNode *node = NULL;

    prefetch_init_key(acb, &key);
    if (pcache_node_find_and_create(acb, &key, &node)) {
        return PREFETCH_NEW_NODE;
    }

    /* Node covers the whole request */
    if (node->cm.sector_num <= acb->sector_num &&
        node->cm.sector_num + node->cm.nb_sectors >= acb->sector_num +
                                                     acb->nb_sectors)
    {
        /* XXX: node read */
        pcache_node_unref(node);
        return PREFETCH_FULL_UP;
    }
    pcache_pickup_parts_of_cache(acb, node, key.num, key.size);

    return PREFETCH_PART_UP;
}

static void pcache_node_submit(PrefCachePartReq *req)
{
    PCNode *node = req->node;
    BDRVPCacheState *s = req->acb->s;

    assert(node != NULL);
    assert(atomic_read(&node->ref) != 0);
    assert(node->data != NULL);

    qemu_co_mutex_lock(&node->lock);
    if (node->status == NODE_WAIT_STATUS) {
        qemu_co_mutex_lock(&s->pcache.lru.lock);
        QTAILQ_INSERT_HEAD(&s->pcache.lru.list, &node->cm, entry);
        qemu_co_mutex_unlock(&s->pcache.lru.lock);

        node->status = NODE_SUCCESS_STATUS;
    }
    qemu_co_mutex_unlock(&node->lock);
}

static void pcache_merge_requests(PrefCacheAIOCB *acb)
{
    PrefCachePartReq *req, *next;

    qemu_co_mutex_lock(&acb->requests.lock);
    QTAILQ_FOREACH_SAFE(req, &acb->requests.list, entry, next) {
        QTAILQ_REMOVE(&acb->requests.list, req, entry);

        assert(req != NULL);
        assert(req->node->status == NODE_WAIT_STATUS);

        pcache_node_submit(req);

        /* XXX: pcache read */

        pcache_node_unref(req->node);

        g_slice_free1(sizeof(*req), req);
    }
    qemu_co_mutex_unlock(&acb->requests.lock);
}

static void pcache_aio_cb(void *opaque, int ret)
{
    PrefCacheAIOCB *acb = opaque;

    if (acb->aio_type & QEMU_AIO_READ) {
        pcache_merge_requests(acb);
    }

    acb->common.cb(acb->common.opaque, ret);

    qemu_aio_unref(acb);
}

static PrefCacheAIOCB *pcache_aio_get(BlockDriverState *bs, int64_t sector_num,
                                      QEMUIOVector *qiov, int nb_sectors,
                                      BlockCompletionFunc *cb, void *opaque,
                                      int type)
{
    PrefCacheAIOCB *acb = qemu_aio_get(&pcache_aiocb_info, bs, cb, opaque);

    acb->s = bs->opaque;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;
    acb->qiov = qiov;
    acb->aio_type = type;
    acb->ret = 0;

    QTAILQ_INIT(&acb->requests.list);
    qemu_co_mutex_init(&acb->requests.lock);

    return acb;
}

static BlockAIOCB *pcache_aio_readv(BlockDriverState *bs,
                                    int64_t sector_num,
                                    QEMUIOVector *qiov,
                                    int nb_sectors,
                                    BlockCompletionFunc *cb,
                                    void *opaque)
{
    PrefCacheAIOCB *acb = pcache_aio_get(bs, sector_num, qiov, nb_sectors, cb,
                                         opaque, QEMU_AIO_READ);
    pcache_prefetch(acb);

    bdrv_aio_readv(bs->file, sector_num, qiov, nb_sectors,
                   pcache_aio_cb, acb);
    return &acb->common;
}

static BlockAIOCB *pcache_aio_writev(BlockDriverState *bs,
                                     int64_t sector_num,
                                     QEMUIOVector *qiov,
                                     int nb_sectors,
                                     BlockCompletionFunc *cb,
                                     void *opaque)
{
    PrefCacheAIOCB *acb = pcache_aio_get(bs, sector_num, qiov, nb_sectors, cb,
                                         opaque, QEMU_AIO_WRITE);

    bdrv_aio_writev(bs->file, sector_num, qiov, nb_sectors,
                    pcache_aio_cb, acb);
    return &acb->common;
}

static void pcache_state_init(QemuOpts *opts, BDRVPCacheState *s)
{
    uint64_t cache_size = qemu_opt_get_size(opts, PCACHE_OPT_CACHE_SIZE,
                                            PCACHE_DEFAULT_CACHE_SIZE);
    DPRINTF("pcache configure:\n");
    DPRINTF("pcache-full-size = %jd\n", cache_size);

    s->pcache.tree.root = RB_ROOT;
    qemu_co_mutex_init(&s->pcache.tree.lock);
    QTAILQ_INIT(&s->pcache.lru.list);
    qemu_co_mutex_init(&s->pcache.lru.lock);
    s->pcache.curr_size = 0;

    s->cfg_cache_size = cache_size >> BDRV_SECTOR_BITS;
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

static void pcache_node_check_and_free(BDRVPCacheState *s, PCNode *node)
{
    assert(node->status == NODE_SUCCESS_STATUS);
    assert(node->ref == 0);

    node->status = NODE_REMOVE_STATUS;
    rb_erase(&node->cm.rb_node, &s->pcache.tree.root);
    g_free(node->data);
    g_slice_free1(sizeof(*node), node);
}

static void pcache_close(BlockDriverState *bs)
{
    uint32_t cnt = 0;
    BDRVPCacheState *s = bs->opaque;
    BlockNode *node, *next;
    QTAILQ_FOREACH_SAFE(node, &s->pcache.lru.list, entry, next) {
        QTAILQ_REMOVE(&s->pcache.lru.list, node, entry);
        pcache_node_check_and_free(s, PCNODE(node));
        cnt++;
    }
    DPRINTF("used %d nodes\n", cnt);
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

    .bdrv_aio_readv                     = pcache_aio_readv,
    .bdrv_aio_writev                    = pcache_aio_writev,

    .is_filter                          = true,
    .bdrv_recurse_is_first_non_filter   = pcache_recurse_is_first_non_filter,
};

static void bdrv_cache_init(void)
{
    bdrv_register(&bdrv_pcache);
}

block_init(bdrv_cache_init);
