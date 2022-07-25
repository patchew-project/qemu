/*
 * Copyright (c) 2022 Virtuozzo International GmbH.
 * Author: Andrey Zhadchenko <andrey.zhadchenko@virtuozzo.com>
 *
 * vhost-blk is host kernel accelerator for virtio-blk.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/boards.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-blk.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/virtio-pci.h"
#include "sysemu/sysemu.h"
#include "linux-headers/linux/vhost.h"
#include <sys/ioctl.h>
#include <linux/fs.h>

static int vhost_blk_start(VirtIODevice *vdev)
{
    VHostBlk *s = VHOST_BLK(vdev);
    struct vhost_vring_file backend;
    int ret, i;
    int *fd = blk_bs(s->conf.conf.blk)->file->bs->opaque;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (!k->set_guest_notifiers) {
        error_report("vhost-blk: binding does not support guest notifiers");
        return -ENOSYS;
    }

    if (s->vhost_started) {
        return 0;
    }

    if (ioctl(s->vhostfd, VHOST_SET_OWNER, NULL)) {
        error_report("vhost-blk: unable to set owner");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&s->dev, vdev);
    if (ret < 0) {
        error_report("vhost-blk: unable to enable dev notifiers", errno);
        return ret;
    }

    s->dev.acked_features = vdev->guest_features & s->dev.backend_features;

    ret = vhost_dev_start(&s->dev, vdev);
    if (ret < 0) {
        error_report("vhost-blk: unable to start vhost dev");
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, true);
    if (ret < 0) {
        error_report("vhost-blk: unable to bind guest notifiers");
        goto out;
    }

    memset(&backend, 0, sizeof(backend));
    backend.index = 0;
    backend.fd = *fd;
    if (ioctl(s->vhostfd, VHOST_BLK_SET_BACKEND, &backend)) {
        error_report("vhost-blk: unable to set backend");
        ret = -errno;
        goto out;
    }

    for (i = 0; i < s->dev.nvqs; i++) {
        vhost_virtqueue_mask(&s->dev, vdev, i, false);
    }

    event_notifier_set(virtio_queue_get_host_notifier(virtio_get_queue(vdev, 0)));

    s->vhost_started = true;

    return 0;

out:
    vhost_dev_stop(&s->dev, vdev);
    return ret;

}

static void vhost_blk_stop(VirtIODevice *vdev)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VHostBlk *s = VHOST_BLK(vdev);
    int ret;

    if (!s->vhost_started) {
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost-blk: unable to unbind guest notifiers");
    }
    vhost_dev_disable_notifiers(&s->dev, vdev);
    vhost_dev_stop(&s->dev, vdev);

    s->vhost_started = false;
}

static void vhost_blk_reset(VirtIODevice *vdev)
{
    VHostBlk *s = VHOST_BLK(vdev);
    int ret;

    vhost_blk_stop(vdev);
    ret = ioctl(s->vhostfd, VHOST_RESET_OWNER, NULL);
    if (ret && errno != EPERM) {
            error_report("vhost-blk: failed to reset owner %d", errno);
    }
}

static void vhost_blk_set_status(VirtIODevice *vdev, uint8_t status)
{
    if (status & (VIRTIO_CONFIG_S_NEEDS_RESET | VIRTIO_CONFIG_S_FAILED)) {
        vhost_blk_stop(vdev);
        return;
    }

    if (!(status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return;
    }

    if (vhost_blk_start(vdev)) {
        error_report("vhost-blk: failed to start");
    }
}

static void vhost_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void vhost_blk_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostBlk *s = VHOST_BLK(vdev);
    VhostBlkConf *conf = &s->conf;
    int i, ret;

    if (!conf->conf.blk) {
        error_setg(errp, "vhost-blk: drive property not set");
        return;
    }

    if (!blk_is_inserted(conf->conf.blk)) {
        error_setg(errp, "vhost-blk: device needs media, but drive is empty");
        return;
    }

    if (conf->num_queues == VHOST_BLK_AUTO_NUM_QUEUES) {
        conf->num_queues = MIN(virtio_pci_optimal_num_queues(0),
                               VHOST_BLK_MAX_QUEUES);
    }

    if (!conf->num_queues) {
        error_setg(errp, "vhost-blk: num-queues property must be larger than 0");
        return;
    }

    if (conf->queue_size <= 2) {
        error_setg(errp, "vhost-blk: invalid queue-size property (%" PRIu16 "), "
                   "must be > 2", conf->queue_size);
        return;
    }

    if (!is_power_of_2(conf->queue_size) ||
        conf->queue_size > VIRTQUEUE_MAX_SIZE) {
        error_setg(errp, "vhost_blk: invalid queue-size property (%" PRIu16 "), "
                   "must be a power of 2 (max %d)",
                   conf->queue_size, VIRTQUEUE_MAX_SIZE);
        return;
    }

    if (!blkconf_apply_backend_options(&conf->conf,
                                       !blk_supports_write_perm(conf->conf.blk),
                                       true, errp)) {
        return;
    }

    if (!blkconf_geometry(&conf->conf, NULL, 65535, 255, 255, errp)) {
        return;
    }

    if (!blkconf_blocksizes(&conf->conf, errp)) {
        return;
    }

    s->dev.nvqs = conf->num_queues;
    s->dev.max_queues = conf->num_queues;
    s->dev.vqs = g_new(struct vhost_virtqueue, s->dev.nvqs);
    s->dev.vq_index = 0;

    virtio_init(vdev, VIRTIO_ID_BLOCK, sizeof(struct virtio_blk_config));

    for (i = 0; i < conf->num_queues; i++) {
        virtio_add_queue(vdev, conf->queue_size, vhost_blk_handle_output);
    }

    s->vhostfd = open("/dev/vhost-blk", O_RDWR);
    if (s->vhostfd < 0) {
        error_setg(errp, "vhost-blk: unable to open /dev/vhost-blk");
        goto cleanup;
    }

    s->dev.acked_features = 0;
    ret = ioctl(s->vhostfd, VHOST_GET_FEATURES, &s->dev.backend_features);
    if (ret < 0) {
        error_setg(errp, "vhost-blk: unable to get backend features");
        goto cleanup;
    }

    ret = vhost_dev_init(&s->dev, (void *)((size_t)s->vhostfd),
                         VHOST_BACKEND_TYPE_KERNEL, 0, false);
    if (ret < 0) {
        error_setg(errp, "vhost-blk: vhost initialization failed: %s",
                strerror(-ret));
        goto cleanup;
    }

    return;

cleanup:
    g_free(s->dev.vqs);
    close(s->vhostfd);
    for (i = 0; i < conf->num_queues; i++) {
        virtio_del_queue(vdev, i);
    }
    virtio_cleanup(vdev);
    return;
}

static void vhost_blk_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostBlk *s = VHOST_BLK(dev);

    vhost_blk_set_status(vdev, 0);
    vhost_dev_cleanup(&s->dev);
    g_free(s->dev.vqs);
    virtio_cleanup(vdev);
}

static const int user_feature_bits[] = {
    VIRTIO_BLK_F_FLUSH,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VHOST_INVALID_FEATURE_BIT
};


static uint64_t vhost_blk_get_features(VirtIODevice *vdev,
                                            uint64_t features,
                                            Error **errp)
{
    VHostBlk *s = VHOST_BLK(vdev);
    uint64_t res;

    features |= s->host_features;

    virtio_add_feature(&features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_add_feature(&features, VIRTIO_BLK_F_SEG_MAX);
    virtio_add_feature(&features, VIRTIO_BLK_F_GEOMETRY);
    virtio_add_feature(&features, VIRTIO_BLK_F_TOPOLOGY);
    virtio_add_feature(&features, VIRTIO_BLK_F_SIZE_MAX);

    virtio_add_feature(&features, VIRTIO_F_VERSION_1);

    if (!blk_is_writable(s->conf.conf.blk)) {
        virtio_add_feature(&features, VIRTIO_BLK_F_RO);
    }

    if (s->conf.num_queues > 1) {
        virtio_add_feature(&features, VIRTIO_BLK_F_MQ);
    }

    res = vhost_get_features(&s->dev, user_feature_bits, features);

    return res;
}

static void vhost_blk_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostBlk *s = VHOST_BLK(vdev);
    BlockConf *conf = &s->conf.conf;
    struct virtio_blk_config blkcfg;
    uint64_t capacity;
    int64_t length;
    int blk_size = conf->logical_block_size;

    blk_get_geometry(s->conf.conf.blk, &capacity);
    memset(&blkcfg, 0, sizeof(blkcfg));
    virtio_stq_p(vdev, &blkcfg.capacity, capacity);
    virtio_stl_p(vdev, &blkcfg.seg_max, s->conf.queue_size - 2);
    virtio_stw_p(vdev, &blkcfg.geometry.cylinders, conf->cyls);
    virtio_stl_p(vdev, &blkcfg.blk_size, blk_size);
    blkcfg.geometry.heads = conf->heads;

    length = blk_getlength(s->conf.conf.blk);
    if (length > 0 && length / conf->heads / conf->secs % blk_size) {
        unsigned short mask;

        mask = (s->conf.conf.logical_block_size / BDRV_SECTOR_SIZE) - 1;
        blkcfg.geometry.sectors = conf->secs & ~mask;
    } else {
        blkcfg.geometry.sectors = conf->secs;
    }

    blkcfg.size_max = 0;
    blkcfg.physical_block_exp = get_physical_block_exp(conf);
    blkcfg.alignment_offset = 0;
    virtio_stw_p(vdev, &blkcfg.num_queues, s->conf.num_queues);

    memcpy(config, &blkcfg, sizeof(struct virtio_blk_config));
}

static Property vhost_blk_properties[] = {
    DEFINE_BLOCK_PROPERTIES(VHostBlk, conf.conf),
    DEFINE_PROP_UINT16("num-queues", VHostBlk, conf.num_queues,
                       VHOST_BLK_AUTO_NUM_QUEUES),
    DEFINE_PROP_UINT16("queue-size", VHostBlk, conf.queue_size, 256),
/* Discard and write-zeroes not yet implemented in kernel module */
    DEFINE_PROP_BIT64("discard", VHostBlk, host_features,
                      VIRTIO_BLK_F_DISCARD, false),
    DEFINE_PROP_BIT64("write-zeroes", VHostBlk, host_features,
                      VIRTIO_BLK_F_WRITE_ZEROES, false),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_vhost_blk = {
    .name = "vhost-blk",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void vhost_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_blk_properties);
    dc->vmsd = &vmstate_vhost_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_blk_device_realize;
    vdc->unrealize = vhost_blk_device_unrealize;
    vdc->get_config = vhost_blk_update_config;
    vdc->get_features = vhost_blk_get_features;
    vdc->set_status = vhost_blk_set_status;
    vdc->reset = vhost_blk_reset;
}

static void vhost_blk_instance_init(Object *obj)
{
    VHostBlk *s = VHOST_BLK(obj);

    device_add_bootindex_property(obj, &s->conf.conf.bootindex,
                                  "bootindex", "/disk@0,0",
                                  DEVICE(obj));
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
