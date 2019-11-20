/*
 * Blockdev HMP commands
 *
 * Copyright (c) 2004 Fabrice Bellard
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
#include "hw/boards.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "block/block_int.h"
#include "block/qapi.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/qmp/qerror.h"
#include "monitor/hmp.h"

void hmp_drive_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    DriveInfo *dinfo = NULL;
    QemuOpts *opts;
    MachineClass *mc;
    const char *optstr = qdict_get_str(qdict, "opts");
    bool node = qdict_get_try_bool(qdict, "node", false);

    if (node) {
        hmp_drive_add_node(mon, optstr);
        return;
    }

    opts = drive_def(optstr);
    if (!opts)
        return;

    mc = MACHINE_GET_CLASS(current_machine);
    dinfo = drive_new(opts, mc->block_default_type, &err);
    if (err) {
        error_report_err(err);
        qemu_opts_del(opts);
        goto err;
    }

    if (!dinfo) {
        return;
    }

    switch (dinfo->type) {
    case IF_NONE:
        monitor_printf(mon, "OK\n");
        break;
    default:
        monitor_printf(mon, "Can't hot-add drive to type %d\n", dinfo->type);
        goto err;
    }
    return;

err:
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
        monitor_remove_blk(blk);
        blk_unref(blk);
    }
}

void hmp_drive_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    BlockBackend *blk;
    BlockDriverState *bs;
    AioContext *aio_context;
    Error *local_err = NULL;

    bs = bdrv_find_node(id);
    if (bs) {
        qmp_blockdev_del(id, &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
        return;
    }

    blk = blk_by_name(id);
    if (!blk) {
        error_report("Device '%s' not found", id);
        return;
    }

    if (!blk_legacy_dinfo(blk)) {
        error_report("Deleting device added with blockdev-add"
                     " is not supported");
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    bs = blk_bs(blk);
    if (bs) {
        if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_DRIVE_DEL, &local_err)) {
            error_report_err(local_err);
            aio_context_release(aio_context);
            return;
        }

        blk_remove_bs(blk);
    }

    /* Make the BlockBackend and the attached BlockDriverState anonymous */
    monitor_remove_blk(blk);

    /* If this BlockBackend has a device attached to it, its refcount will be
     * decremented when the device is removed; otherwise we have to do so here.
     */
    if (blk_get_attached_dev(blk)) {
        /* Further I/O must not pause the guest */
        blk_set_on_error(blk, BLOCKDEV_ON_ERROR_REPORT,
                         BLOCKDEV_ON_ERROR_REPORT);
    } else {
        blk_unref(blk);
    }

    aio_context_release(aio_context);
}

void hmp_commit(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    BlockBackend *blk;
    int ret;

    if (!strcmp(device, "all")) {
        ret = blk_commit_all();
    } else {
        BlockDriverState *bs;
        AioContext *aio_context;

        blk = blk_by_name(device);
        if (!blk) {
            error_report("Device '%s' not found", device);
            return;
        }
        if (!blk_is_available(blk)) {
            error_report("Device '%s' has no medium", device);
            return;
        }

        bs = blk_bs(blk);
        aio_context = bdrv_get_aio_context(bs);
        aio_context_acquire(aio_context);

        ret = bdrv_commit(bs);

        aio_context_release(aio_context);
    }
    if (ret < 0) {
        error_report("'commit' error for '%s': %s", device, strerror(-ret));
    }
}

void hmp_drive_mirror(Monitor *mon, const QDict *qdict)
{
    const char *filename = qdict_get_str(qdict, "target");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    bool full = qdict_get_try_bool(qdict, "full", false);
    Error *err = NULL;
    DriveMirror mirror = {
        .device = (char *)qdict_get_str(qdict, "device"),
        .target = (char *)filename,
        .has_format = !!format,
        .format = (char *)format,
        .sync = full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
        .has_mode = true,
        .mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS,
        .unmap = true,
    };

    if (!filename) {
        error_setg(&err, QERR_MISSING_PARAMETER, "target");
        hmp_handle_error(mon, &err);
        return;
    }
    qmp_drive_mirror(&mirror, &err);
    hmp_handle_error(mon, &err);
}

void hmp_drive_backup(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_str(qdict, "target");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    bool full = qdict_get_try_bool(qdict, "full", false);
    bool compress = qdict_get_try_bool(qdict, "compress", false);
    Error *err = NULL;
    DriveBackup backup = {
        .device = (char *)device,
        .target = (char *)filename,
        .has_format = !!format,
        .format = (char *)format,
        .sync = full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
        .has_mode = true,
        .mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS,
        .has_compress = !!compress,
        .compress = compress,
    };

    if (!filename) {
        error_setg(&err, QERR_MISSING_PARAMETER, "target");
        hmp_handle_error(mon, &err);
        return;
    }

    qmp_drive_backup(&backup, &err);
    hmp_handle_error(mon, &err);
}


void hmp_block_job_set_speed(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    int64_t value = qdict_get_int(qdict, "speed");

    qmp_block_job_set_speed(device, value, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_cancel(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    bool force = qdict_get_try_bool(qdict, "force", false);

    qmp_block_job_cancel(device, true, force, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_pause(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_pause(device, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_resume(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_resume(device, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_complete(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_complete(device, &error);

    hmp_handle_error(mon, &error);
}

void hmp_snapshot_blkdev(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_try_str(qdict, "snapshot-file");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    enum NewImageMode mode;
    Error *err = NULL;

    if (!filename) {
        /* In the future, if 'snapshot-file' is not specified, the snapshot
           will be taken internally. Today it's actually required. */
        error_setg(&err, QERR_MISSING_PARAMETER, "snapshot-file");
        hmp_handle_error(mon, &err);
        return;
    }

    mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    qmp_blockdev_snapshot_sync(true, device, false, NULL,
                               filename, false, NULL,
                               !!format, format,
                               true, mode, &err);
    hmp_handle_error(mon, &err);
}

void hmp_snapshot_blkdev_internal(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    qmp_blockdev_snapshot_internal_sync(device, name, &err);
    hmp_handle_error(mon, &err);
}

void hmp_snapshot_delete_blkdev_internal(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *name = qdict_get_str(qdict, "name");
    const char *id = qdict_get_try_str(qdict, "id");
    Error *err = NULL;

    qmp_blockdev_snapshot_delete_internal_sync(device, !!id, id,
                                               true, name, &err);
    hmp_handle_error(mon, &err);
}

void hmp_block_resize(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    int64_t size = qdict_get_int(qdict, "size");
    Error *err = NULL;

    qmp_block_resize(true, device, false, NULL, size, &err);
    hmp_handle_error(mon, &err);
}

void hmp_block_stream(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    const char *base = qdict_get_try_str(qdict, "base");
    int64_t speed = qdict_get_try_int(qdict, "speed", 0);

    qmp_block_stream(true, device, device, base != NULL, base, false, NULL,
                     false, NULL, qdict_haskey(qdict, "speed"), speed, true,
                     BLOCKDEV_ON_ERROR_REPORT, false, false, false, false,
                     &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_passwd(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *password = qdict_get_str(qdict, "password");
    Error *err = NULL;

    qmp_block_passwd(true, device, false, NULL, password, &err);
    hmp_handle_error(mon, &err);
}

void hmp_block_set_io_throttle(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    char *device = (char *) qdict_get_str(qdict, "device");
    BlockIOThrottle throttle = {
        .bps = qdict_get_int(qdict, "bps"),
        .bps_rd = qdict_get_int(qdict, "bps_rd"),
        .bps_wr = qdict_get_int(qdict, "bps_wr"),
        .iops = qdict_get_int(qdict, "iops"),
        .iops_rd = qdict_get_int(qdict, "iops_rd"),
        .iops_wr = qdict_get_int(qdict, "iops_wr"),
    };

    /* qmp_block_set_io_throttle has separate parameters for the
     * (deprecated) block device name and the qdev ID but the HMP
     * version has only one, so we must decide which one to pass. */
    if (blk_by_name(device)) {
        throttle.has_device = true;
        throttle.device = device;
    } else {
        throttle.has_id = true;
        throttle.id = device;
    }

    qmp_block_set_io_throttle(&throttle, &err);
    hmp_handle_error(mon, &err);
}

static void print_block_info(Monitor *mon, BlockInfo *info,
                             BlockDeviceInfo *inserted, bool verbose)
{
    ImageInfo *image_info;

    assert(!info || !info->has_inserted || info->inserted == inserted);

    if (info && *info->device) {
        monitor_printf(mon, "%s", info->device);
        if (inserted && inserted->has_node_name) {
            monitor_printf(mon, " (%s)", inserted->node_name);
        }
    } else {
        assert(info || inserted);
        monitor_printf(mon, "%s",
                       inserted && inserted->has_node_name ? inserted->node_name
                       : info && info->has_qdev ? info->qdev
                       : "<anonymous>");
    }

    if (inserted) {
        monitor_printf(mon, ": %s (%s%s%s)\n",
                       inserted->file,
                       inserted->drv,
                       inserted->ro ? ", read-only" : "",
                       inserted->encrypted ? ", encrypted" : "");
    } else {
        monitor_printf(mon, ": [not inserted]\n");
    }

    if (info) {
        if (info->has_qdev) {
            monitor_printf(mon, "    Attached to:      %s\n", info->qdev);
        }
        if (info->has_io_status && info->io_status != BLOCK_DEVICE_IO_STATUS_OK) {
            monitor_printf(mon, "    I/O status:       %s\n",
                           BlockDeviceIoStatus_str(info->io_status));
        }

        if (info->removable) {
            monitor_printf(mon, "    Removable device: %slocked, tray %s\n",
                           info->locked ? "" : "not ",
                           info->tray_open ? "open" : "closed");
        }
    }


    if (!inserted) {
        return;
    }

    monitor_printf(mon, "    Cache mode:       %s%s%s\n",
                   inserted->cache->writeback ? "writeback" : "writethrough",
                   inserted->cache->direct ? ", direct" : "",
                   inserted->cache->no_flush ? ", ignore flushes" : "");

    if (inserted->has_backing_file) {
        monitor_printf(mon,
                       "    Backing file:     %s "
                       "(chain depth: %" PRId64 ")\n",
                       inserted->backing_file,
                       inserted->backing_file_depth);
    }

    if (inserted->detect_zeroes != BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF) {
        monitor_printf(mon, "    Detect zeroes:    %s\n",
                BlockdevDetectZeroesOptions_str(inserted->detect_zeroes));
    }

    if (inserted->bps  || inserted->bps_rd  || inserted->bps_wr  ||
        inserted->iops || inserted->iops_rd || inserted->iops_wr)
    {
        monitor_printf(mon, "    I/O throttling:   bps=%" PRId64
                        " bps_rd=%" PRId64  " bps_wr=%" PRId64
                        " bps_max=%" PRId64
                        " bps_rd_max=%" PRId64
                        " bps_wr_max=%" PRId64
                        " iops=%" PRId64 " iops_rd=%" PRId64
                        " iops_wr=%" PRId64
                        " iops_max=%" PRId64
                        " iops_rd_max=%" PRId64
                        " iops_wr_max=%" PRId64
                        " iops_size=%" PRId64
                        " group=%s\n",
                        inserted->bps,
                        inserted->bps_rd,
                        inserted->bps_wr,
                        inserted->bps_max,
                        inserted->bps_rd_max,
                        inserted->bps_wr_max,
                        inserted->iops,
                        inserted->iops_rd,
                        inserted->iops_wr,
                        inserted->iops_max,
                        inserted->iops_rd_max,
                        inserted->iops_wr_max,
                        inserted->iops_size,
                        inserted->group);
    }

    if (verbose) {
        monitor_printf(mon, "\nImages:\n");
        image_info = inserted->image;
        while (1) {
                bdrv_image_info_dump(image_info);
            if (image_info->has_backing_image) {
                image_info = image_info->backing_image;
            } else {
                break;
            }
        }
    }
}

void hmp_info_block(Monitor *mon, const QDict *qdict)
{
    BlockInfoList *block_list, *info;
    BlockDeviceInfoList *blockdev_list, *blockdev;
    const char *device = qdict_get_try_str(qdict, "device");
    bool verbose = qdict_get_try_bool(qdict, "verbose", false);
    bool nodes = qdict_get_try_bool(qdict, "nodes", false);
    bool printed = false;

    /* Print BlockBackend information */
    if (!nodes) {
        block_list = qmp_query_block(NULL);
    } else {
        block_list = NULL;
    }

    for (info = block_list; info; info = info->next) {
        if (device && strcmp(device, info->value->device)) {
            continue;
        }

        if (info != block_list) {
            monitor_printf(mon, "\n");
        }

        print_block_info(mon, info->value, info->value->has_inserted
                                           ? info->value->inserted : NULL,
                         verbose);
        printed = true;
    }

    qapi_free_BlockInfoList(block_list);

    if ((!device && !nodes) || printed) {
        return;
    }

    /* Print node information */
    blockdev_list = qmp_query_named_block_nodes(NULL);
    for (blockdev = blockdev_list; blockdev; blockdev = blockdev->next) {
        assert(blockdev->value->has_node_name);
        if (device && strcmp(device, blockdev->value->node_name)) {
            continue;
        }

        if (blockdev != blockdev_list) {
            monitor_printf(mon, "\n");
        }

        print_block_info(mon, NULL, blockdev->value, verbose);
    }
    qapi_free_BlockDeviceInfoList(blockdev_list);
}

void hmp_info_blockstats(Monitor *mon, const QDict *qdict)
{
    BlockStatsList *stats_list, *stats;

    stats_list = qmp_query_blockstats(false, false, NULL);

    for (stats = stats_list; stats; stats = stats->next) {
        if (!stats->value->has_device) {
            continue;
        }

        monitor_printf(mon, "%s:", stats->value->device);
        monitor_printf(mon, " rd_bytes=%" PRId64
                       " wr_bytes=%" PRId64
                       " rd_operations=%" PRId64
                       " wr_operations=%" PRId64
                       " flush_operations=%" PRId64
                       " wr_total_time_ns=%" PRId64
                       " rd_total_time_ns=%" PRId64
                       " flush_total_time_ns=%" PRId64
                       " rd_merged=%" PRId64
                       " wr_merged=%" PRId64
                       " idle_time_ns=%" PRId64
                       "\n",
                       stats->value->stats->rd_bytes,
                       stats->value->stats->wr_bytes,
                       stats->value->stats->rd_operations,
                       stats->value->stats->wr_operations,
                       stats->value->stats->flush_operations,
                       stats->value->stats->wr_total_time_ns,
                       stats->value->stats->rd_total_time_ns,
                       stats->value->stats->flush_total_time_ns,
                       stats->value->stats->rd_merged,
                       stats->value->stats->wr_merged,
                       stats->value->stats->idle_time_ns);
    }

    qapi_free_BlockStatsList(stats_list);
}

void hmp_info_block_jobs(Monitor *mon, const QDict *qdict)
{
    BlockJobInfoList *list;
    Error *err = NULL;

    list = qmp_query_block_jobs(&err);
    assert(!err);

    if (!list) {
        monitor_printf(mon, "No active jobs\n");
        return;
    }

    while (list) {
        if (strcmp(list->value->type, "stream") == 0) {
            monitor_printf(mon, "Streaming device %s: Completed %" PRId64
                           " of %" PRId64 " bytes, speed limit %" PRId64
                           " bytes/s\n",
                           list->value->device,
                           list->value->offset,
                           list->value->len,
                           list->value->speed);
        } else {
            monitor_printf(mon, "Type %s, device %s: Completed %" PRId64
                           " of %" PRId64 " bytes, speed limit %" PRId64
                           " bytes/s\n",
                           list->value->type,
                           list->value->device,
                           list->value->offset,
                           list->value->len,
                           list->value->speed);
        }
        list = list->next;
    }

    qapi_free_BlockJobInfoList(list);
}
