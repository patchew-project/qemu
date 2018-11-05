/*
 * vhost-blk host device
 *
 * Copyright(C) 2018 IBM Corporation
 *
 * Authors:
 *  Vitaly Mayatskikh <v.mayatskih@gmail.com>
 *
 * Largely based on the "vhost-user-blk.c" implemented by:
 * Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include <sys/ioctl.h>
#include <linux/fs.h>

static const int feature_bits[] = {
    VIRTIO_BLK_F_SIZE_MAX,
    VIRTIO_BLK_F_SEG_MAX,
    VIRTIO_BLK_F_BLK_SIZE,
    VIRTIO_BLK_F_TOPOLOGY,
    VIRTIO_BLK_F_MQ,
    VIRTIO_BLK_F_RO,
    VIRTIO_BLK_F_FLUSH,
    VIRTIO_BLK_F_CONFIG_WCE,
    VIRTIO_F_VERSION_1,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VHOST_INVALID_FEATURE_BIT
};

static void vhost_blk_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostBlk *s = VHOST_BLK(vdev);
    memcpy(config, &s->blkcfg, sizeof(struct virtio_blk_config));
}

static void vhost_blk_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VHostBlk *s = VHOST_BLK(vdev);
    struct virtio_blk_config *blkcfg = (struct virtio_blk_config *)config;
    int ret;

    if (blkcfg->wce == s->blkcfg.wce) {
        return;
    }

    ret = vhost_dev_set_config(&s->dev, &blkcfg->wce,
                               offsetof(struct virtio_blk_config, wce),
                               sizeof(blkcfg->wce),
                               VHOST_SET_CONFIG_TYPE_MASTER);
    if (ret) {
        error_report("set device config space failed");
        return;
    }

    s->blkcfg.wce = blkcfg->wce;
}

static int vhost_blk_handle_config_change(struct vhost_dev *dev)
{
    int ret;
    struct virtio_blk_config blkcfg;
    VHostBlk *s = VHOST_BLK(dev->vdev);

    ret = vhost_dev_get_config(dev, (uint8_t *)&blkcfg,
                               sizeof(struct virtio_blk_config));
    if (ret < 0) {
        error_report("get config space failed");
        return -1;
    }

    /* valid for resize only */
    if (blkcfg.capacity != s->blkcfg.capacity) {
        s->blkcfg.capacity = blkcfg.capacity;
        memcpy(dev->vdev->config, &s->blkcfg, sizeof(struct virtio_blk_config));
        virtio_notify_config(dev->vdev);
    }

    return 0;
}

const VhostDevConfigOps vhost_blk_ops = {
    .vhost_dev_config_notifier = vhost_blk_handle_config_change,
};

static void vhost_blk_start(VirtIODevice *vdev)
{
    VHostBlk *s = VHOST_BLK(vdev);
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
    printf("hdev %p vqs %d\n", &s->dev, s->dev.nvqs);
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

    s->bs_fd = open(blk_bs(s->blk)->filename, O_RDWR);
    if (s->bs_fd < 0) {
        error_report("Error opening backing store: %d\n", -errno);
        goto err_ioctl;
    }
    ret = ioctl(s->vhostfd, _IOW(0xaf, 0x50, int), &s->bs_fd);
    if (ret < 0) {
        error_report("Error setting up backend: %d", -errno);
        goto err_ioctl;
    }

    return;

err_ioctl:
    if (s->bs_fd > 0)
        close(s->bs_fd);
    vhost_dev_stop(&s->dev, vdev);
err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&s->dev, vdev);
}

static void vhost_blk_stop(VirtIODevice *vdev)
{
    VHostBlk *s = VHOST_BLK(vdev);
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
    close(s->bs_fd);
}

static void vhost_blk_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostBlk *s = VHOST_BLK(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (s->dev.started == should_start) {
        return;
    }

    if (should_start) {
        vhost_blk_start(vdev);
    } else {
        vhost_blk_stop(vdev);
    }

}

static uint64_t vhost_blk_get_features(VirtIODevice *vdev,
                                       uint64_t features,
                                       Error **errp)
{
    VHostBlk *s = VHOST_BLK(vdev);

    /* Turn on pre-defined features */
    virtio_add_feature(&features, VIRTIO_BLK_F_SIZE_MAX);
    virtio_add_feature(&features, VIRTIO_BLK_F_SEG_MAX);
    virtio_add_feature(&features, VIRTIO_BLK_F_TOPOLOGY);
    virtio_add_feature(&features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_add_feature(&features, VIRTIO_BLK_F_FLUSH);
    virtio_add_feature(&features, VIRTIO_BLK_F_RO);

    if (s->config_wce) {
        virtio_add_feature(&features, VIRTIO_BLK_F_CONFIG_WCE);
    }
    if (s->num_queues > 1) {
        virtio_add_feature(&features, VIRTIO_BLK_F_MQ);
    }

    return vhost_get_features(&s->dev, feature_bits, features);
}

static void vhost_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
}

static int vhost_blk_cfg_init(VHostBlk *s)
{
    int ret;
    uint64_t var64;
    int var;
    int fd = - 1;

    fd = open(blk_bs(s->blk)->filename, O_RDWR);
    if (fd < 0) {
	    error_report("Can't open device %s: %d\n", blk_bs(s->blk)->filename, errno);
	    goto out;
    }
    ret = ioctl(fd, BLKGETSIZE64, &var64);
    if (ret != 0 && (errno == ENOTTY)) {
	    ret = ioctl(fd, BLKGETSIZE, &var);
	    var64 = var;
    }
    if (ret != 0) {
	    error_report("Can't get drive size: %d\n", errno);
	    goto out;
    }
    s->blkcfg.capacity = var64 / 512;

    ret = ioctl(fd, BLKSSZGET, &var);
    if (ret != 0) {
	    error_report("Can't get drive logical sector size, assuming 512: %d\n", errno);
	    var = 512;
    }
    s->blkcfg.blk_size = var;
    s->blkcfg.physical_block_exp = 0;
    s->blkcfg.num_queues = s->num_queues;
    /* TODO query actual block device */
    s->blkcfg.size_max = 8192;
    s->blkcfg.seg_max = 8192 / 512;
    s->blkcfg.min_io_size = 512;
    s->blkcfg.opt_io_size = 8192;

out:
    if (fd > 0)
	    close(fd);
    return ret;
}

static void vhost_blk_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostBlk *s = VHOST_BLK(vdev);
    int i, ret;

    if (!s->blk) {
        error_setg(errp, "drive property not set");
        return;
    }
    if (!blk_is_inserted(s->blk)) {
        error_setg(errp, "Device needs media, but drive is empty");
        return;
    }

    if (!s->num_queues || s->num_queues > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "vhost-blk: invalid number of IO queues");
        return;
    }

    if (!s->queue_size) {
        error_setg(errp, "vhost-blk: queue size must be non-zero");
        return;
    }

    virtio_init(vdev, "virtio-blk", VIRTIO_ID_BLOCK,
                sizeof(struct virtio_blk_config));

    s->dev.max_queues = s->num_queues;
    s->dev.nvqs = s->num_queues;
    s->dev.vqs = g_new(struct vhost_virtqueue, s->dev.nvqs);
    s->dev.vq_index = 0;
    s->dev.backend_features = 0;

    vhost_dev_set_config_notifier(&s->dev, &vhost_blk_ops);

    for (i = 0; i < s->dev.max_queues; i++) {
        virtio_add_queue(vdev, s->queue_size,
                         vhost_blk_handle_output);
    }

    s->vhostfd = open("/dev/vhost-blk", O_RDWR);
    if (s->vhostfd < 0) {
        error_setg_errno(errp, -errno,
                        "vhost-blk: failed to open vhost device");
	goto virtio_err;
    }

    ret = vhost_dev_init(&s->dev, (void *)(uintptr_t)s->vhostfd, VHOST_BACKEND_TYPE_KERNEL, 0);
    if (ret < 0) {
        error_setg(errp, "vhost-blk: vhost initialization failed: %s",
                   strerror(-ret));
        goto virtio_err;
    }

    vhost_blk_cfg_init(s);
    blk_iostatus_enable(s->blk);
    return;

virtio_err:
    g_free(s->dev.vqs);
    virtio_cleanup(vdev);
    close(s->vhostfd);
}

static void vhost_blk_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostBlk *s = VHOST_BLK(dev);

    vhost_blk_set_status(vdev, 0);
    close(s->vhostfd);
    vhost_dev_cleanup(&s->dev);
    g_free(s->dev.vqs);
    virtio_cleanup(vdev);
}

static void vhost_blk_instance_init(Object *obj)
{
    VHostBlk *s = VHOST_BLK(obj);

    device_add_bootindex_property(obj, &s->bootindex, "bootindex",
                                  "/disk@0,0", DEVICE(obj), NULL);
}

static const VMStateDescription vmstate_vhost_blk = {
    .name = "vhost-blk",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property vhost_blk_properties[] = {
    DEFINE_PROP_DRIVE("drive", VHostBlk, blk),
    DEFINE_PROP_UINT16("num-queues", VHostBlk, num_queues, 1),
    DEFINE_PROP_UINT32("queue-size", VHostBlk, queue_size, 128),
    DEFINE_PROP_BIT("config-wce", VHostBlk, config_wce, 0, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vhost_blk_properties;
    dc->vmsd = &vmstate_vhost_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_blk_device_realize;
    vdc->unrealize = vhost_blk_device_unrealize;
    vdc->get_config = vhost_blk_get_config;
    vdc->set_config = vhost_blk_set_config;
    vdc->get_features = vhost_blk_get_features;
    vdc->set_status = vhost_blk_set_status;
}

static const TypeInfo vhost_blk_info = {
    .name = TYPE_VHOST_BLK,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostBlk),
    .instance_init = vhost_blk_instance_init,
    .class_init = vhost_blk_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_blk_info);
}

type_init(virtio_register_types)
