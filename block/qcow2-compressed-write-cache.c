/*
 * Write cache for qcow2 compressed writes
 *
 * Copyright (c) 2021 Virtuozzo International GmbH.
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
#include "block/block-gen.h"
#include "qemu/coroutine.h"
#include "qapi/qapi-events-block-core.h"
#include "qcow2.h"

typedef struct CacheExtent {
    int64_t offset;
    int64_t bytes;
    void *buf;
    QLIST_ENTRY(CacheExtent) next;
} CacheExtent;

typedef struct CacheCluster {
    int64_t cluster_offset;
    int64_t n_bytes; /* sum of extents lengths */

    /*
     * data_end: cluster is full if data_end reached and ready to be flushed.
     * data_end is absoluted offset, not relative.
     */
    int64_t data_end;

    bool in_flight; /* cluster is being flushed now */

    /*
     * waiters: coroutines to wake after flush. Must be empty when in_flight is
     * false
     */
    CoQueue waiters;

    QTAILQ_ENTRY(CacheCluster) next;
    QLIST_HEAD(, CacheExtent) extents; /* sorted by offset */
} CacheCluster;

typedef QTAILQ_HEAD(, CacheCluster) CacheClusterQueue;

struct Qcow2CompressedWriteCache {
    /*
     * @data_file and @cluster_size are copied from qcow2 state. Not huge
     * duplications, seems better to avoid accessing the whole qcow2 state
     * instead.
     */
    BdrvChild *data_file;
    int64_t cluster_size;

    CoQueue waiters; /* coroutines, waiting for free place in the cache */

    /*
     * Cache limits not total number of clusters but total number of active
     * clusters. Active clusters are clusters with non-zero @n_bytes (and
     * therefor non-empty @extents). This is done so we don't need to wait for
     * cache flush qcow2_compressed_cache_set_cluster_end() (which may create
     * cluster with @data_end set but zero @n_bytes), as
     * qcow2_compressed_cache_set_cluster_end() is intended to be called from
     * qcow2 mutex critical section.
     */
    int nb_active_clusters;

    /*
     * If max_active_clusters is 0 it means that cache is inactive: all new
     * writes should fallthrough to data_file immediately.
     */
    int max_active_clusters;

    CacheClusterQueue clusters;
};

/* New extent takes ownership of @buf */
static CacheExtent *cache_extent_new(int64_t offset, int64_t bytes, void *buf)
{
    CacheExtent *e = g_new(CacheExtent, 1);

    *e = (CacheExtent) {
        .offset = offset,
        .bytes = bytes,
        .buf = buf,
    };

    return e;
}

static void cache_extent_free(CacheExtent *e)
{
    if (e) {
        g_free(e->buf);
        g_free(e);
    }
}

static CacheCluster *find_cluster(Qcow2CompressedWriteCache *s,
                                  int64_t cluster_offset)
{
    CacheCluster *c;

    assert(QEMU_IS_ALIGNED(cluster_offset, s->cluster_size));

    QTAILQ_FOREACH(c, &s->clusters, next) {
        if (cluster_offset == c->cluster_offset) {
            return c;
        }
    }

    return NULL;
}

/* Creates "inactive" cluster, which doesn't influence s->nb_active_clusters */
static CacheCluster *cache_cluster_new(Qcow2CompressedWriteCache *s,
                                       int64_t cluster_offset)
{
    CacheCluster *c;

    assert(!find_cluster(s, cluster_offset));

    c = g_new(CacheCluster, 1);
    *c = (CacheCluster) {
        .cluster_offset = cluster_offset,
        .data_end = cluster_offset + s->cluster_size
    };

    qemu_co_queue_init(&c->waiters);
    QTAILQ_INSERT_TAIL(&s->clusters, c, next);

    return c;
}

static void cache_cluster_free(CacheCluster *cluster)
{
    assert(!cluster->in_flight);
    assert(qemu_co_queue_empty(&cluster->waiters));
    QLIST_FOREACH_FUNC_SAFE(&cluster->extents, next, cache_extent_free);
    g_free(cluster);
}

static bool cache_cluster_is_full(CacheCluster *cluster)
{
    assert(cluster->cluster_offset + cluster->n_bytes <= cluster->data_end);
    return cluster->cluster_offset + cluster->n_bytes == cluster->data_end;
}

static void cache_cluster_remove(Qcow2CompressedWriteCache *s,
                                 CacheCluster *cluster)
{
    if (cluster->n_bytes) {
        s->nb_active_clusters--;
    }
    QTAILQ_REMOVE(&s->clusters, cluster, next);
    cache_cluster_free(cluster);
}

/*
 * Return number of consequtive clusters starting from @first. Store next after
 * last extent pointer into @next, store end offset of last extent into
 * @end_off.
 */
static int count_consequitive_extents(CacheExtent *first,
                                      CacheExtent **next,
                                      int64_t *end_off)
{
    CacheExtent *e;
    int64_t off = first->offset;
    int nb = 0;

    for (e = first; e; e = QLIST_NEXT(e, next)) {
        assert(e->offset >= off);
        if (e->offset > off) {
            break;
        }
        off += e->bytes;
        nb++;
    }

    if (next) {
        *next = e;
    }

    if (end_off) {
        *end_off = off;
    }

    return nb;
}

/*
 * Write consequtive extents, starting from @firest. Store next after last
 * extent pointer into @next. If align > 1, align end of the whole write by
 * zero.
 */
static int coroutine_fn flush_consequitive_extents(Qcow2CompressedWriteCache *s,
                                                   CacheExtent *first,
                                                   CacheExtent **next,
                                                   int64_t align)
{
    CacheExtent *e, *end_extent;
    int64_t end;
    int nb_extents = count_consequitive_extents(first, &end_extent, &end);
    int64_t aligned_end = QEMU_ALIGN_UP(end, align);
    int64_t tail = aligned_end - end;
    int64_t len = aligned_end - first->offset;

    /*
     * Alignment if for flushing full cluster, first extent offset is always
     * aligned.
     */
    assert(QEMU_IS_ALIGNED(first->offset, align));

    if (next) {
        *next = end_extent;
    }

    if (tail) {
        nb_extents++;
    }

    if (nb_extents > IOV_MAX) {
        g_autofree void *buf = g_malloc(len);
        char *p = buf;

        for (e = first; e != end_extent; e = QLIST_NEXT(e, next)) {
            memcpy(p, e->buf, e->bytes);
            p += e->bytes;
        }

        if (tail) {
            memset(p, 0, tail);
        }

        BLKDBG_EVENT(s->data_file, BLKDBG_WRITE_COMPRESSED);
        return bdrv_co_pwrite(s->data_file, first->offset, len, buf, 0);
    } else {
        int ret;
        QEMUIOVector qiov;
        g_autofree void *tail_buf = NULL;

        qemu_iovec_init(&qiov, nb_extents);
        for (e = first; e != end_extent; e = QLIST_NEXT(e, next)) {
            qemu_iovec_add(&qiov, e->buf, e->bytes);
        }

        if (tail) {
            tail_buf = g_malloc0(tail);
            qemu_iovec_add(&qiov, tail_buf, tail);
        }

        BLKDBG_EVENT(s->data_file, BLKDBG_WRITE_COMPRESSED);
        ret = bdrv_co_pwritev(s->data_file, first->offset, len, &qiov, 0);
        qemu_iovec_destroy(&qiov);
        return ret;
    }
}

static int coroutine_fn cache_cluster_flush_full(Qcow2CompressedWriteCache *s,
                                                 CacheCluster *cluster)
{
    int ret;
    CacheExtent *end_extent;
    int64_t align = MIN(s->cluster_size,
                        MAX(s->data_file->bs->bl.request_alignment, 4 * 1024));

    assert(cache_cluster_is_full(cluster));

    ret = flush_consequitive_extents(s, QLIST_FIRST(&cluster->extents),
                                     &end_extent, align);

    assert(end_extent == NULL); /* all extents flushed */

    return ret;
}

static int coroutine_fn cache_cluster_flush(Qcow2CompressedWriteCache *s,
                                            CacheCluster *c)
{
    int ret;
    CacheExtent *e = QLIST_FIRST(&c->extents);

    if (cache_cluster_is_full(c)) {
        return cache_cluster_flush_full(s, c);
    }

    while (e) {
        ret = flush_consequitive_extents(s, e, &e, 1);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

int coroutine_fn qcow2_compressed_cache_co_flush(Qcow2CompressedWriteCache *s)
{
    int ret = 0;
    CacheCluster *c;
    GList *local_clusters = NULL, *p;

    /*
     * Make a snapshot of current state: we will not flush clusters created in
     * parallel with flush operations and don't allow adding more extents to
     * staged clusters. We are also protected from parallel flush operations
     * flushing the same clusters.
     */
    QTAILQ_FOREACH(c, &s->clusters, next) {
        if (!c->in_flight && c->n_bytes) {
            c->in_flight = true;
            local_clusters = g_list_append(local_clusters, c);
        }
    }

    for (p = local_clusters; p; p = p->next) {
        CacheCluster *c = p->data;

        if (ret == 0) {
            ret = cache_cluster_flush(s, c);
        }

        c->in_flight = false;
        qemu_co_queue_restart_all(&c->waiters);

        if (ret == 0) {
            cache_cluster_remove(s, c);
        }
    }

    g_list_free(local_clusters);

    return ret;
}

int coroutine_fn
qcow2_compressed_cache_co_stop_flush(Qcow2CompressedWriteCache *s)
{
    int ret, save;

    save = s->max_active_clusters;
    s->max_active_clusters = 0; /* No more extents */

    ret = qcow2_compressed_cache_co_flush(s);
    if (ret < 0) {
        s->max_active_clusters = save;
        return ret;
    }

    assert(QTAILQ_EMPTY(&s->clusters));
    return 0;
}

/* @cluster takes ownership of @extent */
static void cluster_add_extent(Qcow2CompressedWriteCache *s,
                               CacheCluster *cluster, CacheExtent *extent)
{
    CacheExtent *e;

    assert(extent->bytes);
    assert(extent->offset >= cluster->cluster_offset);
    assert(extent->offset + extent->bytes <= cluster->data_end);
    assert(!cluster->in_flight);

    e = QLIST_FIRST(&cluster->extents);
    if (!e) {
        /* inactive cluster */
        assert(!cluster->n_bytes);
        s->nb_active_clusters++;
        assert(s->nb_active_clusters <= s->max_active_clusters);
        QLIST_INSERT_HEAD(&cluster->extents, extent, next);
    } else if (e->offset > extent->offset) {
        assert(extent->offset + extent->bytes <= e->offset);
        QLIST_INSERT_HEAD(&cluster->extents, extent, next);
    } else {
        while (QLIST_NEXT(e, next) &&
               QLIST_NEXT(e, next)->offset < extent->offset) {
            e = QLIST_NEXT(e, next);
        }

        /* Now e is last element with offset < extent->offset */
        assert(e->offset + e->bytes <= extent->offset);

        QLIST_INSERT_AFTER(e, extent, next);

        e = QLIST_NEXT(extent, next);
        if (e) {
            assert(extent->offset + extent->bytes <= e->offset);
        }
    }

    cluster->n_bytes += extent->bytes;
}

static CacheCluster *find_cluster_to_flush(Qcow2CompressedWriteCache *s)
{
    CacheCluster *c;

    QTAILQ_FOREACH(c, &s->clusters, next) {
        if (!c->in_flight && cache_cluster_is_full(c)) {
            return c;
        }
    }

    return NULL;
}

/* Cache an extent if there is a place */
static bool coroutine_fn
try_cache_extent(Qcow2CompressedWriteCache *s, CacheExtent *extent,
                 bool *cluster_in_flight)
{
    CacheCluster *c;
    int64_t cluster_offset = QEMU_ALIGN_DOWN(extent->offset, s->cluster_size);

    assert(extent->bytes);

    if (s->max_active_clusters == 0) {
        *cluster_in_flight = true;
        return false;
    }

    *cluster_in_flight = false;

    c = find_cluster(s, cluster_offset);
    if (c && c->in_flight) {
        *cluster_in_flight = true;
        return false;
    }
    if (s->nb_active_clusters >= s->max_active_clusters &&
        (!c || !c->n_bytes))
    {
        /*
         * Cache is full, we can't allocate a new cluster and can't activate
         * existing inactive cluster
         */
        return false;
    }

    if (!c) {
        c = cache_cluster_new(s, cluster_offset);
    }

    cluster_add_extent(s, c, extent);

    if (cache_cluster_is_full(c)) {
        qemu_co_queue_restart_all(&s->waiters);
    }

    return true;
}

/* Takes ownership of @buf, don't free it after call! */
int coroutine_fn
qcow2_compressed_cache_co_write(Qcow2CompressedWriteCache *s, int64_t offset,
                                int64_t bytes, void *buf)
{
    int ret;
    int i;
    CacheExtent *extents[] = {NULL, NULL};
    int nb_extents = 0; /* number of non-NULL elements in @extents */
    int64_t e0_len;

    assert(bytes > 0);
    assert(bytes < s->cluster_size);

    e0_len = MIN(bytes, QEMU_ALIGN_UP(offset + 1, s->cluster_size) - offset);
    extents[0] = cache_extent_new(offset, e0_len, buf);
    nb_extents = 1;

    if (bytes > e0_len) {
        int64_t e1_len = bytes - e0_len;
        /*
         * We have to allocate a new buffer, so that clusters are independent
         * and can free their extents when needed.
         */
        void *e1_buf = g_memdup(((const char *)buf) + e0_len, e1_len);

        extents[1] = cache_extent_new(offset + e0_len, e1_len, e1_buf);
        nb_extents = 2;
    }

    while (nb_extents) {
        bool do_write = false;

        for (i = 0; i < 2; i++) {
            CacheExtent *e = extents[i];

            do_write = false;

            if (!e) {
                continue;
            }

            if (try_cache_extent(s, e, &do_write)) {
                extents[i] = NULL;
                nb_extents--;
                continue;
            }

            if (do_write) {
                ret = bdrv_co_pwrite(s->data_file, e->offset, e->bytes,
                                     e->buf, 0);

                cache_extent_free(e);
                extents[i] = NULL;
                nb_extents--;

                if (ret < 0) {
                    goto out;
                }
            }
        }

        if (do_write) {
            /*
             * We yielded during second extent writing. Probably the cache is
             * already free and we can now cache the first extent.
             */
            continue;
        }

        if (nb_extents) {
            CacheCluster *cluster = find_cluster_to_flush(s);

            if (cluster) {
                cluster->in_flight = true;
                ret = cache_cluster_flush_full(s, cluster);
                cluster->in_flight = false;
                qemu_co_queue_restart_all(&cluster->waiters);
                qemu_co_queue_restart_all(&s->waiters);
                if (ret < 0) {
                    goto out;
                }
                cache_cluster_remove(s, cluster);
                continue;
            }

            qemu_co_queue_wait(&s->waiters, NULL);
        }
    }

    ret = 0;

out:
    for (i = 0; i < 2; i++) {
        cache_extent_free(extents[i]);
    }

    return 0;
}

int coroutine_fn
qcow2_compressed_cache_co_read(Qcow2CompressedWriteCache *s, int64_t offset,
                               int64_t bytes, void *buf)
{
    CacheCluster *c;
    CacheExtent *e;
    int64_t cluster_offset = QEMU_ALIGN_DOWN(offset, s->cluster_size);

    c = find_cluster(s, cluster_offset);
    if (c) {
        QLIST_FOREACH(e, &c->extents, next) {
            if (e->offset == offset && e->bytes <= bytes) {
                memcpy(buf, e->buf, e->bytes);
                return 0;
            }
        }
    }

    return bdrv_co_pread(s->data_file, offset, bytes, buf, 0);
}

/*
 * Caller states, that there would be no writes to this cluster beyond
 * specified @cluster_data_end. So, it's OK to flush cluster when it is filled
 * up to @cluster_data_end and it's OK to align flushing write operation up to
 * some alignment (not greater than cluster_size of course).
 */
void coroutine_fn
qcow2_compressed_cache_co_set_cluster_end(Qcow2CompressedWriteCache *s,
                                          int64_t cluster_data_end)
{
    int64_t cluster_offset = QEMU_ALIGN_DOWN(cluster_data_end, s->cluster_size);
    CacheExtent *e;
    CacheCluster *c;

    c = find_cluster(s, cluster_offset);
    if (!c) {
        c = cache_cluster_new(s, cluster_offset);
    }

    QLIST_FOREACH(e, &c->extents, next) {
        assert(e->offset + e->bytes <= cluster_data_end);
    }

    /* Shouldn't set data_end several times */
    assert(c->data_end == c->cluster_offset + s->cluster_size);

    c->data_end = cluster_data_end;
    if (cache_cluster_is_full(c)) {
        qemu_co_queue_restart_all(&s->waiters);
    }
}

Qcow2CompressedWriteCache *qcow2_compressed_cache_new(BdrvChild *data_file,
                                                      int64_t cluster_size,
                                                      int64_t cache_size)
{
    Qcow2CompressedWriteCache *s = g_new(Qcow2CompressedWriteCache, 1);

    assert(cache_size >= cluster_size);

    *s = (Qcow2CompressedWriteCache) {
        .data_file = data_file,
        .cluster_size = cluster_size,
        .max_active_clusters = cache_size / cluster_size,
    };

    qemu_co_queue_init(&s->waiters);
    QTAILQ_INIT(&s->clusters);

    return s;
}

void qcow2_compressed_cache_free(Qcow2CompressedWriteCache *s)
{
    if (!s) {
        return;
    }

    QTAILQ_FOREACH_FUNC_SAFE(&s->clusters, next, cache_cluster_free);
    g_free(s);
}

void qcow2_compressed_cache_set_size(Qcow2CompressedWriteCache *s,
                                     int64_t size)
{
    /*
     * We don't do flush here. Don't care too much, it's safe to have cache
     * larger than maximum, it will only decrease until it reaches the new
     * maximum.
     */
    assert(size >= s->cluster_size);
    s->max_active_clusters = size / s->cluster_size;
}

void coroutine_fn
qcow2_compressed_cache_co_discard(Qcow2CompressedWriteCache *s,
                                  int64_t cluster_offset)
{
    CacheCluster *c;

    while (true) {
        c = find_cluster(s, cluster_offset);
        if (!c) {
            return;
        }
        if (!c->in_flight) {
            cache_cluster_remove(s, c);
            return;
        }
        qemu_co_queue_wait(&c->waiters, NULL);
    }
}


/*
 * Wrappers for qcow2_compressed_cache_co_flush
 *
 * TODO: update scripts/block-coroutine-wrapper to generate this too
 */

typedef struct Qcow2CompressedCacheCoFlush {
    BdrvPollCo poll_state;
    Qcow2CompressedWriteCache *state;
} Qcow2CompressedCacheCoFlush;

static void coroutine_fn
qcow2_compressed_cache_co_flush_entry(void *opaque)
{
    Qcow2CompressedCacheCoFlush *s = opaque;

    s->poll_state.ret = qcow2_compressed_cache_co_flush(s->state);
    s->poll_state.in_progress = false;

    aio_wait_kick();
}

int qcow2_compressed_cache_flush(BlockDriverState *bs,
                                 Qcow2CompressedWriteCache *state)
{
    if (qemu_in_coroutine()) {
        return qcow2_compressed_cache_co_flush(state);
    } else {
        Qcow2CompressedCacheCoFlush s = {
            .poll_state.bs = bs,
            .poll_state.in_progress = true,

            .state = state,
        };

        s.poll_state.co =
            qemu_coroutine_create(qcow2_compressed_cache_co_flush_entry, &s);

        return bdrv_poll_co(&s.poll_state);
    }
}

/*
 * Wrappers for qcow2_compressed_cache_co_stop_flush
 *
 * TODO: update scripts/block-coroutine-wrapper to generate this too
 */

typedef struct Qcow2CompressedCacheCoStopFlush {
    BdrvPollCo poll_state;
    Qcow2CompressedWriteCache *state;
} Qcow2CompressedCacheCoStopFlush;

static void coroutine_fn
qcow2_compressed_cache_co_stop_flush_entry(void *opaque)
{
    Qcow2CompressedCacheCoStopFlush *s = opaque;

    s->poll_state.ret = qcow2_compressed_cache_co_stop_flush(s->state);
    s->poll_state.in_progress = false;

    aio_wait_kick();
}

int qcow2_compressed_cache_stop_flush(BlockDriverState *bs,
                                      Qcow2CompressedWriteCache *state)
{
    if (qemu_in_coroutine()) {
        return qcow2_compressed_cache_co_stop_flush(state);
    } else {
        Qcow2CompressedCacheCoStopFlush s = {
            .poll_state.bs = bs,
            .poll_state.in_progress = true,

            .state = state,
        };

        s.poll_state.co =
            qemu_coroutine_create(qcow2_compressed_cache_co_stop_flush_entry,
                                  &s);

        return bdrv_poll_co(&s.poll_state);
    }
}
