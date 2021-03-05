/*
 * Cache for small sequential write requests.
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
 *
 *
 * = Description =
 *
 * SeqCache is an abbreviation for Sequential Cache.
 *
 * The Cache is intended to improve performance of small unaligned sequential
 * writes. Cache has a cluster_size parameter and the unit of caching is aligned
 * cluster.  Cache has a list of cached clusters, several "finished" ones and at
 * most one "unfinished". "unfinished" cluster is a cluster where last byte of
 * last write operation is cached and there is a free place after that byte to
 * the end of cluster. "finished" clusters are just stored in cache to be read
 * or flushed and don't allow any writes to them.
 *
 * If write to the cache intersects cluster bounds, it's splat into several
 * requests by cluster bounds. So, consider a write that doesn't intersect
 * cluster bounds to describe the whole picture:
 *
 * There are two cases allowed:
 *
 * 1. Sequential write to "unfinished" cluster. Actually it's a write sequential
 *    previous write. The data goes to "unfinished" cluster. If "unfinished" is
 *    filled up to the cluster bound it becomes "finished".
 *
 * 2. Write to new cluster, not existing in the cache. It may be sequential to
 *    previous write or not. Current "unfinshed" cluster (if exists) becomes
 *    "finished" and new "unfinished" cluster is started. Note also that offset
 *    of the write to new cluster is not required to be aligned.
 *
 *    Any other write operation (non-sequential write to "unfinished" cluster
 *    or write to any of "finished" clusters) will crash.
 */

#include "qemu/osdep.h"

#include "qemu/queue.h"
#include "qemu/seqcache.h"

/*
 * Cluster
 *
 * Representation of one cached cluster, aligned to SeqCache::cluster_size.
 * Caches only one subregion of the cluster, started at @offset (may be
 * unaligned to cluster_size) and of @bytes length (may be unaligned as well).
 * The whole subregion always lay in one aligned cluster:
 *
 *      QEMU_ALIGN_DOWN(offset, cluster_size) ==
 *          QEMU_ALIGN_DOWN(offset + bytes - 1, cluster_size)
 *
 * @buf is allocated to be able to fill the cluster up to the cluster end, i.e.
 * allocated @buf length is at least:
 *
 *      cluster_size - offset % cluster_size
 */
typedef struct Cluster {
   int64_t offset;
   int64_t bytes;
   uint8_t *buf;
   QSIMPLEQ_ENTRY(Cluster) entry;
} Cluster;

/*
 * SeqCache caches small sequential writes into "unfinished" @cur_write cluster,
 * until entire cluster (of @cluster_size bytes) is filled by seqcache_write()
 * calls.
 *
 * @cur_write->offset may be unaligned to @cluster_size if first write offset is
 * not aligned (for example, if there was a flush request and cache was flushed,
 * then we continue from the middle of the cluster with an empty cache).
 *
 * @cur_write may be NULL, which means we don't have current cluster and next
 * seqcache_write() will start a new one.
 *
 * @all is a list of all clusters cached in the cache, some "finished" and one
 * "unfinished" @cur_write (if exists). If @cur_write is not NULL it is a last
 * one in the list.
 *
 * @nb_clusters is number of elements in @all list.
 *
 * @next_flush is an iterator for flushing. SeqCache knows nothing about how
 * data should be flushing, so for flush user calls seqcache_get_next_flush()
 * (which moves @next_flush iterator) and when data is flushed somehow and cache
 * is not needed anymore, user can call seqcache_discard_cluster().
 */
typedef struct SeqCache {
    int64_t cluster_size;
    int64_t nb_clusters;
    Cluster *cur_write;
    Cluster *next_flush;
    QSIMPLEQ_HEAD(, Cluster) all;
} SeqCache;

static void cluster_free(Cluster *req)
{
    if (req) {
        g_free(req->buf);
        g_free(req);
    }
}

SeqCache *seqcache_new(int64_t cluster_size)
{
    SeqCache *s = g_new(SeqCache, 1);

    *s = (SeqCache) {
        .cluster_size = cluster_size,
    };

    QSIMPLEQ_INIT(&s->all);

    return s;
}

/*
 * User should clean the cache calling seqcache_get_next_flush() and
 * seqcache_discard_cluster() sequentially before freeing it.
 */
void seqcache_free(SeqCache *s)
{
    if (s) {
        assert(QSIMPLEQ_EMPTY(&s->all));
        g_free(s);
    }
}

/* End of cached region inside one cluster */
static int64_t cached_end(Cluster *cl)
{
    return cl->offset + cl->bytes;
}

/* Start of aligned cluster containing the @offset */
static int64_t cluster_start(SeqCache *s, int64_t offset)
{
    return QEMU_ALIGN_DOWN(offset, s->cluster_size);
}

/* End of aligned cluster containing the @offset */
static int64_t cluster_end(SeqCache *s, int64_t offset)
{
    return cluster_start(s, offset) + s->cluster_size;
}

/* Align down @offset to s->cluster_size and search for corresponding cluster */
static Cluster *seqcache_find_cluster(SeqCache *s, int64_t offset)
{
    Cluster *cl;
    int64_t cl_start = cluster_start(s, offset);

    QSIMPLEQ_FOREACH(cl, &s->all, entry) {
        if (cluster_start(s, cl->offset) == cl_start) {
            return cl;
        }
    }

    return NULL;
}

/* Makes current "unfinished" cluster the "finished" one. */
static void seqcache_finalize_current_cluster(SeqCache *s)
{
    if (s->cur_write && !s->next_flush) {
        s->next_flush = s->cur_write;
    }
    s->cur_write = NULL;
}

/*
 * Write an @offset, @bytes, @buf request into the cache. The requests MUST not
 * intersect cluster bounds.
 */
static void seqcache_write_one(SeqCache *s, int64_t offset, int64_t bytes,
                               uint8_t *buf)
{
    assert(bytes > 0);
    assert(cluster_start(s, offset) == cluster_start(s, offset + bytes - 1));

    if (s->cur_write && offset == cached_end(s->cur_write)) {
        /* Continue sequential process */
        memcpy(s->cur_write->buf + s->cur_write->bytes, buf, bytes);
        s->cur_write->bytes += bytes;

        if (cached_end(s->cur_write) == cluster_end(s, s->cur_write->offset)) {
            seqcache_finalize_current_cluster(s);
        }

        return;
    }

    /* We are starting a new cluster. Check that it's really new */
    assert(!seqcache_find_cluster(s, offset));

    seqcache_finalize_current_cluster(s);

    s->cur_write = g_new(Cluster, 1);
    *s->cur_write = (Cluster) {
        .offset = offset,
        .bytes = bytes,
        .buf = g_malloc(s->cluster_size),
    };

    memcpy(s->cur_write->buf, buf, bytes);
    QSIMPLEQ_INSERT_TAIL(&s->all, s->cur_write, entry);
    s->nb_clusters++;
}

/* Write an @offset, @bytes, @buf request into the cache. */
void seqcache_write(SeqCache *s, int64_t offset, int64_t bytes, uint8_t *buf)
{
    while (bytes) {
        int64_t cl_end = cluster_end(s, offset);
        int64_t chunk = MIN(bytes, cl_end - offset);

        seqcache_write_one(s, offset, chunk, buf);
        offset += chunk;
        bytes -= chunk;
        buf += chunk;
    }
}

/*
 * Read from the cache.
 *
 * If @offset misses the cache, return 0.
 *
 * If @offset is inside the cache, copy corresponding available data to @buf
 * (may be less than required @bytes if hole reached earlier) and return number
 * of bytes copied.
 */
int64_t seqcache_read(SeqCache *s, int64_t offset, int64_t bytes, uint8_t *buf)
{
    uint8_t *pos = buf;

    while (bytes) {
        Cluster *cl = seqcache_find_cluster(s, offset);
        int64_t chunk;

        if (!cl || offset < cl->offset || offset >= cached_end(cl)) {
            break;
        }

        chunk = MIN(bytes, cached_end(cl) - offset);
        memcpy(pos, cl->buf + offset - cl->offset, chunk);
        offset += chunk;
        bytes -= chunk;
        pos += chunk;

        if (!QEMU_IS_ALIGNED(offset, s->cluster_size)) {
            break;
        }
    }

    return pos - buf;
}

/*
 * Get next region for flushing. @offset, @bytes and @buf are out-parameters
 * to return the region.
 *
 * @unfinished is in-out argument which means that user is interested in
 * flushing unfinished cluster too:
 *
 * If there are "finished" clusters, "finished" cluster is returned and
 * *@unfinished is set to false, independently of its original value.
 *
 * If there are no "finished" clusters but "unfinished" exists (i.e.
 * s->cur_write != NULL and it is the only element of s->all), then *@unfinished
 * value remains the same and the following logic works:
 *
 *    If *@unfinished:
 *       return s->cur_write unfinished cluster for flushing
 *    Else
 *       return nothing
 *
 *
 * Returns true and set @offset, @bytes, @buf and @unfinished if there is
 * something to flush (accordingly to @unfinished value), returns false
 * otherwise.
 *
 * Nothing is removed from the cache.
 */
bool seqcache_get_next_flush(SeqCache *s, int64_t *offset, int64_t *bytes,
                             uint8_t **buf, bool *unfinished)
{
    Cluster *req = s->next_flush;

    if (s->next_flush) {
        *unfinished = false;
        req = s->next_flush;
        s->next_flush = QSIMPLEQ_NEXT(req, entry);
        if (s->next_flush == s->cur_write) {
            s->next_flush = NULL;
        }
    } else if (s->cur_write && *unfinished) {
        req = s->cur_write;
    } else {
        return false;
    }

    *offset = req->offset;
    *bytes = req->bytes;
    *buf = req->buf;

    return true;
}

/*
 * Find corresponding cluster and drop it. No matter does requested @offset is
 * cached itself or not.
 */
void seqcache_discard_cluster(SeqCache *s, int64_t offset)
{
    Cluster *cl = seqcache_find_cluster(s, offset);

    if (!cl) {
        return;
    }

    if (cl == s->cur_write) {
        assert(cl != s->next_flush);
        s->cur_write = NULL;
    } else if (cl == s->next_flush) {
        assert(cl != s->cur_write);
        s->next_flush = QSIMPLEQ_NEXT(s->next_flush, entry);
        if (s->next_flush == s->cur_write) {
            s->next_flush = NULL;
        }
    }

    QSIMPLEQ_REMOVE(&s->all, cl, Cluster, entry);
    cluster_free(cl);
    s->nb_clusters--;
}

/* Returns number of cached clusters including unfinished */
uint64_t seqcache_nb_clusters(SeqCache *s)
{
    return s->nb_clusters;
}
