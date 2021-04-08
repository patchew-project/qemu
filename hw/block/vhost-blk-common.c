/*
 * Parent class for vhost based block devices
 *
 * Copyright (C) 2021 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *
 * Heavily based on the vhost-user-blk.c by:
 *   Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/vhost-blk-common.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"

static void vhost_blk_common_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(vdev);

    /* Our num_queues overrides the device backend */
    virtio_stw_p(vdev, &vbc->blkcfg.num_queues, vbc->num_queues);

    memcpy(config, &vbc->blkcfg, sizeof(struct virtio_blk_config));
}

static void vhost_blk_common_set_config(VirtIODevice *vdev,
                                        const uint8_t *config)
{
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(vdev);
    struct virtio_blk_config *blkcfg = (struct virtio_blk_config *)config;
    int ret;

    if (blkcfg->wce == vbc->blkcfg.wce) {
        return;
    }

    ret = vhost_dev_set_config(&vbc->dev, &blkcfg->wce,
                               offsetof(struct virtio_blk_config, wce),
                               sizeof(blkcfg->wce),
                               VHOST_SET_CONFIG_TYPE_MASTER);
    if (ret) {
        error_report("set device config space failed");
        return;
    }

    vbc->blkcfg.wce = blkcfg->wce;
}

static int vhost_blk_common_handle_config_change(struct vhost_dev *dev)
{
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(dev->vdev);
    struct virtio_blk_config blkcfg;
    int ret;

    ret = vhost_dev_get_config(dev, (uint8_t *)&blkcfg,
                               sizeof(struct virtio_blk_config));
    if (ret < 0) {
        error_report("get config space failed");
        return ret;
    }

    /* valid for resize only */
    if (blkcfg.capacity != vbc->blkcfg.capacity) {
        vbc->blkcfg.capacity = blkcfg.capacity;
        memcpy(dev->vdev->config, &vbc->blkcfg,
               sizeof(struct virtio_blk_config));
        virtio_notify_config(dev->vdev);
    }

    return 0;
}

const VhostDevConfigOps blk_ops = {
    .vhost_dev_config_notifier = vhost_blk_common_handle_config_change,
};

static uint64_t vhost_blk_common_get_features(VirtIODevice *vdev,
                                              uint64_t features,
                                              Error **errp)
{
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(vdev);

    /* Turn on pre-defined features */
    virtio_add_feature(&features, VIRTIO_BLK_F_SEG_MAX);
    virtio_add_feature(&features, VIRTIO_BLK_F_GEOMETRY);
    virtio_add_feature(&features, VIRTIO_BLK_F_TOPOLOGY);
    virtio_add_feature(&features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_add_feature(&features, VIRTIO_BLK_F_FLUSH);
    virtio_add_feature(&features, VIRTIO_BLK_F_RO);
    virtio_add_feature(&features, VIRTIO_BLK_F_DISCARD);
    virtio_add_feature(&features, VIRTIO_BLK_F_WRITE_ZEROES);

    if (vbc->config_wce) {
        virtio_add_feature(&features, VIRTIO_BLK_F_CONFIG_WCE);
    }
    if (vbc->num_queues > 1) {
        virtio_add_feature(&features, VIRTIO_BLK_F_MQ);
    }

    return vhost_get_features(&vbc->dev, vbc->feature_bits, features);
}

int vhost_blk_common_start(VHostBlkCommon *vbc)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vbc);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i, ret;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&vbc->dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, vbc->dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    vbc->dev.acked_features = vdev->guest_features;

    ret = vhost_dev_prepare_inflight(&vbc->dev, vdev);
    if (ret < 0) {
        error_report("Error set inflight format: %d", -ret);
        goto err_guest_notifiers;
    }

    if (!vbc->inflight->addr) {
        ret = vhost_dev_get_inflight(&vbc->dev, vbc->queue_size, vbc->inflight);
        if (ret < 0) {
            error_report("Error get inflight: %d", -ret);
            goto err_guest_notifiers;
        }
    }

    ret = vhost_dev_set_inflight(&vbc->dev, vbc->inflight);
    if (ret < 0) {
        error_report("Error set inflight: %d", -ret);
        goto err_guest_notifiers;
    }

    ret = vhost_dev_start(&vbc->dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }
    vbc->started = true;

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < vbc->dev.nvqs; i++) {
        vhost_virtqueue_mask(&vbc->dev, vdev, i, false);
    }

    return ret;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, vbc->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&vbc->dev, vdev);
    return ret;
}

void vhost_blk_common_stop(VHostBlkCommon *vbc)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vbc);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!vbc->started) {
        return;
    }
    vbc->started = false;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&vbc->dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, vbc->dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&vbc->dev, vdev);
}

void vhost_blk_common_realize(VHostBlkCommon *vbc,
                              VirtIOHandleOutput handle_output,
                              Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vbc);
    int i;

    if (vbc->num_queues == VHOST_BLK_AUTO_NUM_QUEUES) {
        vbc->num_queues = 1;
    }

    if (!vbc->num_queues || vbc->num_queues > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "vhost-blk-common: invalid number of IO queues");
        return;
    }

    if (!vbc->queue_size) {
        error_setg(errp, "vhost-blk-common: queue size must be non-zero");
        return;
    }

    virtio_init(vdev, "virtio-blk", VIRTIO_ID_BLOCK,
                sizeof(struct virtio_blk_config));

    vbc->virtqs = g_new(VirtQueue *, vbc->num_queues);
    for (i = 0; i < vbc->num_queues; i++) {
        vbc->virtqs[i] = virtio_add_queue(vdev, vbc->queue_size,
                                          handle_output);
    }

    vbc->inflight = g_new0(struct vhost_inflight, 1);
    vbc->vhost_vqs = g_new0(struct vhost_virtqueue, vbc->num_queues);
}

void vhost_blk_common_unrealize(VHostBlkCommon *vbc)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vbc);
    int i;

    g_free(vbc->vhost_vqs);
    vbc->vhost_vqs = NULL;
    g_free(vbc->inflight);
    vbc->inflight = NULL;

    for (i = 0; i < vbc->num_queues; i++) {
        virtio_delete_queue(vbc->virtqs[i]);
    }
    g_free(vbc->virtqs);
    virtio_cleanup(vdev);
}

static void vhost_blk_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->get_config = vhost_blk_common_update_config;
    vdc->set_config = vhost_blk_common_set_config;
    vdc->get_features = vhost_blk_common_get_features;
}

static const TypeInfo vhost_blk_common_info = {
    .name = TYPE_VHOST_BLK_COMMON,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostBlkCommon),
    .class_init = vhost_blk_common_class_init,
    .abstract = true,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_blk_common_info);
}

type_init(virtio_register_types)
