/*
 * Image streaming
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "block/block_int.h"
#include "block/blockjob_int.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "sysemu/block-backend.h"

enum {
    /*
     * Size of data buffer for populating the image file.  This should be large
     * enough to process multiple clusters in a single call, so that populating
     * contiguous regions of the image is efficient.
     */
    STREAM_BUFFER_SIZE = 512 * 1024, /* in bytes */
};

typedef struct StreamBlockJob {
    BlockJob common;
    BlockDriverState *base;
    BlockdevOnError on_error;
    char *backing_file_str;
    int bs_flags;
    bool discard;
    BlockDriverState *stream_top_bs;
    GSList *im_nodes;
} StreamBlockJob;

typedef struct IntermediateNode {
    BlockBackend *blk;
    int flags;
} IntermediateNode;

static inline void restore_all_im_nodes(StreamBlockJob *s)
{
    GSList *l;
    BlockDriverState *bs_active;
    BlockDriverState *bs_im;
    IntermediateNode *im_node;
    BlockReopenQueue *queue = NULL;
    Error *local_err = NULL;

    assert(s->stream_top_bs && s->stream_top_bs->backing &&
           s->stream_top_bs->backing->bs);
    bs_active = backing_bs(s->stream_top_bs);
    assert(backing_bs(bs_active));

    bdrv_subtree_drained_begin(backing_bs(bs_active));

    for (l = s->im_nodes; l; l = l->next) {
        im_node = l->data;
        if (im_node->blk) {
            bs_im = blk_bs(im_node->blk);

            if (im_node->flags != bdrv_get_flags(bs_im) && bs_im) {
                queue = bdrv_reopen_queue(queue, bs_im, NULL, im_node->flags);
            }
            /* Give up write permissions before making it read-only */
            blk_set_perm(im_node->blk, 0, BLK_PERM_ALL, &error_abort);
            blk_unref(im_node->blk);
            bdrv_unref(bs_im);
        }
        g_free(im_node);
    }
    g_slist_free(s->im_nodes);
    s->im_nodes = NULL;

    if (queue) {
        bdrv_reopen_multiple(bdrv_get_aio_context(bs_active), queue,
                             &local_err);
        if (local_err != NULL) {
            error_report_err(local_err);
        }
    }

    bdrv_subtree_drained_end(backing_bs(bs_active));
}

static int coroutine_fn stream_populate(const StreamBlockJob *s,
                                        int64_t offset, uint64_t bytes,
                                        void *buf)
{
    struct iovec iov = {
        .iov_base = buf,
        .iov_len  = bytes,
    };
    QEMUIOVector qiov;
    GSList *l;
    IntermediateNode *im_node;
    int ret;

    assert(s);
    assert(bytes < SIZE_MAX);
    qemu_iovec_init_external(&qiov, &iov, 1);

    /* Copy-on-read the unallocated clusters */
    ret = blk_co_preadv(s->common.blk, offset, qiov.size, &qiov,
                        BDRV_REQ_COPY_ON_READ);

    if (ret < 0 || !s->discard) {
        return ret;
    }

    for (l = s->im_nodes; l; l = l->next) {
        im_node = l->data;
        blk_co_pdiscard(im_node->blk, offset, bytes);
    }

    return ret;
}

static int stream_exit_discard(StreamBlockJob *s)
{
    BlockJob *bjob = &s->common;
    BlockDriverState *bs = backing_bs(s->stream_top_bs);
    BlockDriverState *base = s->base;
    Error *local_err = NULL;
    int ret = 0;

    /* Make sure that the BDS doesn't go away during bdrv_replace_node,
     * before we can call bdrv_drained_end */
    bdrv_ref(s->stream_top_bs);
    /* Reopen intermediate images back in read-only mode */
    restore_all_im_nodes(s);
    /* Hold a guest back from writing until we remove the filter */
    bdrv_drained_begin(bs);
    /* Dropping WRITE is required before changing the backing file. */
    bdrv_child_try_set_perm(s->stream_top_bs->backing, 0, BLK_PERM_ALL,
                            &error_abort);
    if (bs->backing) {
        const char *base_id = NULL, *base_fmt = NULL;
        if (base) {
            base_id = s->backing_file_str;
            if (base->drv) {
                base_fmt = base->drv->format_name;
            }
        }
        ret = bdrv_change_backing_file(bs, base_id, base_fmt);
        bdrv_set_backing_hd(bs, base, &local_err);
        if (local_err) {
            error_report_err(local_err);
            ret = -EPERM;
        }
    }
    /* Remove the filter driver from the graph. Before this, get rid of
     * the blockers on the intermediate nodes so that the resulting state is
     * valid. Also give up permissions on stream_top_bs->backing, which might
     * block the removal. */
    block_job_remove_all_bdrv(bjob);
    bdrv_child_try_set_perm(s->stream_top_bs->backing, 0, BLK_PERM_ALL,
                            &error_abort);
    bdrv_replace_node(s->stream_top_bs, backing_bs(s->stream_top_bs),
                      &error_abort);
    /* We just changed the BDS the job BB refers to (with either or both of the
     * bdrv_replace_node() calls), so switch the BB back so the cleanup does
     * the right thing. We don't need any permissions any more now. */
    blk_remove_bs(bjob->blk);
    blk_set_perm(bjob->blk, 0, BLK_PERM_ALL, &error_abort);
    blk_insert_bs(bjob->blk, s->stream_top_bs, &error_abort);

    bdrv_drained_end(bs);
    bdrv_unref(s->stream_top_bs);

    return ret;
}

static int stream_prepare(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockJob *bjob = &s->common;
    BlockDriverState *bs = blk_bs(bjob->blk);
    BlockDriverState *base = s->base;
    Error *local_err = NULL;
    int ret = 0;

    if (s->discard) {
        return stream_exit_discard(s);
    }

    if (bs->backing) {
        const char *base_id = NULL, *base_fmt = NULL;
        if (base) {
            base_id = s->backing_file_str;
            if (base->drv) {
                base_fmt = base->drv->format_name;
            }
        }
        ret = bdrv_change_backing_file(bs, base_id, base_fmt);
        bdrv_set_backing_hd(bs, base, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -EPERM;
        }
    }

    return ret;
}

static void stream_clean(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockJob *bjob = &s->common;
    BlockDriverState *bs = blk_bs(bjob->blk);

    /* Reopen the image back in read-only mode if necessary */
    if (s->bs_flags != bdrv_get_flags(bs)) {
        /* Give up write permissions before making it read-only */
        blk_set_perm(bjob->blk, 0, BLK_PERM_ALL, &error_abort);
        bdrv_reopen(bs, s->bs_flags, NULL);
    }

    g_free(s->backing_file_str);
}

static int coroutine_fn stream_run(Job *job, Error **errp)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockBackend *blk = s->common.blk;
    BlockDriverState *bs;
    BlockDriverState *base = s->base;
    int64_t len;
    int64_t offset = 0;
    uint64_t delay_ns = 0;
    int error = 0;
    int ret = 0;
    int64_t n = 0; /* bytes */
    void *buf;

    if (s->discard) {
        bs = backing_bs(s->stream_top_bs);
    } else {
        bs = blk_bs(blk);
    }

    if (!bs->backing) {
        goto out;
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        ret = len;
        goto out;
    }
    job_progress_set_remaining(&s->common.job, len);

    buf = qemu_blockalign(bs, STREAM_BUFFER_SIZE);

    /* Turn on copy-on-read for the whole block device so that guest read
     * requests help us make progress.  Only do this when copying the entire
     * backing chain since the copy-on-read operation does not take base into
     * account.
     */
    if (!base) {
        bdrv_enable_copy_on_read(bs);
    }

    for ( ; offset < len; offset += n) {
        bool copy;

        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that bdrv_drain_all() returns.
         */
        job_sleep_ns(&s->common.job, delay_ns);
        if (job_is_cancelled(&s->common.job)) {
            break;
        }

        copy = false;

        ret = bdrv_is_allocated(bs, offset, STREAM_BUFFER_SIZE, &n);
        if (ret == 1) {
            /* Allocated in the top, no need to copy.  */
        } else if (ret >= 0) {
            /* Copy if allocated in the intermediate images.  Limit to the
             * known-unallocated area [offset, offset+n*BDRV_SECTOR_SIZE).  */
            ret = bdrv_is_allocated_above(backing_bs(bs), base,
                                          offset, n, &n);

            /* Finish early if end of backing file has been reached */
            if (ret == 0 && n == 0) {
                n = len - offset;
            }

            copy = (ret == 1);
        }
        trace_stream_one_iteration(s, offset, n, ret);
        if (copy) {
            ret = stream_populate(s, offset, n, buf);
        }
        if (ret < 0) {
            BlockErrorAction action =
                block_job_error_action(&s->common, s->on_error, true, -ret);
            if (action == BLOCK_ERROR_ACTION_STOP) {
                n = 0;
                continue;
            }
            if (error == 0) {
                error = ret;
            }
            if (action == BLOCK_ERROR_ACTION_REPORT) {
                break;
            }
        }
        ret = 0;

        /* Publish progress */
        job_progress_update(&s->common.job, n);
        if (copy) {
            delay_ns = block_job_ratelimit_get_delay(&s->common, n);
        } else {
            delay_ns = 0;
        }
    }

    if (!base) {
        bdrv_disable_copy_on_read(bs);
    }

    /* Do not remove the backing file if an error was there but ignored.  */
    ret = error;

    qemu_vfree(buf);

out:
    /* Modify backing chain and close BDSes in main loop */
    return ret;
}

static int coroutine_fn bdrv_stream_top_preadv(BlockDriverState *bs,
    uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static int coroutine_fn bdrv_stream_top_pwritev(BlockDriverState *bs,
    uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    return bdrv_co_pwritev(bs->backing, offset, bytes, qiov, flags);
}

static int coroutine_fn bdrv_stream_top_flush(BlockDriverState *bs)
{
    if (bs->backing == NULL) {
        /* we can be here after failed bdrv_append in stream_start */
        return 0;
    }
    return bdrv_co_flush(bs->backing->bs);
}

static int coroutine_fn bdrv_stream_top_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int bytes, BdrvRequestFlags flags)
{
    return bdrv_co_pwrite_zeroes(bs->backing, offset, bytes, flags);
}

static int coroutine_fn bdrv_stream_top_pdiscard(BlockDriverState *bs,
    int64_t offset, int bytes)
{
    return bdrv_co_pdiscard(bs->backing, offset, bytes);
}

static int bdrv_stream_top_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return bdrv_get_info(bs->backing->bs, bdi);
}

static void bdrv_stream_top_refresh_filename(BlockDriverState *bs, QDict *opts)
{
    if (bs->backing == NULL) {
        /* we can be here after failed bdrv_attach_child in
         * bdrv_set_backing_hd */
        return;
    }
    bdrv_refresh_filename(bs->backing->bs);
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void bdrv_stream_top_child_perm(BlockDriverState *bs, BdrvChild *c,
                                       const BdrvChildRole *role,
                                       BlockReopenQueue *reopen_queue,
                                       uint64_t perm, uint64_t shared,
                                       uint64_t *nperm, uint64_t *nshared)
{
    /* Must be able to forward guest writes to the real image */
    *nperm = 0;
    if (perm & BLK_PERM_WRITE) {
        *nperm |= BLK_PERM_WRITE;
    }

    *nshared = BLK_PERM_ALL;
}

/* Dummy node that provides consistent read to its users without requiring it
 * from its backing file and that allows writes on the backing file chain. */
static BlockDriver bdrv_stream_top = {
    .format_name                = "stream_top",
    .bdrv_co_preadv             = bdrv_stream_top_preadv,
    .bdrv_co_pwritev            = bdrv_stream_top_pwritev,
    .bdrv_co_pwrite_zeroes      = bdrv_stream_top_pwrite_zeroes,
    .bdrv_co_pdiscard           = bdrv_stream_top_pdiscard,
    .bdrv_get_info              = bdrv_stream_top_get_info,
    .bdrv_co_flush              = bdrv_stream_top_flush,
    .bdrv_co_block_status       = bdrv_co_block_status_from_backing,
    .bdrv_refresh_filename      = bdrv_stream_top_refresh_filename,
    .bdrv_child_perm            = bdrv_stream_top_child_perm,
};

/* In the case of block discard, add a dummy driver
 * to make the backing chain writable. */
static BlockDriverState *insert_filter(BlockDriverState *bs, Error **errp)
{
    const char *filter_node_name = NULL;
    BlockDriverState *stream_top_bs;
    Error *local_err = NULL;

    stream_top_bs = bdrv_new_open_driver(&bdrv_stream_top, filter_node_name,
                                         BDRV_O_RDWR, errp);
    if (stream_top_bs == NULL) {
        return NULL;
    }
    if (!filter_node_name) {
        stream_top_bs->implicit = true;
    }

    stream_top_bs->total_sectors = bs->total_sectors;
    stream_top_bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED;
    stream_top_bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED;
    bdrv_set_aio_context(stream_top_bs, bdrv_get_aio_context(bs));

    /* bdrv_append takes ownership of the stream_top_bs reference, need to keep
     * it alive until block_job_create() succeeds even if bs has no parent. */
    bdrv_ref(stream_top_bs);
    bdrv_drained_begin(bs);
    bdrv_append(stream_top_bs, bs, &local_err);
    bdrv_drained_end(bs);

    if (local_err) {
        bdrv_unref(stream_top_bs);
        error_propagate(errp, local_err);
        return NULL;
    }

    return stream_top_bs;
}

/* Makes intermediate block chain writable */
static int init_intermediate_nodes(StreamBlockJob *s,
                                   BlockDriverState *bs,
                                   BlockDriverState *base, Error **errp)
{
    BlockDriverState *iter;
    int backing_bs_flags;
    IntermediateNode *im_node;
    BlockBackend *blk;
    BlockReopenQueue *queue = NULL;
    Error *local_err = NULL;
    int ret;

    /* Sanity check */
    if (!backing_bs(bs)) {
        error_setg(errp, "Top BDS does not have a backing file.");
        return -EINVAL;
    }
    if (base && !bdrv_chain_contains(bs, base)) {
        error_setg(errp, "The backing chain does not contain the base file.");
        return -EINVAL;
    }

    /* Reopen intermediate images in read-write mode */
    bdrv_subtree_drained_begin(backing_bs(bs));

    for (iter = backing_bs(bs); iter && iter != base; iter = backing_bs(iter)) {
        /* Keep the intermediate backing chain with BDRV original flags */
        backing_bs_flags = bdrv_get_flags(iter);
        im_node = g_new0(IntermediateNode, 1);
        im_node->blk = NULL;
        im_node->flags = backing_bs_flags;
        bdrv_ref(iter);
        s->im_nodes = g_slist_prepend(s->im_nodes, im_node);

        if ((backing_bs_flags & BDRV_O_RDWR) == 0) {
            queue = bdrv_reopen_queue(queue, iter, NULL,
                                      backing_bs_flags | BDRV_O_RDWR);
        }
    }

    if (queue) {
        ret = bdrv_reopen_multiple(bdrv_get_aio_context(bs), queue, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            bdrv_subtree_drained_end(backing_bs(bs));
            restore_all_im_nodes(s);
            return -1;
        }
    }

    bdrv_subtree_drained_end(backing_bs(bs));

    s->im_nodes = g_slist_reverse(s->im_nodes);
    GSList *l = s->im_nodes;

    for (iter = backing_bs(bs); iter && iter != base; iter = backing_bs(iter)) {
        blk = blk_new(BLK_PERM_WRITE, BLK_PERM_CONSISTENT_READ |
                      BLK_PERM_WRITE | BLK_PERM_WRITE_UNCHANGED |
                      BLK_PERM_GRAPH_MOD);
        if (!blk) {
            error_setg(errp,
                       "Block Stream: failed to create new Block Backend.");
            goto fail;
        }

        ret = blk_insert_bs(blk, iter, errp);
        if (ret < 0) {
            goto fail;
        }

        assert(l);
        im_node = l->data;
        im_node->blk = blk;
        l = l->next;
    }

    return 0;

fail:
    restore_all_im_nodes(s);

    return -1;
}

static const BlockJobDriver stream_job_driver = {
    .job_driver = {
        .instance_size = sizeof(StreamBlockJob),
        .job_type      = JOB_TYPE_STREAM,
        .free          = block_job_free,
        .run           = stream_run,
        .prepare       = stream_prepare,
        .clean         = stream_clean,
        .user_resume   = block_job_user_resume,
        .drain         = block_job_drain,
    },
};

void stream_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, const char *backing_file_str,
                  int creation_flags, int64_t speed, bool discard,
                  BlockdevOnError on_error, Error **errp)
{
    StreamBlockJob *s = NULL;
    BlockDriverState *iter;
    int orig_bs_flags;
    BlockDriverState *stream_top_bs;
    int node_shared_flags = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED;
    int ret;

    /* Make sure that the image is opened in read-write mode */
    orig_bs_flags = bdrv_get_flags(bs);
    if (!(orig_bs_flags & BDRV_O_RDWR)) {
        if (bdrv_reopen(bs, orig_bs_flags | BDRV_O_RDWR, errp) != 0) {
            return;
        }
    }

    if (discard) {
        node_shared_flags |= BLK_PERM_WRITE;
        stream_top_bs = insert_filter(bs, errp);
        if (stream_top_bs == NULL) {
            goto fail;
        }
    } else {
        stream_top_bs = bs;
    }
    /* Prevent concurrent jobs trying to modify the graph structure here, we
     * already have our own plans. Also don't allow resize as the image size is
     * queried only at the job start and then cached. */
    s = block_job_create(job_id, &stream_job_driver, NULL, stream_top_bs,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_GRAPH_MOD,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_WRITE,
                         speed, creation_flags, NULL, NULL, errp);
    if (!s) {
        goto fail;
    }

    /* Block all intermediate nodes between bs and base, because they will
     * disappear from the chain after this operation. The streaming job reads
     * every block only once, assuming that it doesn't change, so forbid writes
     * and resizes. Allow writing in case of discard. */
    for (iter = backing_bs(bs); iter && iter != base; iter = backing_bs(iter)) {
        block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                           node_shared_flags, &error_abort);
    }

    if (discard) {
        s->stream_top_bs = stream_top_bs;
        /* The block job now has a reference to this node */
        bdrv_unref(stream_top_bs);

        ret = init_intermediate_nodes(s, bs, base, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    s->base = base;
    s->backing_file_str = g_strdup(backing_file_str);
    s->bs_flags = orig_bs_flags;
    s->discard = discard;
    s->on_error = on_error;
    trace_stream_start(bs, base, s);
    job_start(&s->common.job);
    return;

fail:
    if (orig_bs_flags != bdrv_get_flags(bs)) {
        bdrv_reopen(bs, orig_bs_flags, NULL);
    }
    if (!discard) {
        return;
    }
    if (s) {
        /* Make sure this BDS does not go away until we have completed the graph
         * changes below */
        bdrv_ref(stream_top_bs);
        job_early_fail(&s->common.job);
    }
    if (stream_top_bs) {
        bdrv_drained_begin(bs);
        bdrv_child_try_set_perm(stream_top_bs->backing, 0, BLK_PERM_ALL,
                                &error_abort);
        bdrv_replace_node(stream_top_bs, backing_bs(stream_top_bs),
                          &error_abort);
        bdrv_drained_end(bs);
        bdrv_unref(stream_top_bs);
    }
}
