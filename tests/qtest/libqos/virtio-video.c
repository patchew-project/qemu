/*
 * libqos virtio-video driver
 *
 * Copyright (c) 2023 Red Hat Inc.
 * 
 * Authors:
 *  Albert Esteve <aesteve@redhat.com>
 *   (based on virtio-gpio.c)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_config.h"
#include "../libqtest.h"
#include "qemu/module.h"
#include "qgraph.h"
#include "virtio-video.h"

#define NUM_VQUEUES 2
#define PCI_SLOT    0x04
#define PCI_FN      0x00

static QGuestAllocator *alloc;

static void virtio_video_cleanup(QVhostUserVideo *video)
{
    QVirtioDevice *vdev = video->vdev;
    int i;

    for (i = 0; i < NUM_VQUEUES; i++) {
        qvirtqueue_cleanup(vdev->bus, video->queues[i], alloc);
    }
    g_free(video->queues);
}

/*
 * This handles the VirtIO setup from the point of view of the driver
 * frontend and therefore doesn't present any vhost specific features
 * and in fact masks off the re-used bit.
 */
static void virtio_video_setup(QVhostUserVideo *video)
{
    QVirtioDevice *vdev = video->vdev;
    uint64_t features;
    int i;

    features = qvirtio_get_features(vdev);
    features &= ~QVIRTIO_F_BAD_FEATURE;
    qvirtio_set_features(vdev, features);

    video->queues = g_new(QVirtQueue *, NUM_VQUEUES);
    for (i = 0; i < NUM_VQUEUES; i++) {
        video->queues[i] = qvirtqueue_setup(vdev, alloc, i);
    }
    qvirtio_set_driver_ok(vdev);
}

static void *qvirtio_video_get_driver(QVhostUserVideo *v_video,
                                      const char *interface)
{
    if (!g_strcmp0(interface, "vhost-user-video")) {
        return v_video;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_video->vdev;
    }

    g_assert_not_reached();
}

static void *qvirtio_video_device_get_driver(void *object,
                                             const char *interface)
{
    QVhostUserVideoDevice *v_video = object;
    return qvirtio_video_get_driver(&v_video->video, interface);
}

/* virtio-video (mmio) */
static void qvirtio_video_device_destructor(QOSGraphObject *obj)
{
    QVhostUserVideoDevice *video_dev = (QVhostUserVideoDevice *) obj;
    virtio_video_cleanup(&video_dev->video);
}

static void qvirtio_video_device_start_hw(QOSGraphObject *obj)
{
    QVhostUserVideoDevice *video_dev = (QVhostUserVideoDevice *) obj;
    virtio_video_setup(&video_dev->video);
}

static void *virtio_video_device_create(void *virtio_dev,
                                       QGuestAllocator *t_alloc,
                                       void *addr)
{
    QVhostUserVideoDevice *virtio_device = g_new0(QVhostUserVideoDevice, 1);
    QVhostUserVideo *interface = &virtio_device->video;

    interface->vdev = virtio_dev;
    alloc = t_alloc;

    virtio_device->obj.get_driver = qvirtio_video_device_get_driver;
    virtio_device->obj.start_hw = qvirtio_video_device_start_hw;
    virtio_device->obj.destructor = qvirtio_video_device_destructor;

    return &virtio_device->obj;
}

/* virtio-video-pci */
static void qvirtio_video_pci_destructor(QOSGraphObject *obj)
{
    QVhostUserVideoPCI *video_pci = (QVhostUserVideoPCI *) obj;
    QOSGraphObject *pci_vobj = &video_pci->pci_vdev.obj;

    virtio_video_cleanup(&video_pci->video);
    qvirtio_pci_destructor(pci_vobj);
}

static void qvirtio_video_pci_start_hw(QOSGraphObject *obj)
{
    QVhostUserVideoPCI *video_pci = (QVhostUserVideoPCI *) obj;
    QOSGraphObject *pci_vobj = &video_pci->pci_vdev.obj;

    qvirtio_pci_start_hw(pci_vobj);
    virtio_video_setup(&video_pci->video);
}

static void *qvirtio_video_pci_get_driver(void *object, const char *interface)
{
    QVhostUserVideoPCI *v_video = object;

    if (!g_strcmp0(interface, "pci-device")) {
        return v_video->pci_vdev.pdev;
    }
    return qvirtio_video_get_driver(&v_video->video, interface);
}

static void *virtio_video_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                    void *addr)
{
    QVhostUserVideoPCI *virtio_spci = g_new0(QVhostUserVideoPCI, 1);
    QVhostUserVideo *interface = &virtio_spci->video;
    QOSGraphObject *obj = &virtio_spci->pci_vdev.obj;

    virtio_pci_init(&virtio_spci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_spci->pci_vdev.vdev;
    alloc = t_alloc;

    obj->get_driver = qvirtio_video_pci_get_driver;
    obj->start_hw = qvirtio_video_pci_start_hw;
    obj->destructor = qvirtio_video_pci_destructor;

    return obj;
}

static void virtio_video_register_nodes(void)
{
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(PCI_SLOT, PCI_FN),
    };

    QOSGraphEdgeOptions edge_opts = {
        .extra_device_opts = "id=video0,chardev=chr-vhost-user-test",
    };

    /* vhost-user-video-device */
    qos_node_create_driver("vhost-user-video-device",
                            virtio_video_device_create);
    qos_node_consumes("vhost-user-video-device", "virtio-bus", &edge_opts);
    qos_node_produces("vhost-user-video-device", "vhost-user-video");

    /* vhost-user-video-pci */
    add_qpci_address(&edge_opts, &addr);
    qos_node_create_driver("vhost-user-video-pci", virtio_video_pci_create);
    qos_node_consumes("vhost-user-video-pci", "pci-bus", &edge_opts);
    qos_node_produces("vhost-user-video-pci", "vhost-user-video");
}

libqos_init(virtio_video_register_nodes);
