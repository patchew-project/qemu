/*
 * vhost-user-blk host device
 *
 * Copyright IBM, Corp. 2011
 * Copyright(C) 2017 Intel Corporation.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "migration/migration.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/typedefs.h"
#include "qemu/cutils.h"
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user-blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"

static const int user_feature_bits[] = {
    VIRTIO_BLK_F_SIZE_MAX,
    VIRTIO_BLK_F_SEG_MAX,
    VIRTIO_BLK_F_GEOMETRY,
    VIRTIO_BLK_F_BLK_SIZE,
    VIRTIO_BLK_F_TOPOLOGY,
    VIRTIO_BLK_F_SCSI,
    VIRTIO_BLK_F_MQ,
    VIRTIO_BLK_F_RO,
    VIRTIO_BLK_F_FLUSH,
    VIRTIO_BLK_F_BARRIER,
    VIRTIO_BLK_F_CONFIG_WCE,
    VIRTIO_F_VERSION_1,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VHOST_INVALID_FEATURE_BIT
};

static void vhost_user_blk_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    int blk_size = s->blkcfg.logical_block_size;
    struct virtio_blk_config blkcfg;

    memset(&blkcfg, 0, sizeof(blkcfg));

    virtio_stq_p(vdev, &blkcfg.capacity, s->capacity);
    virtio_stl_p(vdev, &blkcfg.seg_max, s->max_segment_num - 2);
    virtio_stl_p(vdev, &blkcfg.size_max, s->max_segment_size);
    virtio_stl_p(vdev, &blkcfg.blk_size, blk_size);
    virtio_stw_p(vdev, &blkcfg.min_io_size, s->blkcfg.min_io_size / blk_size);
    virtio_stl_p(vdev, &blkcfg.opt_io_size, s->blkcfg.opt_io_size / blk_size);
    virtio_stw_p(vdev, &blkcfg.num_queues, s->num_queues);
    virtio_stw_p(vdev, &blkcfg.geometry.cylinders,
                 s->blkcfg.cyls);
    blkcfg.geometry.heads = s->blkcfg.heads;
    blkcfg.geometry.sectors = s->blkcfg.secs;
    blkcfg.physical_block_exp = get_physical_block_exp(&s->blkcfg);
    blkcfg.alignment_offset = 0;
    blkcfg.wce = s->config_wce;

    memcpy(config, &blkcfg, sizeof(struct virtio_blk_config));
}

static void vhost_user_blk_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    struct virtio_blk_config blkcfg;

    memcpy(&blkcfg, config, sizeof(blkcfg));

    if (blkcfg.wce != s->config_wce) {
        error_report("vhost-user-blk: does not support the operation");
    }
}

static void vhost_user_blk_start(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i, ret;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&s->dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    s->dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&s->dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < s->dev.nvqs; i++) {
        vhost_virtqueue_mask(&s->dev, vdev, i, false);
    }

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&s->dev, vdev);
}

static void vhost_user_blk_stop(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&s->dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&s->dev, vdev);
}

static void vhost_user_blk_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (s->dev.started == should_start) {
        return;
    }

    if (should_start) {
        vhost_user_blk_start(vdev);
    } else {
        vhost_user_blk_stop(vdev);
    }

}

static uint64_t vhost_user_blk_get_features(VirtIODevice *vdev,
                                            uint64_t features,
                                            Error **errp)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    uint64_t get_features;

    /* Turn on pre-defined features */
    features |= s->host_features;

    get_features = vhost_get_features(&s->dev, user_feature_bits, features);

    return get_features;
}

static void vhost_user_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{

}

static void vhost_user_blk_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    int ret;
    uint64_t size;

    if (!s->chardev.chr) {
        error_setg(errp, "vhost-user-blk: chardev is mandatary");
        return;
    }

    if (!s->num_queues) {
        error_setg(errp, "vhost-user-blk: invalid number of IO queues");
        return;
    }

    if (!s->queue_size) {
        error_setg(errp, "vhost-user-blk: invalid count of the IO queue");
        return;
    }

    if (!s->size) {
        error_setg(errp, "vhost-user-blk: block capacity must be assigned,"
                  "size can be specified by GiB or MiB");
        return;
    }

    ret = qemu_strtosz_MiB(s->size, NULL, &size);
    if (ret < 0) {
        error_setg(errp, "vhost-user-blk: invalid size %s in GiB/MiB", s->size);
        return;
    }
    s->capacity = size / 512;

    /* block size with default 512 Bytes */
    if (!s->blkcfg.logical_block_size) {
        s->blkcfg.logical_block_size = 512;
    }

    virtio_init(vdev, "virtio-blk", VIRTIO_ID_BLOCK,
                sizeof(struct virtio_blk_config));
    virtio_add_queue(vdev, s->queue_size, vhost_user_blk_handle_output);

    s->dev.nvqs = s->num_queues;
    s->dev.vqs = g_new(struct vhost_virtqueue, s->dev.nvqs);
    s->dev.vq_index = 0;
    s->dev.backend_features = 0;

    ret = vhost_dev_init(&s->dev, (void *)&s->chardev,
                         VHOST_BACKEND_TYPE_USER, 0);
    if (ret < 0) {
        error_setg(errp, "vhost-user-blk: vhost initialization failed: %s",
                   strerror(-ret));
        return;
    }
}

static void vhost_user_blk_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(dev);

    vhost_user_blk_set_status(vdev, 0);
    vhost_dev_cleanup(&s->dev);
    g_free(s->dev.vqs);
    virtio_cleanup(vdev);
}

static void vhost_user_blk_instance_init(Object *obj)
{
    VHostUserBlk *s = VHOST_USER_BLK(obj);

    device_add_bootindex_property(obj, &s->bootindex, "bootindex",
                                  "/disk@0,0", DEVICE(obj), NULL);
}

static const VMStateDescription vmstate_vhost_user_blk = {
    .name = "vhost-user-blk",
    .minimum_version_id = 2,
    .version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property vhost_user_blk_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),
    DEFINE_BLOCK_PROPERTIES(VHostUserBlk, blkcfg),
    DEFINE_BLOCK_CHS_PROPERTIES(VHostUserBlk, blkcfg),
    DEFINE_PROP_STRING("size", VHostUserBlk, size),
    DEFINE_PROP_UINT16("num_queues", VHostUserBlk, num_queues, 1),
    DEFINE_PROP_UINT32("queue_size", VHostUserBlk, queue_size, 128),
    DEFINE_PROP_UINT32("max_segment_size", VHostUserBlk, max_segment_size,
                       131072),
    DEFINE_PROP_UINT32("max_segment_num", VHostUserBlk, max_segment_num, 34),
    DEFINE_PROP_BIT("config_wce", VHostUserBlk, config_wce, 0, false),
    DEFINE_PROP_BIT64("f_size_max", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_SIZE_MAX, true),
    DEFINE_PROP_BIT64("f_sizemax", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_SIZE_MAX, true),
    DEFINE_PROP_BIT64("f_segmax", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_SEG_MAX, true),
    DEFINE_PROP_BIT64("f_geometry", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_GEOMETRY, true),
    DEFINE_PROP_BIT64("f_readonly", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_RO, false),
    DEFINE_PROP_BIT64("f_blocksize", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_BLK_SIZE, true),
    DEFINE_PROP_BIT64("f_topology", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_TOPOLOGY, true),
    DEFINE_PROP_BIT64("f_multiqueue", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_MQ, true),
    DEFINE_PROP_BIT64("f_flush", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_FLUSH, true),
    DEFINE_PROP_BIT64("f_barrier", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_BARRIER, false),
    DEFINE_PROP_BIT64("f_scsi", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_SCSI, false),
    DEFINE_PROP_BIT64("f_writecache", VHostUserBlk, host_features,
                      VIRTIO_BLK_F_CONFIG_WCE, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vhost_user_blk_properties;
    dc->vmsd = &vmstate_vhost_user_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_user_blk_device_realize;
    vdc->unrealize = vhost_user_blk_device_unrealize;
    vdc->get_config = vhost_user_blk_update_config;
    vdc->set_config = vhost_user_blk_set_config;
    vdc->get_features = vhost_user_blk_get_features;
    vdc->set_status = vhost_user_blk_set_status;
}

static const TypeInfo vhost_user_blk_info = {
    .name = TYPE_VHOST_USER_BLK,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserBlk),
    .instance_init = vhost_user_blk_instance_init,
    .class_init = vhost_user_blk_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_user_blk_info);
}

type_init(virtio_register_types)
