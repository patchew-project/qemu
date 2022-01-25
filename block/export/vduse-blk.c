/*
 * Export QEMU block device via VDUSE
 *
 * Copyright (C) 2022 Bytedance Inc. and/or its affiliates. All rights reserved.
 *   Portions of codes and concepts borrowed from vhost-user-blk-server.c, so:
 *     Copyright (c) 2020 Red Hat, Inc.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *   Coiby Xu <coiby.xu@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include <sys/eventfd.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/block-backend.h"
#include "block/export.h"
#include "qemu/error-report.h"
#include "util/block-helpers.h"
#include "subprojects/libvduse/libvduse.h"

#include "standard-headers/linux/virtio_ring.h"
#include "standard-headers/linux/virtio_blk.h"

#define VIRTIO_BLK_SECTOR_BITS 9
#define VIRTIO_BLK_SECTOR_SIZE (1ULL << VIRTIO_BLK_SECTOR_BITS)

#define VDUSE_DEFAULT_NUM_QUEUE 1
#define VDUSE_DEFAULT_QUEUE_SIZE 128

typedef struct VduseBlkExport {
    BlockExport export;
    VduseDev *dev;
    uint16_t num_queues;
    uint32_t blk_size;
    bool writable;
} VduseBlkExport;

struct virtio_blk_inhdr {
    unsigned char status;
};

typedef struct VduseBlkReq {
    VduseVirtqElement elem;
    int64_t sector_num;
    size_t in_len;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr out;
    VduseVirtq *vq;
} VduseBlkReq;

static void vduse_blk_req_complete(VduseBlkReq *req)
{
    vduse_queue_push(req->vq, &req->elem, req->in_len);
    vduse_queue_notify(req->vq);

    free(req);
}

static bool vduse_blk_sect_range_ok(VduseBlkExport *vblk_exp,
                                    uint64_t sector, size_t size)
{
    uint64_t nb_sectors;
    uint64_t total_sectors;

    if (size % VIRTIO_BLK_SECTOR_SIZE) {
        return false;
    }

    nb_sectors = size >> VIRTIO_BLK_SECTOR_BITS;

    QEMU_BUILD_BUG_ON(BDRV_SECTOR_SIZE != VIRTIO_BLK_SECTOR_SIZE);
    if (nb_sectors > BDRV_REQUEST_MAX_SECTORS) {
        return false;
    }
    if ((sector << VIRTIO_BLK_SECTOR_BITS) % vblk_exp->blk_size) {
        return false;
    }
    blk_get_geometry(vblk_exp->export.blk, &total_sectors);
    if (sector > total_sectors || nb_sectors > total_sectors - sector) {
        return false;
    }
    return true;
}

static void coroutine_fn vduse_blk_virtio_process_req(void *opaque)
{
    VduseBlkReq *req = opaque;
    VduseVirtq *vq = req->vq;
    VduseDev *dev = vduse_queue_get_dev(vq);
    VduseBlkExport *vblk_exp = vduse_dev_get_priv(dev);
    BlockBackend *blk = vblk_exp->export.blk;
    VduseVirtqElement *elem = &req->elem;
    struct iovec *in_iov = elem->in_sg;
    struct iovec *out_iov = elem->out_sg;
    unsigned in_num = elem->in_num;
    unsigned out_num = elem->out_num;
    uint32_t type;

    if (elem->out_num < 1 || elem->in_num < 1) {
        error_report("virtio-blk request missing headers");
        goto err;
    }

    if (unlikely(iov_to_buf(out_iov, out_num, 0, &req->out,
                            sizeof(req->out)) != sizeof(req->out))) {
        error_report("virtio-blk request outhdr too short");
        goto err;
    }

    iov_discard_front(&out_iov, &out_num, sizeof(req->out));

    if (in_iov[in_num - 1].iov_len < sizeof(struct virtio_blk_inhdr)) {
        error_report("virtio-blk request inhdr too short");
        goto err;
    }

    /* We always touch the last byte, so just see how big in_iov is. */
    req->in_len = iov_size(in_iov, in_num);
    req->in = (void *)in_iov[in_num - 1].iov_base
              + in_iov[in_num - 1].iov_len
              - sizeof(struct virtio_blk_inhdr);
    iov_discard_back(in_iov, &in_num, sizeof(struct virtio_blk_inhdr));

    type = le32_to_cpu(req->out.type);
    switch (type & ~VIRTIO_BLK_T_BARRIER) {
    case VIRTIO_BLK_T_IN:
    case VIRTIO_BLK_T_OUT: {
        QEMUIOVector qiov;
        int64_t offset;
        ssize_t ret = 0;
        bool is_write = type & VIRTIO_BLK_T_OUT;
        req->sector_num = le64_to_cpu(req->out.sector);

        if (is_write && !vblk_exp->writable) {
            req->in->status = VIRTIO_BLK_S_IOERR;
            break;
        }

        if (is_write) {
            qemu_iovec_init_external(&qiov, out_iov, out_num);
        } else {
            qemu_iovec_init_external(&qiov, in_iov, in_num);
        }

        if (unlikely(!vduse_blk_sect_range_ok(vblk_exp,
                                              req->sector_num,
                                              qiov.size))) {
            req->in->status = VIRTIO_BLK_S_IOERR;
            break;
        }

        offset = req->sector_num << VIRTIO_BLK_SECTOR_BITS;

        if (is_write) {
            ret = blk_co_pwritev(blk, offset, qiov.size, &qiov, 0);
        } else {
            ret = blk_co_preadv(blk, offset, qiov.size, &qiov, 0);
        }
        if (ret >= 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }
        break;
    }
    case VIRTIO_BLK_T_FLUSH:
        if (blk_co_flush(blk) == 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }
        break;
    case VIRTIO_BLK_T_GET_ID: {
        size_t size = MIN(iov_size(&elem->in_sg[0], in_num),
                          VIRTIO_BLK_ID_BYTES);
        snprintf(elem->in_sg[0].iov_base, size, "%s", vblk_exp->export.id);
        req->in->status = VIRTIO_BLK_S_OK;
        break;
    }
    default:
        req->in->status = VIRTIO_BLK_S_UNSUPP;
        break;
    }

    vduse_blk_req_complete(req);
    return;

err:
    free(req);
}

static void vduse_blk_vq_handler(VduseDev *dev, VduseVirtq *vq)
{
    while (1) {
        VduseBlkReq *req;

        req = vduse_queue_pop(vq, sizeof(VduseBlkReq));
        if (!req) {
            break;
        }
        req->vq = vq;

        Coroutine *co =
            qemu_coroutine_create(vduse_blk_virtio_process_req, req);
        qemu_coroutine_enter(co);
    }
}

static void on_vduse_vq_kick(void *opaque)
{
    VduseVirtq *vq = opaque;
    VduseDev *dev = vduse_queue_get_dev(vq);
    int fd = vduse_queue_get_fd(vq);
    eventfd_t kick_data;

    if (eventfd_read(fd, &kick_data) == -1) {
        error_report("failed to read data from eventfd");
        return;
    }

    vduse_blk_vq_handler(dev, vq);
}

static void vduse_blk_enable_queue(VduseDev *dev, VduseVirtq *vq)
{
    VduseBlkExport *vblk_exp = vduse_dev_get_priv(dev);

    aio_set_fd_handler(vblk_exp->export.ctx, vduse_queue_get_fd(vq),
                       true, on_vduse_vq_kick, NULL, NULL, NULL, vq);
    /* Make sure we don't miss any kick afer reconnecting */
    eventfd_write(vduse_queue_get_fd(vq), 1);
}

static void vduse_blk_disable_queue(VduseDev *dev, VduseVirtq *vq)
{
    VduseBlkExport *vblk_exp = vduse_dev_get_priv(dev);

    aio_set_fd_handler(vblk_exp->export.ctx, vduse_queue_get_fd(vq),
                       true, NULL, NULL, NULL, NULL, NULL);
}

static const VduseOps vduse_blk_ops = {
    .enable_queue = vduse_blk_enable_queue,
    .disable_queue = vduse_blk_disable_queue,
};

static void on_vduse_dev_kick(void *opaque)
{
    VduseDev *dev = opaque;

    vduse_dev_handler(dev);
}

static void blk_aio_attached(AioContext *ctx, void *opaque)
{
    VduseBlkExport *vblk_exp = opaque;
    int i;

    vblk_exp->export.ctx = ctx;

    aio_set_fd_handler(vblk_exp->export.ctx, vduse_dev_get_fd(vblk_exp->dev),
                       true, on_vduse_dev_kick, NULL, NULL, NULL,
                       vblk_exp->dev);

    for (i = 0; i < vblk_exp->num_queues; i++) {
        VduseVirtq *vq = vduse_dev_get_queue(vblk_exp->dev, i);
        int fd = vduse_queue_get_fd(vq);

        if (fd < 0) {
            continue;
        }
        aio_set_fd_handler(vblk_exp->export.ctx, fd, true,
                           on_vduse_vq_kick, NULL, NULL, NULL, vq);
    }
}

static void blk_aio_detach(void *opaque)
{
    VduseBlkExport *vblk_exp = opaque;
    int i;

    for (i = 0; i < vblk_exp->num_queues; i++) {
        VduseVirtq *vq = vduse_dev_get_queue(vblk_exp->dev, i);
        int fd = vduse_queue_get_fd(vq);

        if (fd < 0) {
            continue;
        }
        aio_set_fd_handler(vblk_exp->export.ctx, fd,
                           true, NULL, NULL, NULL, NULL, NULL);
    }
    aio_set_fd_handler(vblk_exp->export.ctx, vduse_dev_get_fd(vblk_exp->dev),
                       true, NULL, NULL, NULL, NULL, NULL);
    vblk_exp->export.ctx = NULL;
}

static void vduse_blk_resize(void *opaque)
{
    BlockExport *exp = opaque;
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);
    struct virtio_blk_config config;

    config.capacity =
            cpu_to_le64(blk_getlength(exp->blk) >> VIRTIO_BLK_SECTOR_BITS);
    vduse_dev_update_config(vblk_exp->dev, sizeof(config.capacity),
                            offsetof(struct virtio_blk_config, capacity),
                            (char *)&config.capacity);
}

static const BlockDevOps vduse_block_ops = {
    .resize_cb = vduse_blk_resize,
};

static int vduse_blk_exp_create(BlockExport *exp, BlockExportOptions *opts,
                                Error **errp)
{
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);
    BlockExportOptionsVduseBlk *vblk_opts = &opts->u.vduse_blk;
    uint64_t logical_block_size = VIRTIO_BLK_SECTOR_SIZE;
    uint16_t num_queues = VDUSE_DEFAULT_NUM_QUEUE;
    uint16_t queue_size = VDUSE_DEFAULT_QUEUE_SIZE;
    Error *local_err = NULL;
    struct virtio_blk_config config;
    uint64_t features;
    int i;

    if (vblk_opts->has_num_queues) {
        num_queues = vblk_opts->num_queues;
        if (num_queues == 0) {
            error_setg(errp, "num-queues must be greater than 0");
            return -EINVAL;
        }
    }

    if (vblk_opts->has_queue_size) {
        queue_size = vblk_opts->queue_size;
        if (queue_size == 0) {
            error_setg(errp, "queue-size must be greater than 0");
            return -EINVAL;
        }
    }

    if (vblk_opts->has_logical_block_size) {
        logical_block_size = vblk_opts->logical_block_size;
        check_block_size(exp->id, "logical-block-size", logical_block_size,
                         &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return -EINVAL;
        }
    }
    blk_set_guest_block_size(exp->blk, logical_block_size);

    vblk_exp->blk_size = logical_block_size;
    vblk_exp->writable = opts->writable;
    vblk_exp->num_queues = num_queues;

    config.capacity =
            cpu_to_le64(blk_getlength(exp->blk) >> VIRTIO_BLK_SECTOR_BITS);
    config.seg_max = cpu_to_le32(queue_size - 2);
    config.size_max = cpu_to_le32(0);
    config.min_io_size = cpu_to_le16(1);
    config.opt_io_size = cpu_to_le32(1);
    config.num_queues = cpu_to_le16(num_queues);
    config.blk_size = cpu_to_le32(logical_block_size);

    features = (1ULL << VIRTIO_F_IOMMU_PLATFORM) |
               (1ULL << VIRTIO_F_VERSION_1) |
               (1ULL << VIRTIO_RING_F_EVENT_IDX) |
               (1ULL << VIRTIO_F_NOTIFY_ON_EMPTY) |
               (1ULL << VIRTIO_RING_F_INDIRECT_DESC) |
               (1ULL << VIRTIO_BLK_F_SIZE_MAX) |
               (1ULL << VIRTIO_BLK_F_SEG_MAX) |
               (1ULL << VIRTIO_BLK_F_TOPOLOGY) |
               (1ULL << VIRTIO_BLK_F_BLK_SIZE);

    if (num_queues > 1) {
        features |= 1ULL << VIRTIO_BLK_F_MQ;
    }
    if (!vblk_exp->writable) {
        features |= 1ULL << VIRTIO_BLK_F_RO;
    }

    vblk_exp->dev = vduse_dev_create(exp->id, VIRTIO_ID_BLOCK, 0,
                                     features, num_queues,
                                     sizeof(struct virtio_blk_config),
                                     (char *)&config, &vduse_blk_ops,
                                     g_get_tmp_dir(), vblk_exp);
    if (!vblk_exp->dev) {
        error_setg(errp, "failed to create vduse device");
        return -ENOMEM;
    }

    for (i = 0; i < num_queues; i++) {
        vduse_dev_setup_queue(vblk_exp->dev, i, queue_size);
    }

    aio_set_fd_handler(exp->ctx, vduse_dev_get_fd(vblk_exp->dev), true,
                       on_vduse_dev_kick, NULL, NULL, NULL, vblk_exp->dev);

    blk_add_aio_context_notifier(exp->blk, blk_aio_attached, blk_aio_detach,
                                 vblk_exp);

    blk_set_dev_ops(exp->blk, &vduse_block_ops, exp);

    return 0;
}

static void vduse_blk_exp_delete(BlockExport *exp)
{
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);

    vduse_dev_destroy(vblk_exp->dev);
}

static void vduse_blk_exp_request_shutdown(BlockExport *exp)
{
    VduseBlkExport *vblk_exp = container_of(exp, VduseBlkExport, export);
    int i;

    blk_remove_aio_context_notifier(exp->blk, blk_aio_attached, blk_aio_detach,
                                    vblk_exp);

    for (i = 0; i < vblk_exp->num_queues; i++) {
        VduseVirtq *vq = vduse_dev_get_queue(vblk_exp->dev, i);
        int fd = vduse_queue_get_fd(vq);

        if (fd < 0) {
            continue;
        }
        aio_set_fd_handler(exp->ctx, fd, true, NULL, NULL, NULL, NULL, NULL);
    }
    aio_set_fd_handler(exp->ctx, vduse_dev_get_fd(vblk_exp->dev),
                       true, NULL, NULL, NULL, NULL, NULL);
}

const BlockExportDriver blk_exp_vduse_blk = {
    .type               = BLOCK_EXPORT_TYPE_VDUSE_BLK,
    .instance_size      = sizeof(VduseBlkExport),
    .create             = vduse_blk_exp_create,
    .delete             = vduse_blk_exp_delete,
    .request_shutdown   = vduse_blk_exp_request_shutdown,
};
