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

#define NODE_PRINT(_node) \
    printf("node:\n"      \
           "num: %jd size: %d\n"   \
           "ref: %d\nstatus: %d\n" \
           "node_wait_cnt: %d\n"   \
           "data: %p\nlock %u\n",  \
           (_node)->cm.sector_num, (_node)->cm.nb_sectors,    \
           (_node)->ref, (_node)->status, (_node)->wait.cnt,  \
           (_node)->data, (_node)->lock.locked)

#define NODE_ASSERT(_assert, _node) \
    do {                            \
        if (!(_assert)) {           \
            NODE_PRINT(_node);      \
            assert(_assert);        \
        }                           \
    } while (0)

typedef struct RbNodeKey {
    uint64_t    num;
    uint32_t    size;
} RbNodeKey;

typedef struct ACBEntryLink {
    QTAILQ_ENTRY(ACBEntryLink) entry;
    struct PrefCacheAIOCB     *acb;
} ACBEntryLink;

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

    struct {
        QTAILQ_HEAD(acb_head, ACBEntryLink) list;
        uint32_t cnt;
    } wait;
    uint32_t                 status;
    uint32_t                 ref;
    uint8_t                  *data;
    uint32_t                 rdcnt;
    CoMutex                  lock;
} PCNode;

typedef struct LRNode {
    BlockNode cm;
} LRNode;

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
    ReqStor lreq;

    struct {
        uint32_t cache_size;
        uint32_t readahead_size;
        uint32_t max_aio_size;
        uint32_t lreq_pool_size;
    } cfg;

#ifdef PCACHE_DEBUG
    uint64_t shrink_cnt_node;
    QTAILQ_HEAD(death_node_head, BlockNode) death_node_list;
    CoMutex                                 death_node_lock;
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
        uint32_t cnt;
    } requests;
    uint32_t ref;
    QEMUBH   *bh;
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
#define PCACHE_OPT_READAHEAD_SIZE "pcache-readahead-size"
#define PCACHE_OPT_MAX_AIO_SIZE "pcache-max-aio-size"

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
        {
            .name = PCACHE_OPT_READAHEAD_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Prefetch cache readahead size",
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
#define PCACHE_DEFAULT_CACHE_SIZE (4 << MB_BITS)
#define PCACHE_DEFAULT_READAHEAD_SIZE (128 << KB_BITS)
#define PCACHE_DEFAULT_POOL_STAT_SIZE (1 << MB_BITS)
#define PCACHE_DEFAULT_MAX_AIO_SIZE (32 << KB_BITS)

#define PCACHE_WRITE_THROUGH_NODE TRUE

enum {
    NODE_SUCCESS_STATUS = 0,
    NODE_WAIT_STATUS    = 1,
    NODE_REMOVE_STATUS  = 2,
    NODE_GHOST_STATUS   = 3 /* only for debugging */
};

enum {
    PCACHE_AIO_READ      = 1,
    PCACHE_AIO_WRITE     = 2,
    PCACHE_AIO_READAHEAD = 4
};

#define PCNODE(_n) ((PCNode *)(_n))
#define LRNODE(_n) ((LRNode *)(_n))

static inline void pcache_node_unref(BDRVPCacheState *s, PCNode *node)
{
    if (atomic_fetch_dec(&node->ref) == 0) {
        NODE_ASSERT(node->status == NODE_REMOVE_STATUS, node);

        node->status = NODE_GHOST_STATUS;

#ifdef PCACHE_DEBUG
        qemu_co_mutex_lock(&s->death_node_lock);
        QTAILQ_REMOVE(&s->death_node_list, &node->cm, entry);
        qemu_co_mutex_unlock(&s->death_node_lock);
#endif
        g_free(node->data);
        g_slice_free1(sizeof(*node), node);
    }
}

static inline PCNode *pcache_node_ref(PCNode *node)
{
    NODE_ASSERT(node->status == NODE_SUCCESS_STATUS ||
                node->status == NODE_WAIT_STATUS, node);
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

static void *node_search(struct RbRoot *root, RbNodeKey *key)
{
    struct RbNode *rb_node = root->rb_node;

    while (rb_node) {
        BlockNode *node = container_of(rb_node, BlockNode, rb_node);
        int32_t result = pcache_key_cmp(key, &node->key);
        if (result == 0) {
            return pcache_node_prev(node, key);
        }
        rb_node = result < 0 ? rb_node->rb_left : rb_node->rb_right;
    }
    return NULL;
}

static PCNode *pcache_node_search(struct RbRoot *root, RbNodeKey *key)
{
    PCNode *node = node_search(root, key);
    return node == NULL ? NULL : pcache_node_ref(node);
}

static inline LRNode *lreq_node_search(struct RbRoot *root, RbNodeKey *key)
{
    return node_search(root, key);
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

static inline LRNode *lreq_node_insert(struct RbRoot *root, LRNode *node)
{
    return node_insert(root, &node->cm);
}

static inline void *pcache_node_alloc(RbNodeKey* key)
{
    PCNode *node = g_slice_alloc(sizeof(*node));

    node->cm.sector_num = key->num;
    node->cm.nb_sectors = key->size;
    node->ref = 0;
    node->status = NODE_WAIT_STATUS;
    node->rdcnt = 0;
    qemu_co_mutex_init(&node->lock);
    node->data = g_malloc(node->cm.nb_sectors << BDRV_SECTOR_BITS);
    node->wait.cnt = 0;
    QTAILQ_INIT(&node->wait.list);

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

#ifdef PCACHE_DEBUG
    qemu_co_mutex_lock(&s->death_node_lock);
    QTAILQ_INSERT_HEAD(&s->death_node_list, &node->cm, entry);
    qemu_co_mutex_unlock(&s->death_node_lock);
#endif

    qemu_co_mutex_lock(&s->pcache.tree.lock);
    rb_erase(&node->cm.rb_node, &s->pcache.tree.root);
    qemu_co_mutex_unlock(&s->pcache.tree.lock);

    pcache_node_unref(s, node);
}

static inline PCNode *pcache_get_most_unused_node(BDRVPCacheState *s)
{
    PCNode *node;
    assert(!QTAILQ_EMPTY(&s->pcache.lru.list));

    qemu_co_mutex_lock(&s->pcache.lru.lock);
    node = PCNODE(QTAILQ_LAST(&s->pcache.lru.list, lru_head));
    pcache_node_ref(node);
    qemu_co_mutex_unlock(&s->pcache.lru.lock);

    return node;
}

static void pcache_try_shrink(BDRVPCacheState *s)
{
    while (s->pcache.curr_size > s->cfg.cache_size) {
        PCNode *rmv_node;
                /* it can happen if all nodes are waiting */
        if (QTAILQ_EMPTY(&s->pcache.lru.list)) {
            DPRINTF("lru list is empty, but curr_size: %d\n",
                    s->pcache.curr_size);
            break;
        }
        rmv_node = pcache_get_most_unused_node(s);

        pcache_node_drop(s, rmv_node);
        pcache_node_unref(s, rmv_node);
#ifdef PCACHE_DEBUG
        atomic_inc(&s->shrink_cnt_node);
#endif
    }
}

static void lreq_try_shrink(BDRVPCacheState *s)
{
    while (s->lreq.curr_size > s->cfg.lreq_pool_size) {
        LRNode *rmv_node;
        assert(!QTAILQ_EMPTY(&s->lreq.lru.list));

        qemu_co_mutex_lock(&s->lreq.lru.lock);
        rmv_node = LRNODE(QTAILQ_LAST(&s->lreq.lru.list, lru_head));
        qemu_co_mutex_unlock(&s->lreq.lru.lock);

        atomic_sub(&s->lreq.curr_size, rmv_node->cm.nb_sectors);

        qemu_co_mutex_lock(&s->lreq.lru.lock);
        QTAILQ_REMOVE(&s->lreq.lru.list, &rmv_node->cm, entry);
        qemu_co_mutex_unlock(&s->lreq.lru.lock);

        qemu_co_mutex_lock(&s->lreq.tree.lock);
        rb_erase(&rmv_node->cm.rb_node, &s->lreq.tree.root);
        qemu_co_mutex_unlock(&s->lreq.tree.lock);
        g_slice_free1(sizeof(*rmv_node), rmv_node);
    }
}

static PrefCachePartReq *pcache_req_get(PrefCacheAIOCB *acb, PCNode *node)
{
    PrefCachePartReq *req = g_slice_alloc(sizeof(*req));

    req->nb_sectors = node->cm.nb_sectors;
    req->sector_num = node->cm.sector_num;
    req->node = node;
    req->acb = acb;

    NODE_ASSERT(acb->sector_num <= node->cm.sector_num + node->cm.nb_sectors,
                node);
    qemu_iovec_init(&req->qiov, 1);
    qemu_iovec_add(&req->qiov, node->data,
                   node->cm.nb_sectors << BDRV_SECTOR_BITS);
    return req;
}

static inline void push_node_request(PrefCacheAIOCB *acb, PCNode *node)
{
    PrefCachePartReq *req = pcache_req_get(acb, node);

    acb->requests.cnt++;

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

static inline PCNode *pcache_node_add(PrefCacheAIOCB *acb, RbNodeKey *key)
{
    PCNode *node = NULL;
    if (!pcache_node_find_and_create(acb, key, &node)) {
        pcache_node_unref(acb->s, node);
        return NULL;
    }
    return node;
}

static LRNode *lreq_node_add(PrefCacheAIOCB *acb, RbNodeKey *key)
{
    BDRVPCacheState *s = acb->s;
    LRNode *new_node = g_slice_alloc(sizeof(*new_node));
    LRNode *found;

    new_node->cm.sector_num = key->num;
    new_node->cm.nb_sectors = key->size;

    qemu_co_mutex_lock(&s->lreq.tree.lock);
    found = lreq_node_insert(&s->lreq.tree.root, new_node);
    qemu_co_mutex_unlock(&s->lreq.tree.lock);
    if (found != new_node) {
        g_slice_free1(sizeof(*new_node), new_node);
        return NULL;
    }

    atomic_add(&s->lreq.curr_size, new_node->cm.nb_sectors);

    lreq_try_shrink(s);

    qemu_co_mutex_lock(&s->lreq.lru.lock);
    QTAILQ_INSERT_HEAD(&s->lreq.lru.list, &new_node->cm, entry);
    qemu_co_mutex_unlock(&s->lreq.lru.lock);

    return new_node;
}

static uint64_t ranges_overlap_size(uint64_t node1, uint32_t size1,
                                    uint64_t node2, uint32_t size2)
{
    return MIN(node1 + size1, node2 + size2) - MAX(node1, node2);
}

enum {
    NODE_READ_BUF  = 1,
    NODE_WRITE_BUF = 2
};

static void pcache_node_rw_buf(PrefCacheAIOCB *acb, PCNode* node, uint32_t type)
{
    uint64_t qiov_offs = 0, node_offs = 0;
    uint32_t size;
    uint32_t copy;

    if (acb->sector_num < node->cm.sector_num) {
        qiov_offs = (node->cm.sector_num - acb->sector_num) << BDRV_SECTOR_BITS;
    } else {
        node_offs = (acb->sector_num - node->cm.sector_num) << BDRV_SECTOR_BITS;
    }
    size = ranges_overlap_size(acb->sector_num, acb->nb_sectors,
                               node->cm.sector_num, node->cm.nb_sectors)
           << BDRV_SECTOR_BITS;

    if (type & NODE_READ_BUF) {
        qemu_co_mutex_lock(&node->lock); /* XXX: use rw lock */
        copy = qemu_iovec_from_buf(acb->qiov, qiov_offs,
                                   node->data + node_offs, size);
        qemu_co_mutex_unlock(&node->lock);

        /* pcache node is no longer needed, when it was all read */
        atomic_add(&node->rdcnt, size >> BDRV_SECTOR_BITS);
        if (node->rdcnt >= node->cm.nb_sectors) {
            pcache_node_drop(acb->s, node);
        }
    } else {
        qemu_co_mutex_lock(&node->lock); /* XXX: use rw lock */
        copy = qemu_iovec_to_buf(acb->qiov, qiov_offs,
                                 node->data + node_offs, size);
        qemu_co_mutex_unlock(&node->lock);
    }
    assert(copy == size);
}

static inline void pcache_node_read_wait(PrefCacheAIOCB *acb, PCNode *node)
{
    ACBEntryLink *link = g_slice_alloc(sizeof(*link));
    link->acb = acb;

    atomic_inc(&node->wait.cnt);
    QTAILQ_INSERT_HEAD(&node->wait.list, link, entry);
    acb->ref++;
}

static void pcache_node_read(PrefCacheAIOCB *acb, PCNode* node)
{
    NODE_ASSERT(node->status == NODE_SUCCESS_STATUS ||
                node->status == NODE_WAIT_STATUS    ||
                node->status == NODE_REMOVE_STATUS, node);
    NODE_ASSERT(node->data != NULL, node);

    qemu_co_mutex_lock(&node->lock);
    if (node->status == NODE_WAIT_STATUS) {
        pcache_node_read_wait(acb, node);
        qemu_co_mutex_unlock(&node->lock);

        return;
    }
    qemu_co_mutex_unlock(&node->lock);

    pcache_node_rw_buf(acb, node, NODE_READ_BUF);
    pcache_node_unref(acb->s, node);
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
                pcache_node_unref(acb->s, node);
                node = new_node;
                continue;
            }
            size -= up_size;
            num += up_size;
        }
        up_size = MIN(node->cm.sector_num + node->cm.nb_sectors - num, size);
        pcache_node_read(acb, node); /* don't use node after pcache_node_read,
                                      * node maybe free.
                                      */
        node = NULL;

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
    BDRVPCacheState *s = acb->s;
    RbNodeKey key;
    PCNode *node;

    prefetch_init_key(acb, &key);

    /* add request statistics */
    lreq_node_add(acb, &key);

    qemu_co_mutex_lock(&s->pcache.tree.lock); /* XXX: use get_next_node */
    node = pcache_node_search(&s->pcache.tree.root, &key);
    qemu_co_mutex_unlock(&s->pcache.tree.lock);
    if (node == NULL) {
        return PREFETCH_NEW_NODE;
    }
    if (node->status == NODE_SUCCESS_STATUS) {
        pcache_lru_node_up(s, node);
    }

    /* Node covers the whole request */
    if (node->cm.sector_num <= acb->sector_num &&
        node->cm.sector_num + node->cm.nb_sectors >= acb->sector_num +
                                                     acb->nb_sectors)
    {
        pcache_node_read(acb, node);
        return PREFETCH_FULL_UP;
    }
    pcache_pickup_parts_of_cache(acb, node, key.num, key.size);

    return acb->requests.cnt == 0 ? PREFETCH_FULL_UP : PREFETCH_PART_UP;
}

static void pcache_aio_bh(void *opaque)
{
    PrefCacheAIOCB *acb = opaque;
    qemu_bh_delete(acb->bh);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_unref(acb);
}

static void complete_aio_request(PrefCacheAIOCB *acb)
{
    if (atomic_dec_fetch(&acb->ref) == 0) {
        acb->bh = aio_bh_new(bdrv_get_aio_context(acb->common.bs),
                             pcache_aio_bh, acb);
        qemu_bh_schedule(acb->bh);
    }
}

static void pcache_complete_acb_wait_queue(BDRVPCacheState *s, PCNode *node,
                                           int ret)
{
    ACBEntryLink *link, *next;

    if (atomic_read(&node->wait.cnt) == 0) {
        return;
    }

    QTAILQ_FOREACH_SAFE(link, &node->wait.list, entry, next) {
        PrefCacheAIOCB *wait_acb = link->acb;

        QTAILQ_REMOVE(&node->wait.list, link, entry);
        g_slice_free1(sizeof(*link), link);

        if (ret == 0) {
            pcache_node_rw_buf(wait_acb, node, NODE_READ_BUF);
        } else {  /* write only fail, because next request can rewrite error */
            wait_acb->ret = ret;
        }

        NODE_ASSERT(node->ref != 0, node);
        pcache_node_unref(s, node);

        complete_aio_request(wait_acb);
        atomic_dec(&node->wait.cnt);
    }
    NODE_ASSERT(atomic_read(&node->wait.cnt) == 0, node);
}

static void pcache_node_submit(PrefCachePartReq *req)
{
    PCNode *node = req->node;
    BDRVPCacheState *s = req->acb->s;

    assert(node != NULL);
    NODE_ASSERT(atomic_read(&node->ref) != 0, node);
    NODE_ASSERT(node->data != NULL, node);

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
        PCNode *node = req->node;
        QTAILQ_REMOVE(&acb->requests.list, req, entry);

        assert(req != NULL);
        NODE_ASSERT(node->status == NODE_WAIT_STATUS, node);

        if (acb->ret == 0) {
            pcache_node_submit(req);
            if (!(acb->aio_type & PCACHE_AIO_READAHEAD)) {
                pcache_node_rw_buf(acb, node, NODE_READ_BUF);
            }
        } else {
            pcache_node_drop(acb->s, node);
        }
        pcache_complete_acb_wait_queue(acb->s, node, acb->ret);

        pcache_node_unref(acb->s, node);
        g_slice_free1(sizeof(*req), req);
    }
    qemu_co_mutex_unlock(&acb->requests.lock);
}

static void pcache_update_node_state(PrefCacheAIOCB *acb)
{
    BDRVPCacheState *s = acb->s;
    RbNodeKey key;
    PCNode *node;
    uint64_t end_offs = acb->sector_num + acb->nb_sectors;

    key.num = acb->sector_num;
    do {
        key.size = end_offs - key.num;

        qemu_co_mutex_lock(&s->pcache.tree.lock); /* XXX: use get_next_node */
        node = pcache_node_search(&s->pcache.tree.root, &key);
        qemu_co_mutex_unlock(&s->pcache.tree.lock);
        if (node == NULL) {
            return;
        }
        if (node->status != NODE_WAIT_STATUS) {
            NODE_ASSERT(node->status == NODE_SUCCESS_STATUS, node);
#if PCACHE_WRITE_THROUGH_NODE
            pcache_node_rw_buf(acb, node, NODE_WRITE_BUF);
#else
            pcache_node_drop(s, node);
#endif
        }
        key.num = node->cm.sector_num + node->cm.nb_sectors;

        pcache_node_unref(s, node);
    } while (end_offs > key.num);
}

static void pcache_aio_cb(void *opaque, int ret)
{
    PrefCacheAIOCB *acb = opaque;

    if (ret != 0) {
        acb->ret = ret;
        DPRINTF("pcache aio_cb(num: %jd nb: %d) err: %d",
                acb->sector_num, acb->nb_sectors, ret);
    }
    if (acb->aio_type & PCACHE_AIO_READ) {
        if (atomic_dec_fetch(&acb->requests.cnt) > 0) {
            return;
        }
        pcache_merge_requests(acb);
        if (acb->aio_type & PCACHE_AIO_READAHEAD) {
            qemu_aio_unref(acb);
            return;
        }
    } else {        /* PCACHE_AIO_WRITE */
        pcache_update_node_state(acb);
    }

    complete_aio_request(acb);
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
    acb->requests.cnt = 0;
    acb->qiov = qiov;
    acb->aio_type = type;
    acb->ref = 1;
    acb->ret = 0;

    QTAILQ_INIT(&acb->requests.list);
    qemu_co_mutex_init(&acb->requests.lock);

    return acb;
}

static void pcache_send_acb_request_list(BlockDriverState *bs,
                                         PrefCacheAIOCB *acb)
{
    PrefCachePartReq *req;

    assert(acb->requests.cnt != 0);
    qemu_co_mutex_lock(&acb->requests.lock);
    QTAILQ_FOREACH(req, &acb->requests.list, entry) {
        bdrv_aio_readv(bs->file, req->sector_num, &req->qiov,
                       req->nb_sectors, pcache_aio_cb, acb);
    }
    qemu_co_mutex_unlock(&acb->requests.lock);
}

static void readahead_node_prev(PrefCacheAIOCB *acb, PCNode *node,
                                RbNodeKey *key)
{
    RbNodeKey lc_key;
    if (node->cm.key.num <= key->num) {
        return;
    }

    lc_key.num = key->num;
    lc_key.size = node->cm.key.num - key->num;

    pcache_node_add(acb, &lc_key);
}

static void readahead_node_next(PrefCacheAIOCB *acb, PCNode *node,
                                RbNodeKey *key, uint64_t total_sectors)
{
    BDRVPCacheState *s;
    RbNodeKey lc_key;
    if (node->cm.key.num + node->cm.key.size >= key->num + key->size) {
        return;
    }
    s = acb->s;

    lc_key.num = node->cm.key.num + node->cm.key.size;
    lc_key.size = s->cfg.readahead_size;
    if (total_sectors <= lc_key.num + lc_key.size) {
        return;
    }
    pcache_node_add(acb, &lc_key);
}

static bool check_allocated_blocks(BlockDriverState *bs, int64_t sector_num,
                                   int32_t nb_sectors)
{
    int ret, num;

    do {
        ret = bdrv_is_allocated(bs, sector_num, nb_sectors, &num);
        if (ret <= 0) {
            return false;
        }
        sector_num += num;
        nb_sectors -= num;

    } while (nb_sectors);

    return true;
}

static bool check_lreq_sequence(BDRVPCacheState *s, uint64_t sector_num)
{
    RbNodeKey key;
    LRNode *node;
    uint32_t cache_line_sz = s->cfg.readahead_size;

    if (sector_num <= cache_line_sz) {
        return false;
    }
            /* check is a previous cache block */
    key.num = sector_num - cache_line_sz;
    key.size = cache_line_sz;

    qemu_co_mutex_lock(&s->lreq.tree.lock);
    node = lreq_node_search(&s->lreq.tree.root, &key);
    qemu_co_mutex_unlock(&s->lreq.tree.lock);
    if (node == NULL) { /* requests isn't consistent,
                         * most likely there is no sense to make readahead.
                         */
        return false;
    }
    return node->cm.sector_num > key.num ? false : true;
}

static void pcache_readahead_request(BlockDriverState *bs, PrefCacheAIOCB *acb)
{
    BDRVPCacheState *s = acb->s;
    PrefCacheAIOCB *acb_readahead;
    RbNodeKey key;
    uint64_t total_sectors = bdrv_nb_sectors(bs);
    PCNode *node = NULL;

    if (!check_lreq_sequence(acb->s, acb->sector_num)) {
        return;
    }
    prefetch_init_key(acb, &key);

    key.num = key.num + key.size;
    if (total_sectors <= key.num + s->cfg.readahead_size) {
        return; /* readahead too small or beyond end of disk */
    }
    key.size = s->cfg.readahead_size;

    if (!check_allocated_blocks(bs->file->bs, key.num, key.size)) {
        return;
    }

    acb_readahead = pcache_aio_get(bs, key.num, NULL, key.size, acb->common.cb,
                                   acb->common.opaque, PCACHE_AIO_READ |
                                                       PCACHE_AIO_READAHEAD);
    if (!pcache_node_find_and_create(acb_readahead, &key, &node)) {
        readahead_node_prev(acb_readahead, node, &key);
        readahead_node_next(acb_readahead, node, &key, total_sectors);

        pcache_node_unref(s, node);
        if (acb_readahead->requests.cnt == 0) {
            qemu_aio_unref(acb_readahead);
            return;
        }
    }
    pcache_send_acb_request_list(bs, acb_readahead);
}

static inline bool pcache_skip_aio_read(BlockDriverState *bs,
                                        uint64_t sector_num,
                                        uint32_t nb_sectors)
{
    BDRVPCacheState *s = bs->opaque;

    if (nb_sectors > s->cfg.max_aio_size) {
        return true;
    }

    if (bdrv_nb_sectors(bs) < sector_num + nb_sectors) {
        return true;
    }

    return false;
}

static BlockAIOCB *pcache_aio_readv(BlockDriverState *bs,
                                    int64_t sector_num,
                                    QEMUIOVector *qiov,
                                    int nb_sectors,
                                    BlockCompletionFunc *cb,
                                    void *opaque)
{
    PrefCacheAIOCB *acb;
    int32_t status;

    if (pcache_skip_aio_read(bs, sector_num, nb_sectors)) {
        return bdrv_aio_readv(bs->file, sector_num, qiov, nb_sectors,
                              cb, opaque);
    }
    acb = pcache_aio_get(bs, sector_num, qiov, nb_sectors, cb,
                         opaque, PCACHE_AIO_READ);
    status = pcache_prefetch(acb);
    if (status == PREFETCH_NEW_NODE) {
        BlockAIOCB *ret = bdrv_aio_readv(bs->file, sector_num, qiov, nb_sectors,
                                         cb, opaque);
        pcache_readahead_request(bs, acb);
        qemu_aio_unref(acb); /* XXX: fix superfluous alloc */
        return ret;
    } else if (status == PREFETCH_FULL_UP) {
        assert(acb->requests.cnt == 0);
        complete_aio_request(acb);
    } else {
        assert(acb->requests.cnt != 0);

        pcache_send_acb_request_list(bs, acb);
    }
    pcache_readahead_request(bs, acb);

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
                                         opaque, PCACHE_AIO_WRITE);

    bdrv_aio_writev(bs->file, sector_num, qiov, nb_sectors,
                    pcache_aio_cb, acb);
    return &acb->common;
}

static void pcache_state_init(QemuOpts *opts, BDRVPCacheState *s)
{
    uint64_t cache_size = qemu_opt_get_size(opts, PCACHE_OPT_CACHE_SIZE,
                                            PCACHE_DEFAULT_CACHE_SIZE);
    uint64_t readahead_size = qemu_opt_get_size(opts, PCACHE_OPT_READAHEAD_SIZE,
                                                PCACHE_DEFAULT_READAHEAD_SIZE);
    uint64_t max_aio_size = qemu_opt_get_size(opts, PCACHE_OPT_MAX_AIO_SIZE,
                                              PCACHE_DEFAULT_MAX_AIO_SIZE);
    DPRINTF("pcache configure:\n");
    DPRINTF("pcache-full-size = %jd\n", cache_size);
    DPRINTF("readahead_size = %jd\n", readahead_size);
    DPRINTF("max_aio_size = %jd\n", max_aio_size);

    s->pcache.tree.root = RB_ROOT;
    qemu_co_mutex_init(&s->pcache.tree.lock);
    QTAILQ_INIT(&s->pcache.lru.list);
    qemu_co_mutex_init(&s->pcache.lru.lock);
    s->pcache.curr_size = 0;

    s->lreq.tree.root = RB_ROOT;
    qemu_co_mutex_init(&s->lreq.tree.lock);
    QTAILQ_INIT(&s->lreq.lru.list);
    qemu_co_mutex_init(&s->lreq.lru.lock);
    s->lreq.curr_size = 0;

    s->cfg.cache_size = cache_size >> BDRV_SECTOR_BITS;
    s->cfg.readahead_size = readahead_size >> BDRV_SECTOR_BITS;
    s->cfg.lreq_pool_size = PCACHE_DEFAULT_POOL_STAT_SIZE >> BDRV_SECTOR_BITS;
    s->cfg.max_aio_size = max_aio_size >> BDRV_SECTOR_BITS;

#ifdef PCACHE_DEBUG
    QTAILQ_INIT(&s->death_node_list);
    qemu_co_mutex_init(&s->death_node_lock);
#endif
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
    NODE_ASSERT(node->status == NODE_SUCCESS_STATUS, node);
    NODE_ASSERT(node->ref == 0, node);

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

    cnt = 0;
    if (!QTAILQ_EMPTY(&s->lreq.lru.list)) {
        QTAILQ_FOREACH_SAFE(node, &s->lreq.lru.list, entry, next) {
            QTAILQ_REMOVE(&s->lreq.lru.list, node, entry);
            g_slice_free1(sizeof(*node), node);
            cnt++;
        }
    }
    DPRINTF("used %d lreq nodes\n", cnt);

#ifdef PCACHE_DEBUG
    if (!QTAILQ_EMPTY(&s->death_node_list)) {
        cnt = 0;
        DPRINTF("warning: death node list contains of node\n");
        QTAILQ_FOREACH_SAFE(node, &s->death_node_list, entry, next) {
            QTAILQ_REMOVE(&s->death_node_list, node, entry);
            g_free(PCNODE(node)->data);
            g_slice_free1(sizeof(*node), node);
            cnt++;
        }
        DPRINTF("death nodes: %d", cnt);
    }
#endif
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
