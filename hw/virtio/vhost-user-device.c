/*
 * Generic vhost-user stub. This can be used to connect to any
 * vhost-user backend. All configuration details must be handled by
 * the vhost-user daemon itself
 *
 * Copyright (c) 2023 Linaro Ltd
 * Author: Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-device.h"
#include "qemu/error-report.h"

static void vud_start(VirtIODevice *vdev)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VHostUserDevice *vud = VHOST_USER_DEVICE(vdev);
    int ret, i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&vud->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, vud->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    vud->vhost_dev.acked_features = vdev->guest_features;

    ret = vhost_dev_start(&vud->vhost_dev, vdev, true);
    if (ret < 0) {
        error_report("Error starting vhost-user-device: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < vud->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&vud->vhost_dev, vdev, i, false);
    }

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, vud->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&vud->vhost_dev, vdev);
}

static void vud_stop(VirtIODevice *vdev)
{
    VHostUserDevice *vud = VHOST_USER_DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&vud->vhost_dev, vdev, true);

    ret = k->set_guest_notifiers(qbus->parent, vud->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&vud->vhost_dev, vdev);
}

static void vud_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserDevice *vud = VHOST_USER_DEVICE(vdev);
    bool should_start = virtio_device_should_start(vdev, status);

    if (vhost_dev_is_started(&vud->vhost_dev) == should_start) {
        return;
    }

    if (should_start) {
        vud_start(vdev);
    } else {
        vud_stop(vdev);
    }
}

/*
 * For an implementation where everything is delegated to the backend
 * we don't do anything other than return the full feature set offered
 * by the daemon (module the reserved feature bit).
 */
static uint64_t vud_get_features(VirtIODevice *vdev,
                                 uint64_t requested_features, Error **errp)
{
    VHostUserDevice *vud = VHOST_USER_DEVICE(vdev);
    /* This should be set when the vhost connection initialises */
    g_assert(vud->vhost_dev.features);
    return vud->vhost_dev.features & ~(1ULL << VHOST_USER_F_PROTOCOL_FEATURES);
}

static void vud_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
}

static void do_vhost_user_cleanup(VirtIODevice *vdev, VHostUserDevice *vud)
{
    vhost_user_cleanup(&vud->vhost_user);

    for (int i = 0; i < vud->num_vqs; i++) {
        VirtQueue *vq = g_ptr_array_index(vud->vqs, i);
        virtio_delete_queue(vq);
    }

    virtio_cleanup(vdev);
}

static int vud_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserDevice *vud = VHOST_USER_DEVICE(vdev);

    if (vud->connected) {
        return 0;
    }
    vud->connected = true;

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        vud_start(vdev);
    }

    return 0;
}

static void vud_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserDevice *vud = VHOST_USER_DEVICE(vdev);

    if (!vud->connected) {
        return;
    }
    vud->connected = false;

    if (vhost_dev_is_started(&vud->vhost_dev)) {
        vud_stop(vdev);
    }
}

static void vud_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserDevice *vud = VHOST_USER_DEVICE(vdev);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vud_connect(dev) < 0) {
            qemu_chr_fe_disconnect(&vud->chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        vud_disconnect(dev);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void vud_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserDevice *vud = VHOST_USER_DEVICE(dev);
    int ret;

    if (!vud->chardev.chr) {
        error_setg(errp, "vhost-user-device: missing chardev");
        return;
    }

    if (!vud->virtio_id) {
        error_setg(errp, "vhost-user-device: need to define device id");
        return;
    }

    if (!vud->num_vqs) {
        vud->num_vqs = 1; /* reasonable default? */
    }

    if (!vhost_user_init(&vud->vhost_user, &vud->chardev, errp)) {
        return;
    }

    virtio_init(vdev, vud->virtio_id, 0);

    /*
     * Disable guest notifiers, by default all notifications will be via the
     * asynchronous vhost-user socket.
     */
    vdev->use_guest_notifier_mask = false;

    /* Allocate queues */
    vud->vqs = g_ptr_array_sized_new(vud->num_vqs);
    for (int i = 0; i < vud->num_vqs; i++) {
        g_ptr_array_add(vud->vqs,
                        virtio_add_queue(vdev, 4, vud_handle_output));
    }

    vud->vhost_dev.nvqs = vud->num_vqs;
    vud->vhost_dev.vqs = g_new0(struct vhost_virtqueue, vud->vhost_dev.nvqs);

    /* connect to backend */
    fprintf(stderr, "%s: doing vhost_dev_init()\n", __func__);
    ret = vhost_dev_init(&vud->vhost_dev, &vud->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0, errp);

    if (ret < 0) {
        do_vhost_user_cleanup(vdev, vud);
    }

    qemu_chr_fe_set_handlers(&vud->chardev, NULL, NULL, vud_event, NULL,
                             dev, NULL, true);
}

static void vud_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserDevice *vud = VHOST_USER_DEVICE(dev);
    struct vhost_virtqueue *vhost_vqs = vud->vhost_dev.vqs;

    /* This will stop vhost backend if appropriate. */
    vud_set_status(vdev, 0);
    vhost_dev_cleanup(&vud->vhost_dev);
    g_free(vhost_vqs);
    do_vhost_user_cleanup(vdev, vud);
}

static const VMStateDescription vud_vmstate = {
    .name = "vhost-user-device",
    .unmigratable = 1,
};

static Property vud_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserDevice, chardev),
    DEFINE_PROP_UINT16("virtio-id", VHostUserDevice, virtio_id, 0),
    DEFINE_PROP_UINT32("num_vqs", VHostUserDevice, num_vqs, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void vud_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vud_properties);
    dc->vmsd = &vud_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    vdc->realize = vud_device_realize;
    vdc->unrealize = vud_device_unrealize;
    vdc->get_features = vud_get_features;
    vdc->set_status = vud_set_status;
}

static const TypeInfo vud_info = {
    .name = TYPE_VHOST_USER_DEVICE,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserDevice),
    .class_init = vud_class_init,
};

static void vud_register_types(void)
{
    type_register_static(&vud_info);
}

type_init(vud_register_types)
