/*
 * Async Dirty Bitmap Populator
 *
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 *  John Snow <jsnow@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "trace.h"
#include "block/block.h"
#include "block/block_int.h"
#include "block/blockjob_int.h"
#include "block/block_backup.h"
#include "block/block-copy.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"

typedef struct BitpopBlockJob {
    BlockJob common;
    BlockDriverState *bs;
    BdrvDirtyBitmap *target_bitmap;
    BdrvDirtyBitmap *new_bitmap;
    BlockdevOnError on_error;
    uint64_t len;
} BitpopBlockJob;

static const BlockJobDriver bitpop_job_driver;

static void bitpop_commit(Job *job)
{
    BitpopBlockJob *s = container_of(job, BitpopBlockJob, common.job);

    bdrv_dirty_bitmap_merge_internal(s->target_bitmap, s->new_bitmap,
                                     NULL, true);
}

/* no abort needed; just clean without committing. */

static void bitpop_clean(Job *job)
{
    BitpopBlockJob *s = container_of(job, BitpopBlockJob, common.job);

    bdrv_release_dirty_bitmap(s->new_bitmap);
    bdrv_dirty_bitmap_set_busy(s->target_bitmap, false);
}

static BlockErrorAction bitpop_error_action(BitpopBlockJob *job, int error)
{
    return block_job_error_action(&job->common, job->on_error, true, error);
}

static bool coroutine_fn yield_and_check(Job *job)
{
    if (job_is_cancelled(job)) {
        return true;
    }

    job_sleep_ns(job, 0);

    if (job_is_cancelled(job)) {
        return true;
    }

    return false;
}

static int coroutine_fn bitpop_run(Job *job, Error **errp)
{
    BitpopBlockJob *s = container_of(job, BitpopBlockJob, common.job);
    int ret = 0;
    int64_t offset;
    int64_t count;
    int64_t bytes;

    for (offset = 0; offset < s->len; ) {
        if (yield_and_check(job)) {
            ret = -ECANCELED;
            break;
        }

        bytes = s->len - offset;
        ret = bdrv_is_allocated(s->bs, offset, bytes, &count);
        if (ret < 0) {
            if (bitpop_error_action(s, -ret) == BLOCK_ERROR_ACTION_REPORT) {
                break;
            }
            continue;
        }

        if (!count) {
            ret = 0;
            break;
        }

        if (ret) {
            bdrv_set_dirty_bitmap(s->new_bitmap, offset, count);
            ret = 0;
        }

        job_progress_update(job, count);
        offset += count;
    }

    return ret;
}

static const BlockJobDriver bitpop_job_driver = {
    .job_driver = {
        .instance_size          = sizeof(BitpopBlockJob),
        .job_type               = JOB_TYPE_BITMAP_POPULATE,
        .free                   = block_job_free,
        .user_resume            = block_job_user_resume,
        .run                    = bitpop_run,
        .commit                 = bitpop_commit,
        .clean                  = bitpop_clean,
    }
};


BlockJob *bitpop_job_create(
    const char *job_id,
    BlockDriverState *bs,
    BdrvDirtyBitmap *target_bitmap,
    BitmapPattern pattern,
    BlockdevOnError on_error,
    int creation_flags,
    BlockCompletionFunc *cb,
    void *opaque,
    JobTxn *txn,
    Error **errp)
{
    int64_t len;
    BitpopBlockJob *job = NULL;
    int64_t cluster_size;
    BdrvDirtyBitmap *new_bitmap = NULL;

    assert(bs);
    assert(target_bitmap);

    if (!bdrv_is_inserted(bs)) {
        error_setg(errp, "Device is not inserted: %s",
                   bdrv_get_device_name(bs));
        return NULL;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_BITMAP_POPULATE, errp)) {
        return NULL;
    }

    if (bdrv_dirty_bitmap_check(target_bitmap, BDRV_BITMAP_DEFAULT, errp)) {
        return NULL;
    }

    if (pattern != BITMAP_PATTERN_ALLOCATION_TOP) {
        error_setg(errp, "Unrecognized bitmap pattern");
        return NULL;
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "unable to get length for '%s'",
                         bdrv_get_device_or_node_name(bs));
        return NULL;
    }

    /* NB: new bitmap is anonymous and enabled */
    cluster_size = bdrv_dirty_bitmap_granularity(target_bitmap);
    new_bitmap = bdrv_create_dirty_bitmap(bs, cluster_size, NULL, errp);
    if (!new_bitmap) {
        return NULL;
    }

    /* Take ownership; we reserve the right to write into this on-commit. */
    bdrv_dirty_bitmap_set_busy(target_bitmap, true);

    job = block_job_create(job_id, &bitpop_job_driver, txn, bs,
                           BLK_PERM_CONSISTENT_READ,
                           BLK_PERM_ALL & ~BLK_PERM_RESIZE,
                           0, creation_flags,
                           cb, opaque, errp);
    if (!job) {
        bdrv_dirty_bitmap_set_busy(target_bitmap, false);
        bdrv_release_dirty_bitmap(new_bitmap);
        return NULL;
    }

    job->bs = bs;
    job->on_error = on_error;
    job->target_bitmap = target_bitmap;
    job->new_bitmap = new_bitmap;
    job->len = len;
    job_progress_set_remaining(&job->common.job, job->len);

    return &job->common;
}
