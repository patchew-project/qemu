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
#include "qapi/qmp/qdict.h"
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
    bool bs_read_only;
    BlockDriverState *cor_filter_bs;
    bool discard;
    GSList *im_nodes;
} StreamBlockJob;

typedef struct IntermediateNode {
    BlockBackend *blk;
    bool bs_read_only;
} IntermediateNode;

static BlockDriverState *child_file_bs(BlockDriverState *bs)
{
    return bs->file ? bs->file->bs : NULL;
}

static void restore_all_im_nodes(StreamBlockJob *s)
{
    GSList *l;
    BlockDriverState *bs_active;
    BlockDriverState *bs_im;
    IntermediateNode *im_node;
    QDict *opts;
    BlockReopenQueue *queue = NULL;
    Error *local_err = NULL;

    assert(s->cor_filter_bs);
    bs_active = child_file_bs(s->cor_filter_bs);
    assert(bs_active && backing_bs(bs_active));

    bdrv_subtree_drained_begin(backing_bs(bs_active));

    for (l = s->im_nodes; l; l = l->next) {
        im_node = l->data;
        if (im_node->blk) {
            bs_im = blk_bs(im_node->blk);

            if (im_node->bs_read_only && bs_im && !bdrv_is_read_only(bs_im)) {
                opts = qdict_new();
                qdict_put_bool(opts, BDRV_OPT_READ_ONLY, true);
                queue = bdrv_reopen_queue(queue, bs_im, opts);
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

static int stream_change_backing_file(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockJob *bjob = &s->common;
    BlockDriverState *bs = blk_bs(bjob->blk);
    BlockDriverState *base = s->base;
    Error *local_err = NULL;
    int ret = 0;

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

static void remove_filter(BlockDriverState *cor_filter_bs)
{
    BlockDriverState *bs = child_file_bs(cor_filter_bs);

    /* Hold a guest back from writing until we remove the filter */
    bdrv_drained_begin(bs);
    bdrv_child_try_set_perm(cor_filter_bs->file, 0, BLK_PERM_ALL,
                            &error_abort);
    bdrv_replace_node(cor_filter_bs, bs, &error_abort);
    bdrv_drained_end(bs);

    bdrv_unref(cor_filter_bs);
}

static void stream_exit(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    if (s->cor_filter_bs == NULL) {
        return;
    }
    /* Reopen intermediate images back in read-only mode */
    restore_all_im_nodes(s);
    /* Remove the filter driver from the graph */
    remove_filter(s->cor_filter_bs);
    s->cor_filter_bs = NULL;
}

static int stream_prepare(Job *job)
{
    stream_exit(job);

    return stream_change_backing_file(job);
}

static void stream_abort(Job *job)
{
    stream_exit(job);
}

static void stream_clean(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockJob *bjob = &s->common;
    BlockDriverState *bs = blk_bs(bjob->blk);

    /* Reopen the image back in read-only mode if necessary */
    if (s->bs_read_only) {
        /* Give up write permissions before making it read-only */
        blk_set_perm(bjob->blk, 0, BLK_PERM_ALL, &error_abort);
        bdrv_reopen_set_read_only(bs, true, NULL);
    }

    g_free(s->backing_file_str);
}

static int coroutine_fn stream_run(Job *job, Error **errp)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockDriverState *bs = child_file_bs(s->cor_filter_bs);
    BlockDriverState *base = s->base;
    int64_t len;
    int64_t offset = 0;
    uint64_t delay_ns = 0;
    int error = 0;
    int ret = 0;
    int64_t n = 0; /* bytes */
    void *buf;

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

static BlockDriverState *create_filter_node(BlockDriverState *bs, bool discard,
                                            Error **errp)
{
    QDict *opts = qdict_new();

    qdict_put_str(opts, "driver", "copy-on-read");
    qdict_put_str(opts, "file", bdrv_get_node_name(bs));
    if (discard) {
        qdict_put_bool(opts, "driver.discard", true);
    }

    return bdrv_open(NULL, NULL, opts, BDRV_O_RDWR, errp);
}

static BlockDriverState *insert_filter(BlockDriverState *bs, bool discard,
                                       Error **errp)
{
    BlockDriverState *cor_filter_bs;
    Error *local_err = NULL;

    cor_filter_bs = create_filter_node(bs, discard, errp);
    if (cor_filter_bs == NULL) {
        error_prepend(errp, "Could not create filter node: ");
        return NULL;
    }

    bdrv_set_aio_context(cor_filter_bs, bdrv_get_aio_context(bs));

    bdrv_drained_begin(bs);
    bdrv_replace_node(bs, cor_filter_bs, &local_err);
    bdrv_drained_end(bs);

    if (local_err) {
        bdrv_unref(cor_filter_bs);
        error_propagate(errp, local_err);
        return NULL;
    }

    return cor_filter_bs;
}

/* Makes intermediate block chain writable */
static int init_intermediate_nodes(StreamBlockJob *s,
                                   BlockDriverState *bs,
                                   BlockDriverState *base, Error **errp)
{
    BlockDriverState *iter;
    bool bs_read_only;
    IntermediateNode *im_node;
    BlockBackend *blk;
    QDict *opts;
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
        bs_read_only = bdrv_is_read_only(iter);
        im_node = g_new0(IntermediateNode, 1);
        im_node->blk = NULL;
        im_node->bs_read_only = bs_read_only;
        bdrv_ref(iter);
        s->im_nodes = g_slist_prepend(s->im_nodes, im_node);

        if (bs_read_only) {
            opts = qdict_new();
            qdict_put_bool(opts, BDRV_OPT_READ_ONLY, false);
            queue = bdrv_reopen_queue(queue, iter, opts);
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
        .abort         = stream_abort,
        .clean         = stream_clean,
        .user_resume   = block_job_user_resume,
        .drain         = block_job_drain,
    },
};

void stream_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, const char *backing_file_str,
                  int creation_flags, int64_t speed,
                  BlockdevOnError on_error, Error **errp)
{
    StreamBlockJob *s;
    BlockDriverState *iter;
    bool bs_read_only;
    const bool discard = false;
    int node_shared_flags = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED;
    int ret;

    /* Make sure that the image is opened in read-write mode */
    bs_read_only = bdrv_is_read_only(bs);
    if (bs_read_only) {
        if (bdrv_reopen_set_read_only(bs, false, errp) != 0) {
            return;
        }
    }

    /* Prevent concurrent jobs trying to modify the graph structure here, we
     * already have our own plans. Also don't allow resize as the image size is
     * queried only at the job start and then cached. */
    s = block_job_create(job_id, &stream_job_driver, NULL, bs,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_GRAPH_MOD,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_WRITE,
                         speed, creation_flags, NULL, NULL, errp);
    if (!s) {
        goto fail;
    }

    /*
     * Block all intermediate nodes between bs and base, because they will
     * disappear from the chain after this operation. The streaming job reads
     * every block only once, assuming that it doesn't change, so forbid writes
     * and resizes. Allow writing in case of discard.
     */
    if (discard) {
        node_shared_flags |= BLK_PERM_WRITE;
    }
    for (iter = backing_bs(bs); iter && iter != base; iter = backing_bs(iter)) {
        block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                           node_shared_flags,
                           &error_abort);
    }

    s->cor_filter_bs = insert_filter(bs, discard, errp);
    if (s->cor_filter_bs == NULL) {
        goto fail;
    }

    if (discard) {
        ret = init_intermediate_nodes(s, bs, base, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    s->discard = discard;
    s->base = base;
    s->backing_file_str = g_strdup(backing_file_str);
    s->bs_read_only = bs_read_only;

    s->on_error = on_error;
    trace_stream_start(bs, base, s);
    job_start(&s->common.job);
    return;

fail:
    if (s && s->cor_filter_bs) {
        remove_filter(s->cor_filter_bs);
        job_early_fail(&s->common.job);
    }
    if (bs_read_only) {
        bdrv_reopen_set_read_only(bs, true, NULL);
    }
}
