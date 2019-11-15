/*
 * QEMU backup
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2019 Virtuozzo International GmbH.
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
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

#include "block/backup-top.h"

#define BACKUP_CLUSTER_SIZE_DEFAULT (1 << 16)

typedef struct BackupBlockJob {
    BlockJob common;
    BlockDriverState *backup_top;
    BlockDriverState *source_bs;

    BdrvDirtyBitmap *sync_bitmap;

    MirrorSyncMode sync_mode;
    BitmapSyncMode bitmap_mode;
    BlockdevOnError on_source_error;
    BlockdevOnError on_target_error;
    uint64_t len;
    int64_t cluster_size;
    int max_workers;
    int64_t max_chunk;

    BlockCopyState *bcs;
    BdrvDirtyBitmap *bcs_bitmap;

    BlockCopyCallState *bcs_call;
    int ret;
    bool error_is_read;
} BackupBlockJob;

static const BlockJobDriver backup_job_driver;

static void backup_progress_bytes_callback(int64_t bytes, void *opaque)
{
    BackupBlockJob *s = opaque;

    job_progress_update(&s->common.job, bytes);
}

static void backup_progress_reset_callback(void *opaque)
{
    BackupBlockJob *s = opaque;
    uint64_t estimate = bdrv_get_dirty_count(s->bcs_bitmap);

    job_progress_set_remaining(&s->common.job, estimate);
}

static void backup_cleanup_sync_bitmap(BackupBlockJob *job, int ret)
{
    BdrvDirtyBitmap *bm;
    bool sync = (((ret == 0) || (job->bitmap_mode == BITMAP_SYNC_MODE_ALWAYS)) \
                 && (job->bitmap_mode != BITMAP_SYNC_MODE_NEVER));

    if (sync) {
        /*
         * We succeeded, or we always intended to sync the bitmap.
         * Delete this bitmap and install the child.
         */
        bm = bdrv_dirty_bitmap_abdicate(job->sync_bitmap, NULL);
    } else {
        /*
         * We failed, or we never intended to sync the bitmap anyway.
         * Merge the successor back into the parent, keeping all data.
         */
        bm = bdrv_reclaim_dirty_bitmap(job->sync_bitmap, NULL);
    }

    assert(bm);

    if (ret < 0 && job->bitmap_mode == BITMAP_SYNC_MODE_ALWAYS) {
        /* If we failed and synced, merge in the bits we didn't copy: */
        bdrv_dirty_bitmap_merge_internal(bm, job->bcs_bitmap, NULL, true);
    }
}

static void backup_commit(Job *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    if (s->sync_bitmap) {
        backup_cleanup_sync_bitmap(s, 0);
    }
}

static void backup_abort(Job *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    if (s->sync_bitmap) {
        backup_cleanup_sync_bitmap(s, -1);
    }
}

static void backup_clean(Job *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);

    bdrv_backup_top_drop(s->backup_top);
}

void backup_do_checkpoint(BlockJob *job, Error **errp)
{
    BackupBlockJob *backup_job = container_of(job, BackupBlockJob, common);

    assert(block_job_driver(job) == &backup_job_driver);

    if (backup_job->sync_mode != MIRROR_SYNC_MODE_NONE) {
        error_setg(errp, "The backup job only supports block checkpoint in"
                   " sync=none mode");
        return;
    }

    bdrv_set_dirty_bitmap(backup_job->bcs_bitmap, 0, backup_job->len);
}

static BlockErrorAction backup_error_action(BackupBlockJob *job,
                                            bool read, int error)
{
    if (read) {
        return block_job_error_action(&job->common, job->on_source_error,
                                      true, error);
    } else {
        return block_job_error_action(&job->common, job->on_target_error,
                                      false, error);
    }
}

static void coroutine_fn backup_block_copy_callback(int ret, bool error_is_read,
                                                    void *opaque)
{
    BackupBlockJob *s = opaque;

    s->bcs_call = NULL;
    s->ret = ret;
    s->error_is_read = error_is_read;
    job_enter(&s->common.job);
}

static int coroutine_fn backup_loop(BackupBlockJob *job)
{
    while (true) { /* retry loop */
        assert(!job->bcs_call);
        job->bcs_call = block_copy_async(job->bcs, 0,
                                         QEMU_ALIGN_UP(job->len,
                                                       job->cluster_size),
                                         true, job->max_workers, job->max_chunk,
                                         backup_block_copy_callback);

        while (job->bcs_call && !job->common.job.cancelled) {
            /* wait and handle pauses */

            job_pause_point(&job->common.job);

            if (job->bcs_call && !job->common.job.cancelled) {
                job_yield(&job->common.job);
            }
        }

        if (!job->bcs_call && job->ret == 0) {
            /* Success */
            return 0;
        }

        if (job->common.job.cancelled) {
            if (job->bcs_call) {
                block_copy_cancel(job->bcs_call);
            }
            return 0;
        }

        if (!job->bcs_call && job->ret < 0 &&
            (backup_error_action(job, job->error_is_read, -job->ret) ==
             BLOCK_ERROR_ACTION_REPORT))
        {
            return job->ret;
        }
    }

    g_assert_not_reached();
}

static void backup_init_bcs_bitmap(BackupBlockJob *job)
{
    bool ret;
    uint64_t estimate;

    if (job->sync_mode == MIRROR_SYNC_MODE_BITMAP) {
        ret = bdrv_dirty_bitmap_merge_internal(job->bcs_bitmap,
                                               job->sync_bitmap,
                                               NULL, true);
        assert(ret);
    } else {
        if (job->sync_mode == MIRROR_SYNC_MODE_TOP) {
            /*
             * We can't hog the coroutine to initialize this thoroughly.
             * Set a flag and resume work when we are able to yield safely.
             */
            block_copy_set_skip_unallocated(job->bcs, true);
        }
        bdrv_set_dirty_bitmap(job->bcs_bitmap, 0, job->len);
    }

    estimate = bdrv_get_dirty_count(job->bcs_bitmap);
    job_progress_set_remaining(&job->common.job, estimate);
}

static int coroutine_fn backup_run(Job *job, Error **errp)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    int ret = 0;

    backup_init_bcs_bitmap(s);

    if (s->sync_mode == MIRROR_SYNC_MODE_TOP) {
        int64_t offset = 0;
        int64_t count;

        for (offset = 0; offset < s->len; ) {
            if (job_is_cancelled(job)) {
                return -ECANCELED;
            }

            job_pause_point(job);

            if (job_is_cancelled(job)) {
                return -ECANCELED;
            }

            ret = block_copy_reset_unallocated(s->bcs, offset, &count);
            if (ret < 0) {
                goto out;
            }

            offset += count;
        }
        block_copy_set_skip_unallocated(s->bcs, false);
    }

    if (s->sync_mode == MIRROR_SYNC_MODE_NONE) {
        /*
         * All bits are set in bcs_bitmap to allow any cluster to be copied.
         * This does not actually require them to be copied.
         */
        while (!job_is_cancelled(job)) {
            /*
             * Yield until the job is cancelled.  We just let our before_write
             * notify callback service CoW requests.
             */
            job_yield(job);
        }
    } else {
        ret = backup_loop(s);
    }

 out:
    return ret;
}

static void coroutine_fn backup_pause(Job *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);

    if (s->bcs_call) {
        block_copy_cancel(s->bcs_call);
    }
}

static void coroutine_fn backup_set_speed(BlockJob *job, int64_t speed)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    if (s->bcs) {
        /* In block_job_create we yet don't have bcs */
        block_copy_set_speed(s->bcs, s->bcs_call, speed);
    }
}

static const BlockJobDriver backup_job_driver = {
    .job_driver = {
        .instance_size          = sizeof(BackupBlockJob),
        .job_type               = JOB_TYPE_BACKUP,
        .free                   = block_job_free,
        .user_resume            = block_job_user_resume,
        .run                    = backup_run,
        .commit                 = backup_commit,
        .abort                  = backup_abort,
        .clean                  = backup_clean,
        .pause                  = backup_pause,
    },
    .set_speed = backup_set_speed,
};

static int64_t backup_calculate_cluster_size(BlockDriverState *target,
                                             Error **errp)
{
    int ret;
    BlockDriverInfo bdi;

    /*
     * If there is no backing file on the target, we cannot rely on COW if our
     * backup cluster size is smaller than the target cluster size. Even for
     * targets with a backing file, try to avoid COW if possible.
     */
    ret = bdrv_get_info(target, &bdi);
    if (ret == -ENOTSUP && !target->backing) {
        /* Cluster size is not defined */
        warn_report("The target block device doesn't provide "
                    "information about the block size and it doesn't have a "
                    "backing file. The default block size of %u bytes is "
                    "used. If the actual block size of the target exceeds "
                    "this default, the backup may be unusable",
                    BACKUP_CLUSTER_SIZE_DEFAULT);
        return BACKUP_CLUSTER_SIZE_DEFAULT;
    } else if (ret < 0 && !target->backing) {
        error_setg_errno(errp, -ret,
            "Couldn't determine the cluster size of the target image, "
            "which has no backing file");
        error_append_hint(errp,
            "Aborting, since this may create an unusable destination image\n");
        return ret;
    } else if (ret < 0 && target->backing) {
        /* Not fatal; just trudge on ahead. */
        return BACKUP_CLUSTER_SIZE_DEFAULT;
    }

    return MAX(BACKUP_CLUSTER_SIZE_DEFAULT, bdi.cluster_size);
}

BlockJob *backup_job_create(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, int64_t speed,
                  MirrorSyncMode sync_mode, BdrvDirtyBitmap *sync_bitmap,
                  BitmapSyncMode bitmap_mode,
                  bool compress,
                  const char *filter_node_name,
                  int max_workers,
                  int64_t max_chunk,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  int creation_flags,
                  BlockCompletionFunc *cb, void *opaque,
                  JobTxn *txn, Error **errp)
{
    int64_t len;
    BackupBlockJob *job = NULL;
    int64_t cluster_size;
    BdrvRequestFlags write_flags;
    BlockDriverState *backup_top = NULL;
    BlockCopyState *bcs = NULL;

    assert(bs);
    assert(target);

    /* QMP interface protects us from these cases */
    assert(sync_mode != MIRROR_SYNC_MODE_INCREMENTAL);
    assert(sync_bitmap || sync_mode != MIRROR_SYNC_MODE_BITMAP);

    if (max_workers < 1) {
        error_setg(errp, "At least one worker needed");
        return NULL;
    }

    if (max_chunk < 0) {
        error_setg(errp, "max-chunk is negative");
        return NULL;
    }

    if (bs == target) {
        error_setg(errp, "Source and target cannot be the same");
        return NULL;
    }

    if (!bdrv_is_inserted(bs)) {
        error_setg(errp, "Device is not inserted: %s",
                   bdrv_get_device_name(bs));
        return NULL;
    }

    if (!bdrv_is_inserted(target)) {
        error_setg(errp, "Device is not inserted: %s",
                   bdrv_get_device_name(target));
        return NULL;
    }

    if (compress && !block_driver_can_compress(target->drv)) {
        error_setg(errp, "Compression is not supported for this drive %s",
                   bdrv_get_device_name(target));
        return NULL;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_BACKUP_SOURCE, errp)) {
        return NULL;
    }

    if (bdrv_op_is_blocked(target, BLOCK_OP_TYPE_BACKUP_TARGET, errp)) {
        return NULL;
    }

    if (sync_bitmap) {
        /* If we need to write to this bitmap, check that we can: */
        if (bitmap_mode != BITMAP_SYNC_MODE_NEVER &&
            bdrv_dirty_bitmap_check(sync_bitmap, BDRV_BITMAP_DEFAULT, errp)) {
            return NULL;
        }

        /* Create a new bitmap, and freeze/disable this one. */
        if (bdrv_dirty_bitmap_create_successor(sync_bitmap, errp) < 0) {
            return NULL;
        }
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "unable to get length for '%s'",
                         bdrv_get_device_name(bs));
        goto error;
    }

    cluster_size = backup_calculate_cluster_size(target, errp);
    if (cluster_size < 0) {
        goto error;
    }
    if (max_chunk && max_chunk < cluster_size) {
        error_setg(errp, "Required max-chunk (%" PRIi64") is less than backup "
                   "cluster size (%" PRIi64 ")", max_chunk, cluster_size);
        return NULL;
    }

    /*
     * If source is in backing chain of target assume that target is going to be
     * used for "image fleecing", i.e. it should represent a kind of snapshot of
     * source at backup-start point in time. And target is going to be read by
     * somebody (for example, used as NBD export) during backup job.
     *
     * In this case, we need to add BDRV_REQ_SERIALISING write flag to avoid
     * intersection of backup writes and third party reads from target,
     * otherwise reading from target we may occasionally read already updated by
     * guest data.
     *
     * For more information see commit f8d59dfb40bb and test
     * tests/qemu-iotests/222
     */
    write_flags = (bdrv_chain_contains(target, bs) ? BDRV_REQ_SERIALISING : 0) |
                  (compress ? BDRV_REQ_WRITE_COMPRESSED : 0),

    backup_top = bdrv_backup_top_append(bs, target, filter_node_name,
                                        cluster_size, write_flags, &bcs, errp);
    if (!backup_top) {
        goto error;
    }

    /* job->len is fixed, so we can't allow resize */
    job = block_job_create(job_id, &backup_job_driver, txn, backup_top,
                           0, BLK_PERM_ALL,
                           speed, creation_flags, cb, opaque, errp);
    if (!job) {
        goto error;
    }

    job->backup_top = backup_top;
    job->source_bs = bs;
    job->on_source_error = on_source_error;
    job->on_target_error = on_target_error;
    job->sync_mode = sync_mode;
    job->sync_bitmap = sync_bitmap;
    job->bitmap_mode = bitmap_mode;
    job->bcs = bcs;
    job->bcs_bitmap = block_copy_dirty_bitmap(bcs);
    job->cluster_size = cluster_size;
    job->len = len;
    job->max_workers = max_workers;
    job->max_chunk = max_chunk;

    block_copy_set_callbacks(bcs, backup_progress_bytes_callback,
                             backup_progress_reset_callback, job);
    block_copy_set_speed(bcs, NULL, speed);

    /* Required permissions are already taken by backup-top target */
    block_job_add_bdrv(&job->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);

    return &job->common;

 error:
    if (sync_bitmap) {
        bdrv_reclaim_dirty_bitmap(sync_bitmap, NULL);
    }
    if (backup_top) {
        bdrv_backup_top_drop(backup_top);
    }

    return NULL;
}
