/*
 * vhost-user-blk host device
 *
 * Copyright(C) 2017 Intel Corporation.
 *
 * Authors:
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * Largely based on the "vhost-user-scsi.c" and "vhost-scsi.c" implemented by:
 * Felipe Franciosi <felipe@nutanix.com>
 * Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
 * Nicholas Bellinger <nab@risingtidesystems.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
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
#include "hw/virtio/vhost-user-blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"

static const int user_feature_bits[] = {
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

static void vhost_user_blk_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);
    bool should_start = virtio_device_started(vdev, status);
    int ret;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (!s->connected) {
        return;
    }

    if (vbc->dev.started == should_start) {
        return;
    }

    if (should_start) {
        ret = vhost_blk_common_start(vbc);
        if (ret < 0) {
            error_report("vhost-user-blk: vhost start failed: %s",
                         strerror(-ret));
            qemu_chr_fe_disconnect(&s->chardev);
        }
    } else {
        vhost_blk_common_stop(vbc);
    }

}

static void vhost_user_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);
    int i, ret;

    if (!vdev->start_on_kick) {
        return;
    }

    if (!s->connected) {
        return;
    }

    if (vbc->dev.started) {
        return;
    }

    /* Some guests kick before setting VIRTIO_CONFIG_S_DRIVER_OK so start
     * vhost here instead of waiting for .set_status().
     */
    ret = vhost_blk_common_start(vbc);
    if (ret < 0) {
        error_report("vhost-user-blk: vhost start failed: %s",
                     strerror(-ret));
        qemu_chr_fe_disconnect(&s->chardev);
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

static void vhost_user_blk_reset(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);

    vhost_dev_free_inflight(vbc->inflight);
}

static int vhost_user_blk_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);
    int ret = 0;

    if (s->connected) {
        return 0;
    }
    s->connected = true;

    vbc->dev.nvqs = vbc->num_queues;
    vbc->dev.vqs = vbc->vhost_vqs;
    vbc->dev.vq_index = 0;
    vbc->dev.backend_features = 0;

    vhost_dev_set_config_notifier(&vbc->dev, &blk_ops);

    ret = vhost_dev_init(&vbc->dev, &s->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0);
    if (ret < 0) {
        error_report("vhost-user-blk: vhost initialization failed: %s",
                     strerror(-ret));
        return ret;
    }

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        ret = vhost_blk_common_start(vbc);
        if (ret < 0) {
            error_report("vhost-user-blk: vhost start failed: %s",
                         strerror(-ret));
            return ret;
        }
    }

    return 0;
}

static void vhost_user_blk_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);

    if (!s->connected) {
        return;
    }
    s->connected = false;

    vhost_blk_common_stop(vbc);

    vhost_dev_cleanup(&vbc->dev);
}

static void vhost_user_blk_event(void *opaque, QEMUChrEvent event,
                                 bool realized);

static void vhost_user_blk_event_realize(void *opaque, QEMUChrEvent event)
{
    vhost_user_blk_event(opaque, event, false);
}

static void vhost_user_blk_event_oper(void *opaque, QEMUChrEvent event)
{
    vhost_user_blk_event(opaque, event, true);
}

static void vhost_user_blk_chr_closed_bh(void *opaque)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);

    vhost_user_blk_disconnect(dev);
    qemu_chr_fe_set_handlers(&s->chardev, NULL, NULL,
            vhost_user_blk_event_oper, NULL, opaque, NULL, true);
}

static void vhost_user_blk_event(void *opaque, QEMUChrEvent event,
                                 bool realized)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vhost_user_blk_connect(dev) < 0) {
            qemu_chr_fe_disconnect(&s->chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        /*
         * Closing the connection should happen differently on device
         * initialization and operation stages.
         * On initalization, we want to re-start vhost_dev initialization
         * from the very beginning right away when the connection is closed,
         * so we clean up vhost_dev on each connection closing.
         * On operation, we want to postpone vhost_dev cleanup to let the
         * other code perform its own cleanup sequence using vhost_dev data
         * (e.g. vhost_dev_set_log).
         */
        if (realized && !runstate_check(RUN_STATE_SHUTDOWN)) {
            /*
             * A close event may happen during a read/write, but vhost
             * code assumes the vhost_dev remains setup, so delay the
             * stop & clear.
             */
            AioContext *ctx = qemu_get_current_aio_context();

            qemu_chr_fe_set_handlers(&s->chardev, NULL, NULL, NULL, NULL,
                    NULL, NULL, false);
            aio_bh_schedule_oneshot(ctx, vhost_user_blk_chr_closed_bh, opaque);

            /*
             * Move vhost device to the stopped state. The vhost-user device
             * will be clean up and disconnected in BH. This can be useful in
             * the vhost migration code. If disconnect was caught there is an
             * option for the general vhost code to get the dev state without
             * knowing its type (in this case vhost-user).
             */
            vbc->dev.started = false;
        } else {
            vhost_user_blk_disconnect(dev);
        }
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void vhost_user_blk_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);
    Error *err = NULL;
    int ret;

    if (!s->chardev.chr) {
        error_setg(errp, "vhost-user-blk: chardev is mandatory");
        return;
    }

    if (!vhost_user_init(&s->vhost_user, &s->chardev, errp)) {
        return;
    }

    vhost_blk_common_realize(vbc, vhost_user_blk_handle_output, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        goto blk_err;
    }
    s->connected = false;

    qemu_chr_fe_set_handlers(&s->chardev,  NULL, NULL,
                             vhost_user_blk_event_realize, NULL, (void *)dev,
                             NULL, true);

reconnect:
    if (qemu_chr_fe_wait_connected(&s->chardev, &err) < 0) {
        error_report_err(err);
        goto connect_err;
    }

    /* check whether vhost_user_blk_connect() failed or not */
    if (!s->connected) {
        goto reconnect;
    }

    ret = vhost_dev_get_config(&vbc->dev, (uint8_t *)&vbc->blkcfg,
                               sizeof(struct virtio_blk_config));
    if (ret < 0) {
        error_report("vhost-user-blk: get block config failed");
        goto reconnect;
    }

    /* we're fully initialized, now we can operate, so change the handler */
    qemu_chr_fe_set_handlers(&s->chardev,  NULL, NULL,
                             vhost_user_blk_event_oper, NULL, (void *)dev,
                             NULL, true);
    return;

connect_err:
    vhost_blk_common_unrealize(vbc);
blk_err:
    vhost_user_cleanup(&s->vhost_user);
}

static void vhost_user_blk_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(dev);
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(s);

    virtio_set_status(vdev, 0);
    qemu_chr_fe_set_handlers(&s->chardev,  NULL, NULL, NULL,
                             NULL, NULL, NULL, false);
    vhost_dev_cleanup(&vbc->dev);
    vhost_dev_free_inflight(vbc->inflight);
    vhost_blk_common_unrealize(vbc);
    vhost_user_cleanup(&s->vhost_user);
}

static void vhost_user_blk_instance_init(Object *obj)
{
    VHostBlkCommon *vbc = VHOST_BLK_COMMON(obj);

    vbc->feature_bits = user_feature_bits;

    device_add_bootindex_property(obj, &vbc->bootindex, "bootindex",
                                  "/disk@0,0", DEVICE(obj));
}

static const VMStateDescription vmstate_vhost_user_blk = {
    .name = "vhost-user-blk",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property vhost_user_blk_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),
    DEFINE_PROP_UINT16("num-queues", VHostBlkCommon, num_queues,
                       VHOST_BLK_AUTO_NUM_QUEUES),
    DEFINE_PROP_UINT32("queue-size", VHostBlkCommon, queue_size, 128),
    DEFINE_PROP_BIT("config-wce", VHostBlkCommon, config_wce, 0, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_user_blk_properties);
    dc->vmsd = &vmstate_vhost_user_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_user_blk_device_realize;
    vdc->unrealize = vhost_user_blk_device_unrealize;
    vdc->set_status = vhost_user_blk_set_status;
    vdc->reset = vhost_user_blk_reset;
}

static const TypeInfo vhost_user_blk_info = {
    .name = TYPE_VHOST_USER_BLK,
    .parent = TYPE_VHOST_BLK_COMMON,
    .instance_size = sizeof(VHostUserBlk),
    .instance_init = vhost_user_blk_instance_init,
    .class_init = vhost_user_blk_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_user_blk_info);
}

type_init(virtio_register_types)
