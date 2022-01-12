/*
 * Vhost-user GPIO virtio device
 *
 * Copyright (c) 2022 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-gpio.h"
#include "qemu/error-report.h"
#include "standard-headers/linux/virtio_ids.h"

static const int feature_bits[] = {
    VIRTIO_GPIO_F_IRQ
};

static void vu_gpio_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);

    memcpy(config, &gpio->config, sizeof(gpio->config));
}

static int vu_gpio_config_notifier(struct vhost_dev *dev)
{
    VHostUserGPIO *gpio = VHOST_USER_GPIO(dev->vdev);

    memcpy(dev->vdev->config, &gpio->config, sizeof(gpio->config));
    virtio_notify_config(dev->vdev);

    return 0;
}

const VhostDevConfigOps gpio_ops = {
    .vhost_dev_config_notifier = vu_gpio_config_notifier,
};

static int vu_gpio_start(VirtIODevice *vdev)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);
    int ret, i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&gpio->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", ret);
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, gpio->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", ret);
        goto err_host_notifiers;
    }

    gpio->vhost_dev.acked_features = vdev->guest_features;

    ret = vhost_dev_start(&gpio->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost-user-gpio: %d", ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < gpio->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&gpio->vhost_dev, vdev, i, false);
    }

    return 0;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, gpio->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&gpio->vhost_dev, vdev);

    return ret;
}

static void vu_gpio_stop(VirtIODevice *vdev)
{
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&gpio->vhost_dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, gpio->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&gpio->vhost_dev, vdev);
}

static void vu_gpio_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (!gpio->connected) {
        return;
    }

    if (gpio->vhost_dev.started == should_start) {
        return;
    }

    if (should_start) {
        if (vu_gpio_start(vdev)) {
            qemu_chr_fe_disconnect(&gpio->chardev);
        }
    } else {
        vu_gpio_stop(vdev);
    }
}

static uint64_t vu_gpio_get_features(VirtIODevice *vdev,
                                    uint64_t requested_features, Error **errp)
{
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);

    virtio_add_feature(&requested_features, VIRTIO_GPIO_F_IRQ);
    return vhost_get_features(&gpio->vhost_dev, feature_bits, requested_features);
}

static void vu_gpio_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
}

static void vu_gpio_guest_notifier_mask(VirtIODevice *vdev, int idx, bool mask)
{
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);

    vhost_virtqueue_mask(&gpio->vhost_dev, vdev, idx, mask);
}

static void do_vhost_user_cleanup(VirtIODevice *vdev, VHostUserGPIO *gpio)
{
    virtio_delete_queue(gpio->command_vq);
    virtio_delete_queue(gpio->interrupt_vq);
    g_free(gpio->vhost_dev.vqs);
    gpio->vhost_dev.vqs = NULL;
    virtio_cleanup(vdev);
    vhost_user_cleanup(&gpio->vhost_user);
}

static int vu_gpio_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);
    Error *local_err = NULL;
    int ret;

    if (gpio->connected) {
        return 0;
    }
    gpio->connected = true;

    vhost_dev_set_config_notifier(&gpio->vhost_dev, &gpio_ops);

    ret = vhost_dev_init(&gpio->vhost_dev, &gpio->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0, &local_err);
    if (ret < 0) {
        error_report("vhost-user-gpio: vhost initialization failed: %s",
                     strerror(-ret));
        return ret;
    }

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        vu_gpio_start(vdev);
    }

    return 0;
}

static void vu_gpio_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);

    if (!gpio->connected) {
        return;
    }
    gpio->connected = false;

    vu_gpio_stop(vdev);
    vhost_dev_cleanup(&gpio->vhost_dev);
}

static void vu_gpio_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserGPIO *gpio = VHOST_USER_GPIO(vdev);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vu_gpio_connect(dev) < 0) {
            qemu_chr_fe_disconnect(&gpio->chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        vu_gpio_disconnect(dev);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void vu_gpio_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserGPIO *gpio = VHOST_USER_GPIO(dev);
    Error *err = NULL;
    int ret;

    if (!gpio->chardev.chr) {
        error_setg(errp, "vhost-user-gpio: chardev is mandatory");
        return;
    }

    if (!vhost_user_init(&gpio->vhost_user, &gpio->chardev, errp)) {
        return;
    }

    virtio_init(vdev, "vhost-user-gpio", VIRTIO_ID_GPIO, sizeof(gpio->config));

    gpio->vhost_dev.nvqs = 2;
    gpio->command_vq = virtio_add_queue(vdev, 256, vu_gpio_handle_output);
    gpio->interrupt_vq = virtio_add_queue(vdev, 256, vu_gpio_handle_output);
    gpio->vhost_dev.vqs = g_new0(struct vhost_virtqueue, gpio->vhost_dev.nvqs);

    gpio->connected = false;

    qemu_chr_fe_set_handlers(&gpio->chardev, NULL, NULL, vu_gpio_event, NULL,
                             dev, NULL, true);

reconnect:
    if (qemu_chr_fe_wait_connected(&gpio->chardev, &err) < 0) {
        error_report_err(err);
        do_vhost_user_cleanup(vdev, gpio);
        return;
    }

    /* check whether vhost_user_gpio_connect() failed or not */
    if (!gpio->connected) {
        goto reconnect;
    }

    ret = vhost_dev_get_config(&gpio->vhost_dev, (uint8_t *)&gpio->config,
                               sizeof(gpio->config), errp);
    if (ret < 0) {
        error_report("vhost-user-gpio: get config failed");
        goto reconnect;
    }

    return;
}

static void vu_gpio_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserGPIO *gpio = VHOST_USER_GPIO(dev);

    vu_gpio_set_status(vdev, 0);
    qemu_chr_fe_set_handlers(&gpio->chardev, NULL, NULL, NULL, NULL, NULL, NULL,
                             false);
    vhost_dev_cleanup(&gpio->vhost_dev);
    do_vhost_user_cleanup(vdev, gpio);
}

static const VMStateDescription vu_gpio_vmstate = {
    .name = "vhost-user-gpio",
    .unmigratable = 1,
};

static Property vu_gpio_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserGPIO, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vu_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vu_gpio_properties);
    dc->vmsd = &vu_gpio_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    vdc->realize = vu_gpio_device_realize;
    vdc->unrealize = vu_gpio_device_unrealize;
    vdc->get_features = vu_gpio_get_features;
    vdc->get_config = vu_gpio_get_config;
    vdc->set_status = vu_gpio_set_status;
    vdc->guest_notifier_mask = vu_gpio_guest_notifier_mask;
}

static const TypeInfo vu_gpio_info = {
    .name = TYPE_VHOST_USER_GPIO,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserGPIO),
    .class_init = vu_gpio_class_init,
};

static void vu_gpio_register_types(void)
{
    type_register_static(&vu_gpio_info);
}

type_init(vu_gpio_register_types)
