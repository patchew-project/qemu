/*
 * Block layer qmp and info dump related functions
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "block/bdrv_info.h"
#include "block/throttle-groups.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-core.h"
#include "sysemu/block-backend.h"

/* @p_info will be set only on success. */
static void bdrv_query_info(BlockBackend *blk, BlockInfo **p_info,
                            Error **errp)
{
    BlockInfo *info = g_malloc0(sizeof(*info));
    BlockDriverState *bs = blk_bs(blk);
    char *qdev;

    /* Skip automatically inserted nodes that the user isn't aware of */
    while (bs && bs->drv && bs->implicit) {
        bs = backing_bs(bs);
    }

    info->device = g_strdup(blk_name(blk));
    info->type = g_strdup("unknown");
    info->locked = blk_dev_is_medium_locked(blk);
    info->removable = blk_dev_has_removable_media(blk);

    qdev = blk_get_attached_dev_id(blk);
    if (qdev && *qdev) {
        info->has_qdev = true;
        info->qdev = qdev;
    } else {
        g_free(qdev);
    }

    if (blk_dev_has_tray(blk)) {
        info->has_tray_open = true;
        info->tray_open = blk_dev_is_tray_open(blk);
    }

    if (blk_iostatus_is_enabled(blk)) {
        info->has_io_status = true;
        info->io_status = blk_iostatus(blk);
    }

    if (bs && !QLIST_EMPTY(&bs->dirty_bitmaps)) {
        info->has_dirty_bitmaps = true;
        info->dirty_bitmaps = bdrv_query_dirty_bitmaps(bs);
    }

    if (bs && bs->drv) {
        info->has_inserted = true;
        info->inserted = bdrv_block_device_info(blk, bs, errp);
        if (info->inserted == NULL) {
            goto err;
        }
    }

    *p_info = info;
    return;

 err:
    qapi_free_BlockInfo(info);
}

static uint64List *uint64_list(uint64_t *list, int size)
{
    int i;
    uint64List *out_list = NULL;
    uint64List **pout_list = &out_list;

    for (i = 0; i < size; i++) {
        uint64List *entry = g_new(uint64List, 1);
        entry->value = list[i];
        *pout_list = entry;
        pout_list = &entry->next;
    }

    *pout_list = NULL;

    return out_list;
}

static void bdrv_latency_histogram_stats(BlockLatencyHistogram *hist,
                                         bool *not_null,
                                         BlockLatencyHistogramInfo **info)
{
    *not_null = hist->bins != NULL;
    if (*not_null) {
        *info = g_new0(BlockLatencyHistogramInfo, 1);

        (*info)->boundaries = uint64_list(hist->boundaries, hist->nbins - 1);
        (*info)->bins = uint64_list(hist->bins, hist->nbins);
    }
}

static void bdrv_query_blk_stats(BlockDeviceStats *ds, BlockBackend *blk)
{
    BlockAcctStats *stats = blk_get_stats(blk);
    BlockAcctTimedStats *ts = NULL;

    ds->rd_bytes = stats->nr_bytes[BLOCK_ACCT_READ];
    ds->wr_bytes = stats->nr_bytes[BLOCK_ACCT_WRITE];
    ds->rd_operations = stats->nr_ops[BLOCK_ACCT_READ];
    ds->wr_operations = stats->nr_ops[BLOCK_ACCT_WRITE];

    ds->failed_rd_operations = stats->failed_ops[BLOCK_ACCT_READ];
    ds->failed_wr_operations = stats->failed_ops[BLOCK_ACCT_WRITE];
    ds->failed_flush_operations = stats->failed_ops[BLOCK_ACCT_FLUSH];

    ds->invalid_rd_operations = stats->invalid_ops[BLOCK_ACCT_READ];
    ds->invalid_wr_operations = stats->invalid_ops[BLOCK_ACCT_WRITE];
    ds->invalid_flush_operations =
        stats->invalid_ops[BLOCK_ACCT_FLUSH];

    ds->rd_merged = stats->merged[BLOCK_ACCT_READ];
    ds->wr_merged = stats->merged[BLOCK_ACCT_WRITE];
    ds->flush_operations = stats->nr_ops[BLOCK_ACCT_FLUSH];
    ds->wr_total_time_ns = stats->total_time_ns[BLOCK_ACCT_WRITE];
    ds->rd_total_time_ns = stats->total_time_ns[BLOCK_ACCT_READ];
    ds->flush_total_time_ns = stats->total_time_ns[BLOCK_ACCT_FLUSH];

    ds->has_idle_time_ns = stats->last_access_time_ns > 0;
    if (ds->has_idle_time_ns) {
        ds->idle_time_ns = block_acct_idle_time_ns(stats);
    }

    ds->account_invalid = stats->account_invalid;
    ds->account_failed = stats->account_failed;

    while ((ts = block_acct_interval_next(stats, ts))) {
        BlockDeviceTimedStatsList *timed_stats =
            g_malloc0(sizeof(*timed_stats));
        BlockDeviceTimedStats *dev_stats = g_malloc0(sizeof(*dev_stats));
        timed_stats->next = ds->timed_stats;
        timed_stats->value = dev_stats;
        ds->timed_stats = timed_stats;

        TimedAverage *rd = &ts->latency[BLOCK_ACCT_READ];
        TimedAverage *wr = &ts->latency[BLOCK_ACCT_WRITE];
        TimedAverage *fl = &ts->latency[BLOCK_ACCT_FLUSH];

        dev_stats->interval_length = ts->interval_length;

        dev_stats->min_rd_latency_ns = timed_average_min(rd);
        dev_stats->max_rd_latency_ns = timed_average_max(rd);
        dev_stats->avg_rd_latency_ns = timed_average_avg(rd);

        dev_stats->min_wr_latency_ns = timed_average_min(wr);
        dev_stats->max_wr_latency_ns = timed_average_max(wr);
        dev_stats->avg_wr_latency_ns = timed_average_avg(wr);

        dev_stats->min_flush_latency_ns = timed_average_min(fl);
        dev_stats->max_flush_latency_ns = timed_average_max(fl);
        dev_stats->avg_flush_latency_ns = timed_average_avg(fl);

        dev_stats->avg_rd_queue_depth =
            block_acct_queue_depth(ts, BLOCK_ACCT_READ);
        dev_stats->avg_wr_queue_depth =
            block_acct_queue_depth(ts, BLOCK_ACCT_WRITE);
    }

    bdrv_latency_histogram_stats(&stats->latency_histogram[BLOCK_ACCT_READ],
                                 &ds->has_x_rd_latency_histogram,
                                 &ds->x_rd_latency_histogram);
    bdrv_latency_histogram_stats(&stats->latency_histogram[BLOCK_ACCT_WRITE],
                                 &ds->has_x_wr_latency_histogram,
                                 &ds->x_wr_latency_histogram);
    bdrv_latency_histogram_stats(&stats->latency_histogram[BLOCK_ACCT_FLUSH],
                                 &ds->has_x_flush_latency_histogram,
                                 &ds->x_flush_latency_histogram);
}

static BlockStats *bdrv_query_bds_stats(BlockDriverState *bs,
                                        bool blk_level)
{
    BlockStats *s = NULL;

    s = g_malloc0(sizeof(*s));
    s->stats = g_malloc0(sizeof(*s->stats));

    if (!bs) {
        return s;
    }

    /* Skip automatically inserted nodes that the user isn't aware of in
     * a BlockBackend-level command. Stay at the exact node for a node-level
     * command. */
    while (blk_level && bs->drv && bs->implicit) {
        bs = backing_bs(bs);
        assert(bs);
    }

    if (bdrv_get_node_name(bs)[0]) {
        s->has_node_name = true;
        s->node_name = g_strdup(bdrv_get_node_name(bs));
    }

    s->stats->wr_highest_offset = stat64_get(&bs->wr_highest_offset);

    if (bs->file) {
        s->has_parent = true;
        s->parent = bdrv_query_bds_stats(bs->file->bs, blk_level);
    }

    if (blk_level && bs->backing) {
        s->has_backing = true;
        s->backing = bdrv_query_bds_stats(bs->backing->bs, blk_level);
    }

    return s;
}

BlockInfoList *qmp_query_block(Error **errp)
{
    BlockInfoList *head = NULL, **p_next = &head;
    BlockBackend *blk;
    Error *local_err = NULL;

    for (blk = blk_all_next(NULL); blk; blk = blk_all_next(blk)) {
        BlockInfoList *info;

        if (!*blk_name(blk) && !blk_get_attached_dev(blk)) {
            continue;
        }

        info = g_malloc0(sizeof(*info));
        bdrv_query_info(blk, &info->value, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            g_free(info);
            qapi_free_BlockInfoList(head);
            return NULL;
        }

        *p_next = info;
        p_next = &info->next;
    }

    return head;
}

BlockStatsList *qmp_query_blockstats(bool has_query_nodes,
                                     bool query_nodes,
                                     Error **errp)
{
    BlockStatsList *head = NULL, **p_next = &head;
    BlockBackend *blk;
    BlockDriverState *bs;

    /* Just to be safe if query_nodes is not always initialized */
    if (has_query_nodes && query_nodes) {
        for (bs = bdrv_next_node(NULL); bs; bs = bdrv_next_node(bs)) {
            BlockStatsList *info = g_malloc0(sizeof(*info));
            AioContext *ctx = bdrv_get_aio_context(bs);

            aio_context_acquire(ctx);
            info->value = bdrv_query_bds_stats(bs, false);
            aio_context_release(ctx);

            *p_next = info;
            p_next = &info->next;
        }
    } else {
        for (blk = blk_all_next(NULL); blk; blk = blk_all_next(blk)) {
            BlockStatsList *info;
            AioContext *ctx = blk_get_aio_context(blk);
            BlockStats *s;
            char *qdev;

            if (!*blk_name(blk) && !blk_get_attached_dev(blk)) {
                continue;
            }

            aio_context_acquire(ctx);
            s = bdrv_query_bds_stats(blk_bs(blk), true);
            s->has_device = true;
            s->device = g_strdup(blk_name(blk));

            qdev = blk_get_attached_dev_id(blk);
            if (qdev && *qdev) {
                s->has_qdev = true;
                s->qdev = qdev;
            } else {
                g_free(qdev);
            }

            bdrv_query_blk_stats(s->stats, blk);
            aio_context_release(ctx);

            info = g_malloc0(sizeof(*info));
            info->value = s;
            *p_next = info;
            p_next = &info->next;
        }
    }

    return head;
}

