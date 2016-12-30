/*
 * QEMU Range-Based Cache core
 *
 * Copyright (C) 2015-2016 Parallels IP Holdings GmbH.
 *
 * Author: Pavel Butsykin <pbutsykin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/rbcache.h"

/* RBCache provides functionality to cache the data from block devices
 * (basically). The range here is used as the main key for searching and storing
 * data. The cache is based on red-black trees, so basic operations search,
 * insert, delete are performed for O(log n).
 *
 * It is important to note that QEMU usually does not require a data cache, but
 * in reality, there are already some cases where a cache of small amounts can
 * increase performance, so as the data structure was selected red-black trees,
 * this is a quite simple data structure and show high efficiency on a small
 * number of elements. Therefore, when the minimum range is 512 bytes, the
 * recommended size of the cache memory no more than 8-16mb. Also note that this
 * cache implementation allows to store ranges of different lengths without
 * alignment.
 */

struct RBCache {
    struct RbRoot root;
    RBNodeAlloc *alloc;
    RBNodeFree  *free;
    uint64_t limit_size;
    uint64_t cur_size;
    enum eviction_type eviction_type;
    void *opaque;

    QTAILQ_HEAD(RBCacheNodeHead, RBCacheNode) queue;
};

static int node_key_cmp(const RBCacheNode *node1, const RBCacheNode *node2)
{
    assert(node1 != NULL);
    assert(node2 != NULL);

    if (node1->offset >= node2->offset + node2->bytes) {
        return 1;
    }
    if (node1->offset + node1->bytes <= node2->offset) {
        return -1;
    }

    return 0;
}

/* Find leftmost node that intersects given target_offset. */
static RBCacheNode *node_previous(RBCacheNode *node, uint64_t target_offset)
{
    while (node) {
        struct RbNode *prev_rb_node = rb_prev(&node->rb_node);
        RBCacheNode *prev_node;
        if (prev_rb_node == NULL) {
            break;
        }
        prev_node = container_of(prev_rb_node, RBCacheNode, rb_node);
        if (prev_node->offset + prev_node->bytes <= target_offset) {
            break;
        }
        node = prev_node;
    }

    assert(node != NULL);

    return node;
}

RBCacheNode *rbcache_node_alloc(RBCache *rbcache, uint64_t offset,
                                uint64_t bytes)
{
    RBCacheNode *node;

    if (rbcache->alloc) {
        node = rbcache->alloc(offset, bytes, rbcache->opaque);
    } else {
        node = g_new(RBCacheNode, 1);
    }

    node->offset = offset;
    node->bytes  = bytes;

    return node;
}

void rbcache_node_free(RBCache *rbcache, RBCacheNode *node)
{
    if (rbcache->free) {
        rbcache->free(node, rbcache->opaque);
    } else {
        g_free(node);
    }
}

static void rbcache_try_shrink(RBCache *rbcache)
{
    while (rbcache->cur_size > rbcache->limit_size) {
        RBCacheNode *node;
        assert(!QTAILQ_EMPTY(&rbcache->queue));

        node = QTAILQ_LAST(&rbcache->queue, RBCacheNodeHead);

        rbcache_remove(rbcache, node);
    }
}

static inline void node_move_in_queue(RBCache *rbcache, RBCacheNode *node)
{
    if (rbcache->eviction_type == RBCACHE_LRU) {
        QTAILQ_REMOVE(&rbcache->queue, node, entry);
        QTAILQ_INSERT_HEAD(&rbcache->queue, node, entry);
    }
}

/*
 * Adds a new node to the tree if the range of the node doesn't overlap with
 * existing nodes, and returns the new node. If the new node overlaps with
 * another existing node, the tree is not changed and the function returns a
 * pointer to the existing node. If the new node covers multiple nodes, then
 * returns the leftmost node in the tree.
 */
static RBCacheNode *node_insert(RBCache *rbcache, RBCacheNode *node, bool alloc)
{
    struct RbNode **new, *parent = NULL;

    assert(rbcache != NULL);
    assert(node->bytes != 0);

    /* Figure out where to put new node */
    new = &(rbcache->root.rb_node);
    while (*new) {
        RBCacheNode *this = container_of(*new, RBCacheNode, rb_node);
        int result = node_key_cmp(node, this);
        if (result == 0) {
            this = node_previous(this, node->offset);
            node_move_in_queue(rbcache, this);
            return this;
        }
        parent = *new;
        new = result < 0 ? &((*new)->rb_left) : &((*new)->rb_right);
    }

    if (alloc) {
        node = rbcache_node_alloc(rbcache, node->offset, node->bytes);
    }
    /* Add new node and rebalance tree. */
    rb_link_node(&node->rb_node, parent, new);
    rb_insert_color(&node->rb_node, &rbcache->root);

    rbcache->cur_size += node->bytes;

    rbcache_try_shrink(rbcache);

    QTAILQ_INSERT_HEAD(&rbcache->queue, node, entry);

    return node;
}

void *rbcache_search(RBCache *rbcache, uint64_t offset, uint64_t bytes)
{
    struct RbNode *rb_node;
    RBCacheNode node = {
        .offset = offset,
        .bytes  = bytes,
    };

    assert(rbcache != NULL);

    rb_node = rbcache->root.rb_node;
    while (rb_node) {
        RBCacheNode *this = container_of(rb_node, RBCacheNode, rb_node);
        int32_t result = node_key_cmp(&node, this);
        if (result == 0) {
            this = node_previous(this, offset);
            node_move_in_queue(rbcache, this);
            return this;
        }
        rb_node = result < 0 ? rb_node->rb_left : rb_node->rb_right;
    }
    return NULL;
}

void *rbcache_insert(RBCache *rbcache, RBCacheNode *node)
{
    return node_insert(rbcache, node, false);
}

void *rbcache_search_and_insert(RBCache *rbcache, uint64_t offset,
                                uint64_t bytes)
{
    RBCacheNode node = {
        .offset = offset,
        .bytes  = bytes,
    };

    return node_insert(rbcache, &node, true);
}

void rbcache_remove(RBCache *rbcache, RBCacheNode *node)
{
    assert(rbcache->cur_size >= node->bytes);

    rbcache->cur_size -= node->bytes;
    rb_erase(&node->rb_node, &rbcache->root);

    QTAILQ_REMOVE(&rbcache->queue, node, entry);

    rbcache_node_free(rbcache, node);
}

RBCache *rbcache_create(RBNodeAlloc *alloc, RBNodeFree *free,
                        uint64_t limit_size, int eviction_type, void *opaque)
{
    RBCache *rbcache = g_new(RBCache, 1);

    /* We can't use only one callback, or both or neither */
    assert(!(!alloc ^ !free));

    *rbcache = (RBCache) {
        .root          = RB_ROOT,
        .alloc         = alloc,
        .free          = free,
        .limit_size    = limit_size,
        .eviction_type = eviction_type,
        .opaque        = opaque,
        .queue         = QTAILQ_HEAD_INITIALIZER(rbcache->queue),
    };

    return rbcache;
}

void rbcache_destroy(RBCache *rbcache)
{
    RBCacheNode *node, *next;

    assert(rbcache != NULL);

    QTAILQ_FOREACH_SAFE(node, &rbcache->queue, entry, next) {
        QTAILQ_REMOVE(&rbcache->queue, node, entry);
        rbcache_node_free(rbcache, node);
    }

    g_free(rbcache);
}
