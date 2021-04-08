/*
 * vhost-vdpa-blk host device
 *
 * Copyright (C) 2021 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
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
#include "hw/virtio/vhost-vdpa-blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"

static const int vdpa_feature_bits[] = {
    VIRTIO_BLK_F_SIZE_MAX,
    VIRTIO_BLK_F_SEG_MAX,
    VIRTIO_BLK_F_GEOMETRY,
    VIRTIO_BLK_F_BLK_SIZE,
    VIRTIO_BLK_F_TOPOLOGY,
    VIRTIO_BLK_F_MQ,
    VIRTIO_BLK_F_RO,
    VIRTIO_BLK_F_FLUSH,
    VIRTIO_BLK_F_CONFIG_WCE,
    VIRTIO_BLK_F_DISCARD,
    VIRTIO_BLK_F_WRITE_ZEROES,
    VIRTIO_F_VERSION_1,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VHOST_INVALID_FEATURE_BIT
};

static void vhost_vdpa_blk_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostVdpaBlk *s = VHOST_VDPA_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);
    bool should_start = virtio_device_started(vdev, status);
    int ret;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (vbc->dev.started == should_start) {
        return;
    }

    if (should_start) {
        ret = vhost_blk_common_start(vbc);
        if (ret < 0) {
            error_report("vhost-vdpa-blk: vhost start failed: %s",
                         strerror(-ret));
        }
    } else {
        vhost_blk_common_stop(vbc);
    }

}

static void vhost_vdpa_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VHostVdpaBlk *s = VHOST_VDPA_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);
    int i, ret;

    if (!vdev->start_on_kick) {
        return;
    }

    if (vbc->dev.started) {
        return;
    }

    ret = vhost_blk_common_start(vbc);
    if (ret < 0) {
        error_report("vhost-vdpa-blk: vhost start failed: %s",
                     strerror(-ret));
        return;
    }

    /* Kick right away to begin processing requests already in vring */
    for (i = 0; i < vbc->dev.nvqs; i++) {
        VirtQueue *kick_vq = virtio_get_queue(vdev, i);

        if (!virtio_queue_get_desc_addr(vdev, i)) {
            continue;
        }
        event_notifier_set(virtio_queue_get_host_notifier(kick_vq));
    }
}

static void vhost_vdpa_blk_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostVdpaBlk *s = VHOST_VDPA_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);
    Error *err = NULL;
    int ret;

    s->vdpa.device_fd = qemu_open_old(s->vdpa_dev, O_RDWR);
    if (s->vdpa.device_fd == -1) {
        error_setg(errp, "vhost-vdpa-blk: open %s failed: %s",
                   s->vdpa_dev, strerror(errno));
        return;
    }

    vhost_blk_common_realize(vbc, vhost_vdpa_blk_handle_output, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        goto blk_err;
    }

    vbc->vhost_vqs = g_new0(struct vhost_virtqueue, vbc->num_queues);
    vbc->dev.nvqs = vbc->num_queues;
    vbc->dev.vqs = vbc->vhost_vqs;
    vbc->dev.vq_index = 0;
    vbc->dev.backend_features = 0;
    vbc->started = false;

    vhost_dev_set_config_notifier(&vbc->dev, &blk_ops);

    ret = vhost_dev_init(&vbc->dev, &s->vdpa, VHOST_BACKEND_TYPE_VDPA, 0);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-blk: vhost initialization failed: %s",
                   strerror(-ret));
        goto init_err;
    }

    ret = vhost_dev_get_config(&vbc->dev, (uint8_t *)&vbc->blkcfg,
                               sizeof(struct virtio_blk_config));
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-blk: get block config failed");
        goto config_err;
    }

    return;
config_err:
    vhost_dev_cleanup(&vbc->dev);
init_err:
    vhost_blk_common_unrealize(vbc);
blk_err:
    close(s->vdpa.device_fd);
}

static void vhost_vdpa_blk_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostVdpaBlk *s = VHOST_VDPA_BLK(dev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);

    virtio_set_status(vdev, 0);
    vhost_dev_cleanup(&vbc->dev);
    vhost_blk_common_unrealize(vbc);
    close(s->vdpa.device_fd);
}

static void vhost_vdpa_blk_instance_init(Object *obj)
{
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(obj);

    vbc->feature_bits = vdpa_feature_bits;

    device_add_bootindex_property(obj, &vbc->bootindex, "bootindex",
                                  "/disk@0,0", DEVICE(obj));
}

static const VMStateDescription vmstate_vhost_vdpa_blk = {
    .name = "vhost-vdpa-blk",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property vhost_vdpa_blk_properties[] = {
    DEFINE_PROP_STRING("vdpa-dev", VHostVdpaBlk, vdpa_dev),
    DEFINE_PROP_UINT16("num-queues", VHostBlkCommon, num_queues,
                       VHOST_BLK_AUTO_NUM_QUEUES),
    DEFINE_PROP_UINT32("queue-size", VHostBlkCommon, queue_size, 256),
    DEFINE_PROP_BIT("config-wce", VHostBlkCommon, config_wce, 0, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_vdpa_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_vdpa_blk_properties);
    dc->vmsd = &vmstate_vhost_vdpa_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_vdpa_blk_device_realize;
    vdc->unrealize = vhost_vdpa_blk_device_unrealize;
    vdc->set_status = vhost_vdpa_blk_set_status;
}

static const TypeInfo vhost_vdpa_blk_info = {
    .name = TYPE_VHOST_VDPA_BLK,
    .parent = TYPE_VHOST_BLK_COMMON,
    .instance_size = sizeof(VHostVdpaBlk),
    .instance_init = vhost_vdpa_blk_instance_init,
    .class_init = vhost_vdpa_blk_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_vdpa_blk_info);
}

type_init(virtio_register_types)
