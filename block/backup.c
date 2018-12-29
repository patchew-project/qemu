/*
 * QEMU backup
 *
 * Copyright (C) 2013 Proxmox Server Solutions
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
    BdrvChild *source;
    BdrvChild *target;
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

    BlockDriverState *backup_top;
    uint64_t backup_top_progress;
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
    struct iovec iov;
    QEMUIOVector qiov;
    int nbytes;
    int write_flags = job->serialize_target_writes ? BDRV_REQ_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(start, job->cluster_size));
    hbitmap_reset(job->copy_bitmap, start, job->cluster_size);
    nbytes = MIN(job->cluster_size, job->len - start);
    if (!*bounce_buffer) {
        *bounce_buffer = qemu_blockalign(job->source->bs, job->cluster_size);
    }
    iov.iov_base = *bounce_buffer;
    iov.iov_len = nbytes;
    qemu_iovec_init_external(&qiov, &iov, 1);

    ret = bdrv_co_preadv(job->source, start, qiov.size, &qiov, 0);
    if (ret < 0) {
        trace_backup_do_cow_read_fail(job, start, ret);
        if (error_is_read) {
            *error_is_read = true;
        }
        goto fail;
    }

    if (qemu_iovec_is_zero(&qiov)) {
        ret = bdrv_co_pwrite_zeroes(job->target, start, qiov.size,
                                    write_flags | BDRV_REQ_MAY_UNMAP);
    } else {
        ret = bdrv_co_pwritev(job->target, start,
                              qiov.size, &qiov, write_flags |
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
    int nbytes;
    int write_flags = job->serialize_target_writes ? BDRV_REQ_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(job->copy_range_size, job->cluster_size));
    assert(QEMU_IS_ALIGNED(start, job->cluster_size));
    nbytes = MIN(job->copy_range_size, end - start);
    nr_clusters = DIV_ROUND_UP(nbytes, job->cluster_size);
    hbitmap_reset(job->copy_bitmap, start, job->cluster_size * nr_clusters);
    ret = bdrv_co_copy_range(job->source, start, job->target, start, nbytes,
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
    uint64_t backup_top_progress;

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
        backup_top_progress = bdrv_backup_top_progress(job->backup_top);
        job_progress_update(&job->common.job, ret + backup_top_progress -
                            job->backup_top_progress);
        job->backup_top_progress = backup_top_progress;
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
    BlockDriverState *bs = job->source->bs;

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

    /* We must clean it to not crash in backup_drain. */
    s->target = NULL;

    if (s->copy_bitmap) {
        hbitmap_free(s->copy_bitmap);
        s->copy_bitmap = NULL;
    }

    bdrv_backup_top_drop(s->backup_top);
}

static void backup_attached_aio_context(BlockJob *job, AioContext *aio_context)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    bdrv_set_aio_context(s->target->bs, aio_context);
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
        BlockDriverState *target = s->target->bs;
        bdrv_ref(target);
        bdrv_drain(target);
        bdrv_unref(target);
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

static int coroutine_fn backup_run_incremental(BackupBlockJob *job)
{
    int ret;
    bool error_is_read;
    int64_t offset;
    HBitmapIter hbi;
    void *lock = NULL;

    hbitmap_iter_init(&hbi, job->copy_bitmap, 0);
    while (hbitmap_count(job->copy_bitmap)) {
        offset = hbitmap_iter_next(&hbi);
        if (offset == -1) {
            /*
             * we may have skipped some clusters, which were handled by
             * backup-top, but failed and finished by returning error to
             * the guest and set dirty bit back.
             */
            hbitmap_iter_init(&hbi, job->copy_bitmap, 0);
            offset = hbitmap_iter_next(&hbi);
            assert(offset);
        }

        lock = bdrv_co_try_lock(job->source, offset, job->cluster_size);
        /*
         * Dirty bit is set, which means that there are no in-flight
         * write requests on this area. We must succeed.
         */
        assert(lock);

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
        lock = NULL;
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
    BlockDriverState *bs = s->source->bs;
    int64_t offset;
    int ret = 0;
    uint64_t backup_top_progress;

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
             * fileter driver service CbW requests.
             */
            job_yield(job);
        }
    } else if (s->sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        ret = backup_run_incremental(s);
    } else {
        bool retry;
        void *lock;

iteration:
        retry = false;
        lock = NULL;

        /* Both FULL and TOP SYNC_MODE's require copying.. */
        for (offset = 0; offset < s->len;
             offset += s->cluster_size) {
            bool error_is_read;
            int alloced = 0;

            if (retry) {
                retry = false;
            } else if (lock) {
                bdrv_co_unlock(lock);
                lock = NULL;
            }

            if (yield_and_check(s)) {
                break;
            }

            if (s->sync_mode == MIRROR_SYNC_MODE_TOP) {
                int i;
                int64_t n;

                /* Check to see if these blocks are already in the
                 * backing file. */

                for (i = 0; i < s->cluster_size;) {
                    /* bdrv_is_allocated() only returns true/false based
                     * on the first set of sectors it comes across that
                     * are are all in the same state.
                     * For that reason we must verify each sector in the
                     * backup cluster length.  We end up copying more than
                     * needed but at some point that is always the case. */
                    alloced =
                        bdrv_is_allocated(bs, offset + i,
                                          s->cluster_size - i, &n);
                    i += n;

                    if (alloced || n == 0) {
                        break;
                    }
                }

                /* If the above loop never found any sectors that are in
                 * the topmost image, skip this backup. */
                if (alloced == 0) {
                    hbitmap_reset(s->copy_bitmap, offset, s->cluster_size);
                    continue;
                }
            }
            /* FULL sync mode we copy the whole drive. */
            if (alloced < 0) {
                ret = alloced;
            } else {
                if (!hbitmap_get(s->copy_bitmap, offset)) {
                    trace_backup_do_cow_skip(job, offset);
                    continue; /* already copied */
                }
                if (!lock) {
                    lock = bdrv_co_try_lock(s->source, offset, s->cluster_size);
                    /*
                     * Dirty bit is set, which means that there are no in-flight
                     * write requests on this area. We must succeed.
                     */
                    assert(lock);
                }
                ret = backup_do_cow(s, offset, s->cluster_size,
                                    &error_is_read);
            }
            if (ret < 0) {
                /* Depending on error action, fail now or retry cluster */
                BlockErrorAction action =
                    backup_error_action(s, error_is_read, -ret);
                if (action == BLOCK_ERROR_ACTION_REPORT) {
                    break;
                } else {
                    offset -= s->cluster_size;
                    retry = true;
                    continue;
                }
            }
        }
        if (lock) {
            bdrv_co_unlock(lock);
            lock = NULL;
        }
        if (ret == 0 && !job_is_cancelled(job) &&
            hbitmap_count(s->copy_bitmap))
        {
            /*
             * we may have skipped some clusters, which were handled by
             * backup-top, but failed and finished by returning error to
             * the guest and set dirty bit back.
             */
            goto iteration;
        }
    }

    /* wait pending CBW operations in backup-top */
    bdrv_drain(s->backup_top);

    backup_top_progress = bdrv_backup_top_progress(s->backup_top);
    job_progress_update(job, ret + backup_top_progress -
                        s->backup_top_progress);
    s->backup_top_progress = backup_top_progress;

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
    .attached_aio_context   = backup_attached_aio_context,
    .drain                  = backup_drain,
};

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
    BlockDriverInfo bdi;
    BackupBlockJob *job = NULL;
    int ret;
    int64_t cluster_size;
    HBitmap *copy_bitmap = NULL;
    BlockDriverState *backup_top = NULL;
    uint64_t all_except_resize = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE |
                                 BLK_PERM_WRITE_UNCHANGED | BLK_PERM_GRAPH_MOD;

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
        cluster_size = BACKUP_CLUSTER_SIZE_DEFAULT;
    } else if (ret < 0 && !target->backing) {
        error_setg_errno(errp, -ret,
            "Couldn't determine the cluster size of the target image, "
            "which has no backing file");
        error_append_hint(errp,
            "Aborting, since this may create an unusable destination image\n");
        return NULL;
    } else if (ret < 0 && target->backing) {
        /* Not fatal; just trudge on ahead. */
        cluster_size = BACKUP_CLUSTER_SIZE_DEFAULT;
    } else {
        cluster_size = MAX(BACKUP_CLUSTER_SIZE_DEFAULT, bdi.cluster_size);
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "unable to get length for '%s'",
                         bdrv_get_device_name(bs));
        goto error;
    }

    copy_bitmap = hbitmap_alloc(len, ctz32(cluster_size));

    /*
     * bdrv_get_device_name will not help to find device name starting from
     * @bs after backup-top append, so let's calculate job_id before. Do
     * it in the same way like block_job_create
     */
    if (job_id == NULL && !(creation_flags & JOB_INTERNAL)) {
        job_id = bdrv_get_device_name(bs);
    }

    backup_top = bdrv_backup_top_append(bs, target, copy_bitmap, errp);
    if (!backup_top) {
        goto error;
    }

    /* job->len is fixed, so we can't allow resize */
    job = block_job_create(job_id, &backup_job_driver, txn, bs, 0,
                           all_except_resize, speed, creation_flags,
                           cb, opaque, errp);
    if (!job) {
        goto error;
    }

    job->source = backup_top->backing;
    job->target = ((BDRVBackupTopState *)backup_top->opaque)->target;

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
    job->copy_range_size =
            MIN_NON_ZERO(MIN_NON_ZERO(INT_MAX,
                                      job->source->bs->bl.max_transfer),
                         job->target->bs->bl.max_transfer);
    job->copy_range_size = MAX(job->cluster_size,
                               QEMU_ALIGN_UP(job->copy_range_size,
                                             job->cluster_size));

    /* The target must match the source in size, so no resize here either */
    block_job_add_bdrv(&job->common, "target", target, 0, all_except_resize,
                       &error_abort);
    job->len = len;
    job->backup_top = backup_top;

    return &job->common;

 error:
    if (copy_bitmap) {
        assert(!job || !job->copy_bitmap);
        hbitmap_free(copy_bitmap);
    }
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

    return NULL;
}
