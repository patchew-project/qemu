/*
 * Block driver for the QCOW version 2 format
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
#include "qcow2.h"

typedef struct HostCluster {
    uint64_t host_range_refcnt;

    /* For convenience, keep cluster_index here */
    int64_t cluster_index;

    /*
     * Qcow2 refcount of this host cluster is zero. So, when all dynamic users
     * put their references back, we should discard the cluster.
     */
    bool postponed_discard;
    enum qcow2_discard_type postponed_discard_type;
} HostCluster;

void qcow2_init_host_range_refs(BDRVQcow2State *s)
{
    s->host_range_refs =
        g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, g_free);
}

void qcow2_release_host_range_refs(BDRVQcow2State *s)
{
    assert(g_hash_table_size(s->host_range_refs) == 0);
    g_hash_table_unref(s->host_range_refs);
}

static HostCluster *find_host_cluster(BDRVQcow2State *s, int64_t cluster_index)
{
    HostCluster *cl;

    if (!s->host_range_refs) {
        return NULL;
    }

    cl = g_hash_table_lookup(s->host_range_refs, &cluster_index);

    if (cl) {
        assert(cl->host_range_refcnt > 0);
    }

    return cl;
}

uint64_t qcow2_get_host_range_refcnt(BlockDriverState *bs,
                                     int64_t cluster_index)
{
    BDRVQcow2State *s = bs->opaque;
    HostCluster *cl = find_host_cluster(s, cluster_index);

    if (!cl) {
        return 0;
    }

    return cl->host_range_refcnt;
}

/* Inrease host_range_refcnt of clusters intersecting with range */
void coroutine_fn
qcow2_host_range_ref(BlockDriverState *bs, int64_t offset, int64_t length)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t start, last, cluster_index;

    start = start_of_cluster(s, offset) >> s->cluster_bits;
    last = start_of_cluster(s, offset + length - 1) >> s->cluster_bits;
    for (cluster_index = start; cluster_index <= last; cluster_index++) {
        HostCluster *cl = find_host_cluster(s, cluster_index);

        if (!cl) {
            cl = g_new(HostCluster, 1);
            *cl = (HostCluster) {
                .cluster_index = cluster_index,
                .host_range_refcnt = 1,
            };
            g_hash_table_insert(s->host_range_refs,
                                g_memdup(&cluster_index,
                                         sizeof(cluster_index)), cl);
        } else {
            cl->host_range_refcnt++;
        }
        continue;
    }
}

/* Decrease host_range_refcnt of clusters intersecting with range */
void coroutine_fn
qcow2_host_range_unref(BlockDriverState *bs, int64_t offset, int64_t length)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t start, last, cluster_index;

    start = start_of_cluster(s, offset) >> s->cluster_bits;
    last = start_of_cluster(s, offset + length - 1) >> s->cluster_bits;
    for (cluster_index = start; cluster_index <= last; cluster_index++) {
        HostCluster *cl = find_host_cluster(s, cluster_index);

        assert(cl);
        assert(cl->host_range_refcnt >= 1);

        if (cl->host_range_refcnt > 1) {
            cl->host_range_refcnt--;
            continue;
        }

        if (!cl->postponed_discard) {
            g_hash_table_remove(s->host_range_refs, &cluster_index);
            continue;
        }

        /*
         * OK. refcnt become 0 and we should do postponed discard. Let's keep
         * host_range_refcnt = 1 during this final IO operation.
         */
        if (s->discard_passthrough[cl->postponed_discard_type]) {
            int64_t cluster_offset = cluster_index << s->cluster_bits;
            if (s->cache_discards) {
                qcow2_cache_host_discard(bs, cluster_offset, s->cluster_size);
            } else {
                /* Discard is optional, ignore the return value */
                bdrv_pdiscard(bs->file, cluster_offset, s->cluster_size);
            }
        }

        g_hash_table_remove(s->host_range_refs, &cluster_index);

        if (cluster_index < s->free_cluster_index) {
            s->free_cluster_index = cluster_index;
        }
    }
}

bool qcow2_host_cluster_postponed_discard(BlockDriverState *bs,
                                          int64_t cluster_index,
                                          enum qcow2_discard_type type)
{
    BDRVQcow2State *s = bs->opaque;
    HostCluster *cl = find_host_cluster(s, cluster_index);

    if (!cl) {
        return false;
    }

    cl->postponed_discard = true;
    cl->postponed_discard_type = type;

    return true;
}
