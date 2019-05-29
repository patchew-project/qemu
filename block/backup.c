/*
 * QEMU backup
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2019 Virtuozzo International GmbH. All rights reserved.
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
    BlockBackend *target;
    /* bitmap for sync=incremental */
    BdrvDirtyBitmap *sync_bitmap;
    MirrorSyncMode sync_mode;
    BlockdevOnError on_source_error;
    BlockdevOnError on_target_error;
    uint64_t len;
    uint64_t bytes_read;
    int64_t cluster_size;
    bool compress;

    HBitmap *copy_bitmap;
    bool use_copy_range;
    int64_t copy_range_size;

    bool serialize_target_writes;
} BackupBlockJob;

static const BlockJobDriver backup_job_driver;

/* Copy range to target with a bounce buffer and return the bytes copied. If
 * error occurred, return a negative error number */
static int coroutine_fn backup_cow_with_bounce_buffer(BackupBlockJob *job,
                                                      int64_t start,
                                                      int64_t end,
                                                      bool *error_is_read,
                                                      void **bounce_buffer)
{
    int ret;
    BlockBackend *blk = job->common.blk;
    int nbytes;
    int write_flags = job->serialize_target_writes ? BDRV_REQ_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(start, job->cluster_size));
    hbitmap_reset(job->copy_bitmap, start, job->cluster_size);
    nbytes = MIN(job->cluster_size, job->len - start);
    if (!*bounce_buffer) {
        *bounce_buffer = blk_blockalign(blk, job->cluster_size);
    }

    ret = blk_co_pread(blk, start, nbytes, *bounce_buffer, 0);
    if (ret < 0) {
        trace_backup_do_cow_read_fail(job, start, ret);
        if (error_is_read) {
            *error_is_read = true;
        }
        goto fail;
    }

    if (buffer_is_zero(*bounce_buffer, nbytes)) {
        ret = blk_co_pwrite_zeroes(job->target, start,
                                   nbytes, write_flags | BDRV_REQ_MAY_UNMAP);
    } else {
        ret = blk_co_pwrite(job->target, start,
                            nbytes, *bounce_buffer, write_flags |
                            (job->compress ? BDRV_REQ_WRITE_COMPRESSED : 0));
    }
    if (ret < 0) {
        trace_backup_do_cow_write_fail(job, start, ret);
        if (error_is_read) {
            *error_is_read = false;
        }
        goto fail;
    }

    return nbytes;
fail:
    hbitmap_set(job->copy_bitmap, start, job->cluster_size);
    return ret;

}

/* Copy range to target and return the bytes copied. If error occurred, return a
 * negative error number. */
static int coroutine_fn backup_cow_with_offload(BackupBlockJob *job,
                                                int64_t start, int64_t end)
{
    int ret;
    int nr_clusters;
    BlockBackend *blk = job->common.blk;
    int nbytes;
    int write_flags = job->serialize_target_writes ? BDRV_REQ_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(job->copy_range_size, job->cluster_size));
    assert(QEMU_IS_ALIGNED(start, job->cluster_size));
    nbytes = MIN(job->copy_range_size, end - start);
    nr_clusters = DIV_ROUND_UP(nbytes, job->cluster_size);
    hbitmap_reset(job->copy_bitmap, start, job->cluster_size * nr_clusters);
    ret = blk_co_copy_range(blk, start, job->target, start, nbytes,
                            0, write_flags);
    if (ret < 0) {
        trace_backup_do_cow_copy_range_fail(job, start, ret);
        hbitmap_set(job->copy_bitmap, start, job->cluster_size * nr_clusters);
        return ret;
    }

    return nbytes;
}

static int coroutine_fn backup_do_cow(BackupBlockJob *job,
                                      int64_t offset, uint64_t bytes,
                                      bool *error_is_read)
{
    int ret = 0;
    int64_t start, end; /* bytes */
    void *bounce_buffer = NULL;

    start = QEMU_ALIGN_DOWN(offset, job->cluster_size);
    end = QEMU_ALIGN_UP(bytes + offset, job->cluster_size);

    trace_backup_do_cow_enter(job, start, offset, bytes);

    while (start < end) {
        if (!hbitmap_get(job->copy_bitmap, start)) {
            trace_backup_do_cow_skip(job, start);
            start += job->cluster_size;
            continue; /* already copied */
        }

        trace_backup_do_cow_process(job, start);

        if (job->use_copy_range) {
            ret = backup_cow_with_offload(job, start, end);
            if (ret < 0) {
                job->use_copy_range = false;
            }
        }
        if (!job->use_copy_range) {
            ret = backup_cow_with_bounce_buffer(job, start, end,
                                                error_is_read, &bounce_buffer);
        }
        if (ret < 0) {
            break;
        }

        /* Publish progress, guest I/O counts as progress too.  Note that the
         * offset field is an opaque progress value, it is not a disk offset.
         */
        start += ret;
        job->bytes_read += ret;
        job_progress_update(&job->common.job, ret);
        ret = 0;
    }

    if (bounce_buffer) {
        qemu_vfree(bounce_buffer);
    }

    trace_backup_do_cow_return(job, offset, bytes, ret);

    return ret;
}

static void backup_cleanup_sync_bitmap(BackupBlockJob *job, int ret)
{
    BdrvDirtyBitmap *bm;
    BlockDriverState *bs = blk_bs(job->common.blk);

    if (ret < 0) {
        /* Merge the successor back into the parent, delete nothing. */
        bm = bdrv_reclaim_dirty_bitmap(bs, job->sync_bitmap, NULL);
        assert(bm);
    } else {
        /* Everything is fine, delete this bitmap and install the backup. */
        bm = bdrv_dirty_bitmap_abdicate(bs, job->sync_bitmap, NULL);
        assert(bm);
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
    BlockJob *bjob = &s->common;
    BlockDriverState *backup_top = blk_bs(s->common.blk);
    BlockDriverState *src = backup_top->backing->bs;

    assert(s->target);
    blk_unref(s->target);
    s->target = NULL;

    bdrv_ref(backup_top);
    bdrv_ref(src);

    if (s->copy_bitmap) {
        hbitmap_free(s->copy_bitmap);
        s->copy_bitmap = NULL;
    }

    bdrv_backup_top_drop(backup_top);

    blk_remove_bs(bjob->blk);
    blk_set_perm(bjob->blk, 0, BLK_PERM_ALL, &error_abort);
    blk_insert_bs(bjob->blk, backup_top, &error_abort);

    bdrv_unref(src);
    bdrv_unref(backup_top);
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

    hbitmap_set(backup_job->copy_bitmap, 0, backup_job->len);
}

static void backup_drain(BlockJob *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    /* Need to keep a reference in case blk_drain triggers execution
     * of backup_complete...
     */
    if (s->target) {
        BlockBackend *target = s->target;
        blk_ref(target);
        blk_drain(target);
        blk_unref(target);
    }
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

static bool coroutine_fn yield_and_check(BackupBlockJob *job)
{
    uint64_t delay_ns;

    if (job_is_cancelled(&job->common.job)) {
        return true;
    }

    /* We need to yield even for delay_ns = 0 so that bdrv_drain_all() can
     * return. Without a yield, the VM would not reboot. */
    delay_ns = block_job_ratelimit_get_delay(&job->common, job->bytes_read);
    job->bytes_read = 0;
    job_sleep_ns(&job->common.job, delay_ns);

    if (job_is_cancelled(&job->common.job)) {
        return true;
    }

    return false;
}

static bool bdrv_is_unallocated_range(BlockDriverState *bs,
                                      int64_t offset, int64_t bytes)
{
    int64_t end = offset + bytes;

    while (offset < end && !bdrv_is_allocated(bs, offset, bytes, &bytes)) {
        if (bytes == 0) {
            return true;
        }
        offset += bytes;
        bytes = end - offset;
    }

    return offset >= end;
}

static int coroutine_fn backup_loop(BackupBlockJob *job)
{
    int ret;
    bool error_is_read;
    int64_t offset;
    HBitmapIter hbi;
    BlockDriverState *bs = blk_bs(job->common.blk);
    void *lock;

    hbitmap_iter_init(&hbi, job->copy_bitmap, 0);
    while ((offset = hbitmap_iter_next(&hbi)) != -1) {
        lock = bdrv_co_try_lock(backing_bs(blk_bs(job->common.blk)), offset,
                                job->cluster_size);
        /*
         * Dirty bit is set, which means that there are no in-flight
         * write requests on this area. We must succeed.
         */
        assert(lock);

        if (job->sync_mode == MIRROR_SYNC_MODE_TOP &&
            bdrv_is_unallocated_range(bs, offset, job->cluster_size))
        {
            hbitmap_reset(job->copy_bitmap, offset, job->cluster_size);
            bdrv_co_unlock(lock);
            continue;
        }

        do {
            if (yield_and_check(job)) {
                bdrv_co_unlock(lock);
                return 0;
            }
            ret = backup_do_cow(job, offset, job->cluster_size, &error_is_read);
            if (ret < 0 && backup_error_action(job, error_is_read, -ret) ==
                           BLOCK_ERROR_ACTION_REPORT)
            {
                bdrv_co_unlock(lock);
                return ret;
            }
        } while (ret < 0);

        bdrv_co_unlock(lock);
    }

    return 0;
}

/* init copy_bitmap from sync_bitmap */
static void backup_incremental_init_copy_bitmap(BackupBlockJob *job)
{
    uint64_t offset = 0;
    uint64_t bytes = job->len;

    while (bdrv_dirty_bitmap_next_dirty_area(job->sync_bitmap,
                                             &offset, &bytes))
    {
        hbitmap_set(job->copy_bitmap, offset, bytes);

        offset += bytes;
        if (offset >= job->len) {
            break;
        }
        bytes = job->len - offset;
    }

    /* TODO job_progress_set_remaining() would make more sense */
    job_progress_update(&job->common.job,
        job->len - hbitmap_count(job->copy_bitmap));
}

static int coroutine_fn backup_run(Job *job, Error **errp)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    int ret = 0;

    job_progress_set_remaining(job, s->len);

    if (s->sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        backup_incremental_init_copy_bitmap(s);
    } else {
        hbitmap_set(s->copy_bitmap, 0, s->len);
    }

    if (s->sync_mode == MIRROR_SYNC_MODE_NONE) {
        /* All bits are set in copy_bitmap to allow any cluster to be copied.
         * This does not actually require them to be copied. */
        while (!job_is_cancelled(job)) {
            /*
             * Yield until the job is cancelled.  We just let our backup-top
             * filter driver service CbW requests.
             */
            job_yield(job);
        }
    } else {
        ret = backup_loop(s);
    }

    return ret;
}

static const BlockJobDriver backup_job_driver = {
    .job_driver = {
        .instance_size          = sizeof(BackupBlockJob),
        .job_type               = JOB_TYPE_BACKUP,
        .free                   = block_job_free,
        .user_resume            = block_job_user_resume,
        .drain                  = block_job_drain,
        .run                    = backup_run,
        .commit                 = backup_commit,
        .abort                  = backup_abort,
        .clean                  = backup_clean,
    },
    .drain                  = backup_drain,
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

static void backup_top_progress(uint64_t done, void *opaque)
{
    job_progress_update((Job *)opaque, done);
}

BlockJob *backup_job_create(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, int64_t speed,
                  MirrorSyncMode sync_mode, BdrvDirtyBitmap *sync_bitmap,
                  bool compress,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  int creation_flags,
                  BlockCompletionFunc *cb, void *opaque,
                  JobTxn *txn, Error **errp)
{
    int64_t len;
    BackupBlockJob *job = NULL;
    int ret;
    int64_t cluster_size;
    HBitmap *copy_bitmap = NULL;
    BlockDriverState *backup_top = NULL;

    assert(bs);
    assert(target);

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

    if (compress && target->drv->bdrv_co_pwritev_compressed == NULL) {
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

    if (sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        if (!sync_bitmap) {
            error_setg(errp, "must provide a valid bitmap name for "
                             "\"incremental\" sync mode");
            return NULL;
        }

        /* Create a new bitmap, and freeze/disable this one. */
        if (bdrv_dirty_bitmap_create_successor(bs, sync_bitmap, errp) < 0) {
            return NULL;
        }
    } else if (sync_bitmap) {
        error_setg(errp,
                   "a sync_bitmap was provided to backup_run, "
                   "but received an incompatible sync_mode (%s)",
                   MirrorSyncMode_str(sync_mode));
        return NULL;
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

    copy_bitmap = hbitmap_alloc(len, ctz32(cluster_size));

    backup_top = bdrv_backup_top_append(bs, target, copy_bitmap, errp);
    if (!backup_top) {
        goto error;
    }

    /* job->len is fixed, so we can't allow resize */
    job = block_job_create(job_id, &backup_job_driver, txn, backup_top,
                           BLK_PERM_CONSISTENT_READ,
                           BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE |
                           BLK_PERM_WRITE_UNCHANGED | BLK_PERM_GRAPH_MOD,
                           speed, creation_flags, cb, opaque, errp);
    if (!job) {
        goto error;
    }

    bdrv_backup_top_set_progress_callback(backup_top, backup_top_progress,
                                          &job->common.job);

    /* The target must match the source in size, so no resize here either */
    job->target = blk_new(job->common.job.aio_context,
                          BLK_PERM_WRITE,
                          BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE |
                          BLK_PERM_WRITE_UNCHANGED | BLK_PERM_GRAPH_MOD);
    ret = blk_insert_bs(job->target, target, errp);
    if (ret < 0) {
        goto error;
    }

    job->on_source_error = on_source_error;
    job->on_target_error = on_target_error;
    job->sync_mode = sync_mode;
    job->sync_bitmap = sync_mode == MIRROR_SYNC_MODE_INCREMENTAL ?
                       sync_bitmap : NULL;
    job->compress = compress;

    /* Detect image-fleecing (and similar) schemes */
    job->serialize_target_writes = bdrv_chain_contains(target, bs);
    job->cluster_size = cluster_size;
    job->copy_bitmap = copy_bitmap;
    copy_bitmap = NULL;
    job->use_copy_range = true;
    job->copy_range_size = MIN_NON_ZERO(blk_get_max_transfer(job->common.blk),
                                        blk_get_max_transfer(job->target));
    job->copy_range_size = MAX(job->cluster_size,
                               QEMU_ALIGN_UP(job->copy_range_size,
                                             job->cluster_size));

    /* Required permissions are already taken with target's blk_new() */
    block_job_add_bdrv(&job->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);
    job->len = len;

    return &job->common;

 error:
    if (sync_bitmap) {
        bdrv_reclaim_dirty_bitmap(bs, sync_bitmap, NULL);
    }
    if (job) {
        backup_clean(&job->common.job);
        job_early_fail(&job->common.job);
    }
    if (backup_top) {
        bdrv_backup_top_drop(backup_top);
    }
    if (copy_bitmap) {
        assert(!job || !job->copy_bitmap);
        hbitmap_free(copy_bitmap);
    }

    return NULL;
}
