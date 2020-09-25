/*
 * Vhost-user RPMB virtio device
 *
 * This is the boilerplate for instantiating a vhost-user device
 * implementing a Replay Protected Memory Block (RPMB) device. This is
 * a type of flash chip that is protected from replay attacks and used
 * for tamper resistant storage. The actual back-end for this driver
 * is the vhost-user-rpmb daemon. The code here just connects up the
 * device in QEMU and allows it to be instantiated.
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-rpmb.h"
#include "qemu/error-report.h"

/* currently there is no RPMB driver in Linux */
#define VIRTIO_ID_RPMB         28 /* virtio RPMB */

static void vurpmb_get_config(VirtIODevice *vdev, uint8_t *config)
{
    /* this somehow needs to come from the vhost-user daemon */
}

static void vurpmb_start(VirtIODevice *vdev)
{
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;
    int i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&rpmb->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, rpmb->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    rpmb->vhost_dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&rpmb->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost-user-rpmb: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here.  virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < rpmb->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&rpmb->vhost_dev, vdev, i, false);
    }

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, rpmb->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&rpmb->vhost_dev, vdev);
}

static void vurpmb_stop(VirtIODevice *vdev)
{
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&rpmb->vhost_dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, rpmb->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&rpmb->vhost_dev, vdev);
}

static void vurpmb_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (rpmb->vhost_dev.started == should_start) {
        return;
    }

    if (should_start) {
        vurpmb_start(vdev);
    } else {
        vurpmb_stop(vdev);
    }
}

static uint64_t vurpmb_get_features(VirtIODevice *vdev,
                                      uint64_t requested_features,
                                      Error **errp)
{
    /* No feature bits used yet */
    return requested_features;
}

static void vurpmb_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
}

static void vurpmb_guest_notifier_mask(VirtIODevice *vdev, int idx,
                                            bool mask)
{
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(vdev);
    vhost_virtqueue_mask(&rpmb->vhost_dev, vdev, idx, mask);
}

static bool vurpmb_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(vdev);
    return vhost_virtqueue_pending(&rpmb->vhost_dev, idx);
}

/*
 * Chardev connect/disconnect events
 */

static int vurpmb_handle_config_change(struct vhost_dev *dev)
{
    int ret;
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(dev->vdev);

    ret = vhost_dev_get_config(dev, (uint8_t *)&rpmb->conf.config,
                               sizeof(struct virtio_rpmb_config));
    if (ret < 0) {
        error_report("get config space failed");
        return -1;
    }

    return 0;
}

const VhostDevConfigOps rpmb_ops = {
    .vhost_dev_config_notifier = vurpmb_handle_config_change,
};

static int vurpmb_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(vdev);

    if (rpmb->connected) {
        return 0;
    }
    rpmb->connected = true;

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        vurpmb_start(vdev);
    }

    return 0;
}

static void vurpmb_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(vdev);

    if (!rpmb->connected) {
        return;
    }
    rpmb->connected = false;

    if (rpmb->vhost_dev.started) {
        vurpmb_stop(vdev);
    }

    vhost_dev_cleanup(&rpmb->vhost_dev);
}

static void vurpmb_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(vdev);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vurpmb_connect(dev) < 0) {
            qemu_chr_fe_disconnect(&rpmb->conf.chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        vurpmb_disconnect(dev);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void do_vhost_user_cleanup(VirtIODevice *vdev, VHostUserRPMB *rpmb)
{
    vhost_user_cleanup(&rpmb->vhost_user);
    virtio_delete_queue(rpmb->req_vq);
    virtio_cleanup(vdev);
    g_free(rpmb->vhost_dev.vqs);
    rpmb->vhost_dev.vqs = NULL;
}


static void vurpmb_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(dev);
    int ret;

    if (!rpmb->conf.chardev.chr) {
        error_setg(errp, "missing chardev");
        return;
    }

    if (!vhost_user_init(&rpmb->vhost_user, &rpmb->conf.chardev, errp)) {
        return;
    }

    virtio_init(vdev, "vhost-user-rpmb", VIRTIO_ID_RPMB,
                sizeof(struct virtio_rpmb_config));

    /* One request queue, 4 elements in case we don't do indirect */
    rpmb->req_vq = virtio_add_queue(vdev, 4, vurpmb_handle_output);
    rpmb->vhost_dev.nvqs = 1;
    rpmb->vhost_dev.vqs = g_new0(struct vhost_virtqueue, rpmb->vhost_dev.nvqs);
    ret = vhost_dev_init(&rpmb->vhost_dev, &rpmb->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost_dev_init failed");
        do_vhost_user_cleanup(vdev, rpmb);
    }

    /*
     * At this point the next event we will get is a connection from
     * the daemon on the control socket.
     */

    qemu_chr_fe_set_handlers(&rpmb->conf.chardev,  NULL, NULL, vurpmb_event,
                             NULL, (void *)dev, NULL, true);

    return;
}

static void vurpmb_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRPMB *rpmb = VHOST_USER_RPMB(dev);

    /* This will stop vhost backend if appropriate. */
    vurpmb_set_status(vdev, 0);

    do_vhost_user_cleanup(vdev, rpmb);
}

static const VMStateDescription vurpmb_vmstate = {
    .name = "vhost-user-rpmb",
    .unmigratable = 1,
};

static Property vurpmb_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserRPMB, conf.chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vurpmb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vurpmb_properties);
    dc->vmsd = &vurpmb_vmstate;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vurpmb_device_realize;
    vdc->unrealize = vurpmb_device_unrealize;
    vdc->get_features = vurpmb_get_features;
    vdc->get_config = vurpmb_get_config;
    vdc->set_status = vurpmb_set_status;
    vdc->guest_notifier_mask = vurpmb_guest_notifier_mask;
    vdc->guest_notifier_pending = vurpmb_guest_notifier_pending;
}

static const TypeInfo vurpmb_info = {
    .name = TYPE_VHOST_USER_RPMB,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserRPMB),
    .class_init = vurpmb_class_init,
};

static void vurpmb_register_types(void)
{
    type_register_static(&vurpmb_info);
}

type_init(vurpmb_register_types)
