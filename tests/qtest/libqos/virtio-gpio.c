/*
 * virtio-gpio nodes for testing
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "qgraph.h"
#include "virtio-gpio.h"

static void *qvirtio_gpio_get_driver(QVhostUserGPIO *v_gpio,
                                       const char *interface)
{
    if (!g_strcmp0(interface, "vhost-user-gpio")) {
        return v_gpio;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_gpio->vdev;
    }

    fprintf(stderr, "%s not present in virtio-gpio-device\n", interface);
    g_assert_not_reached();
}

static void *qvirtio_gpio_device_get_driver(void *object,
                                              const char *interface)
{
    QVhostUserGPIODevice *v_gpio = object;
    return qvirtio_gpio_get_driver(&v_gpio->gpio, interface);
}

static void *virtio_gpio_device_create(void *virtio_dev,
                                         QGuestAllocator *t_alloc,
                                         void *addr)
{
    QVhostUserGPIODevice *virtio_device = g_new0(QVhostUserGPIODevice, 1);
    QVhostUserGPIO *interface = &virtio_device->gpio;

    interface->vdev = virtio_dev;

    virtio_device->obj.get_driver = qvirtio_gpio_device_get_driver;

    return &virtio_device->obj;
}

/* virtio-gpio-pci */
static void *qvirtio_gpio_pci_get_driver(void *object, const char *interface)
{
    QVhostUserGPIOPCI *v_gpio = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_gpio->pci_vdev.pdev;
    }
    return qvirtio_gpio_get_driver(&v_gpio->gpio, interface);
}

static void *virtio_gpio_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                      void *addr)
{
    QVhostUserGPIOPCI *virtio_spci = g_new0(QVhostUserGPIOPCI, 1);
    QVhostUserGPIO *interface = &virtio_spci->gpio;
    QOSGraphObject *obj = &virtio_spci->pci_vdev.obj;

    virtio_pci_init(&virtio_spci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_spci->pci_vdev.vdev;

    obj->get_driver = qvirtio_gpio_pci_get_driver;

    return obj;
}

static void virtio_gpio_register_nodes(void)
{
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions edge_opts = { };

    /* vhost-user-gpio-device */
    edge_opts.extra_device_opts = "id=gpio0,chardev=vhgpio0";
    qos_node_create_driver("vhost-user-gpio-device",
                            virtio_gpio_device_create);
    qos_node_consumes("vhost-user-gpio-device", "virtio-bus", &edge_opts);
    qos_node_produces("vhost-user-gpio-device", "vhost-user-gpio");

    /* virtio-gpio-pci */
    edge_opts.extra_device_opts = "id=gpio0,addr=04.0,chardev=vhgpio0";
    add_qpci_address(&edge_opts, &addr);
    qos_node_create_driver("vhost-user-gpio-pci", virtio_gpio_pci_create);
    qos_node_consumes("vhost-user-gpio-pci", "pci-bus", &edge_opts);
    qos_node_produces("vhost-user-gpio-pci", "vhost-user-gpio");
}

libqos_init(virtio_gpio_register_nodes);
