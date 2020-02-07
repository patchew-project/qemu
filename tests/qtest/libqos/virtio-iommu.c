/*
 * libqos driver virtio-iommu-pci framework
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-iommu.h"
#include "hw/virtio/virtio-iommu.h"

static QGuestAllocator *alloc;

/* virtio-iommu-device */
static void *qvirtio_iommu_get_driver(QVirtioIOMMU *v_iommu,
                                      const char *interface)
{
    if (!g_strcmp0(interface, "virtio-iommu")) {
        return v_iommu;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_iommu->vdev;
    }

    fprintf(stderr, "%s not present in virtio-iommu-device\n", interface);
    g_assert_not_reached();
}

static void *qvirtio_iommu_device_get_driver(void *object,
                                             const char *interface)
{
    QVirtioIOMMUDevice *v_iommu = object;
    return qvirtio_iommu_get_driver(&v_iommu->iommu, interface);
}

static void virtio_iommu_cleanup(QVirtioIOMMU *interface)
{
    qvirtqueue_cleanup(interface->vdev->bus, interface->vq, alloc);
}

static void virtio_iommu_setup(QVirtioIOMMU *interface)
{
    QVirtioDevice *vdev = interface->vdev;
    uint64_t features;

    features = qvirtio_get_features(vdev);
    features &= ~(QVIRTIO_F_BAD_FEATURE |
                  (1ull << VIRTIO_RING_F_INDIRECT_DESC) |
                  (1ull << VIRTIO_RING_F_EVENT_IDX) |
                  (1ull << VIRTIO_IOMMU_F_BYPASS));
    qvirtio_set_features(vdev, features);
    interface->vq = qvirtqueue_setup(interface->vdev, alloc, 0);
    qvirtio_set_driver_ok(interface->vdev);
}

static void qvirtio_iommu_device_destructor(QOSGraphObject *obj)
{
    QVirtioIOMMUDevice *v_iommu = (QVirtioIOMMUDevice *) obj;
    QVirtioIOMMU *iommu = &v_iommu->iommu;

    virtio_iommu_cleanup(iommu);
}

static void qvirtio_iommu_device_start_hw(QOSGraphObject *obj)
{
    QVirtioIOMMUDevice *v_iommu = (QVirtioIOMMUDevice *) obj;
    QVirtioIOMMU *iommu = &v_iommu->iommu;

    virtio_iommu_setup(iommu);
}

static void *virtio_iommu_device_create(void *virtio_dev,
                                        QGuestAllocator *t_alloc,
                                        void *addr)
{
    QVirtioIOMMUDevice *virtio_rdevice = g_new0(QVirtioIOMMUDevice, 1);
    QVirtioIOMMU *interface = &virtio_rdevice->iommu;

    interface->vdev = virtio_dev;
    alloc = t_alloc;

    virtio_rdevice->obj.get_driver = qvirtio_iommu_device_get_driver;
    virtio_rdevice->obj.destructor = qvirtio_iommu_device_destructor;
    virtio_rdevice->obj.start_hw = qvirtio_iommu_device_start_hw;

    return &virtio_rdevice->obj;
}

/* virtio-iommu-pci */
static void *qvirtio_iommu_pci_get_driver(void *object, const char *interface)
{
    QVirtioIOMMUPCI *v_iommu = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_iommu->pci_vdev.pdev;
    }
    return qvirtio_iommu_get_driver(&v_iommu->iommu, interface);
}

static void qvirtio_iommu_pci_destructor(QOSGraphObject *obj)
{
    QVirtioIOMMUPCI *iommu_pci = (QVirtioIOMMUPCI *) obj;
    QVirtioIOMMU *interface = &iommu_pci->iommu;
    QOSGraphObject *pci_vobj =  &iommu_pci->pci_vdev.obj;

    virtio_iommu_cleanup(interface);
    qvirtio_pci_destructor(pci_vobj);
}

static void qvirtio_iommu_pci_start_hw(QOSGraphObject *obj)
{
    QVirtioIOMMUPCI *iommu_pci = (QVirtioIOMMUPCI *) obj;
    QVirtioIOMMU *interface = &iommu_pci->iommu;
    QOSGraphObject *pci_vobj =  &iommu_pci->pci_vdev.obj;

    qvirtio_pci_start_hw(pci_vobj);
    virtio_iommu_setup(interface);
}


static void *virtio_iommu_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                   void *addr)
{
    QVirtioIOMMUPCI *virtio_rpci = g_new0(QVirtioIOMMUPCI, 1);
    QVirtioIOMMU *interface = &virtio_rpci->iommu;
    QOSGraphObject *obj = &virtio_rpci->pci_vdev.obj;

    virtio_pci_init(&virtio_rpci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_rpci->pci_vdev.vdev;
    alloc = t_alloc;

    obj->get_driver = qvirtio_iommu_pci_get_driver;
    obj->start_hw = qvirtio_iommu_pci_start_hw;
    obj->destructor = qvirtio_iommu_pci_destructor;

    return obj;
}

static void virtio_iommu_register_nodes(void)
{
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };

    /* virtio-iommu-device */
    qos_node_create_driver("virtio-iommu-device", virtio_iommu_device_create);
    qos_node_consumes("virtio-iommu-device", "virtio-bus", NULL);
    qos_node_produces("virtio-iommu-device", "virtio");
    qos_node_produces("virtio-iommu-device", "virtio-iommu");

    /* virtio-iommu-pci */
    add_qpci_address(&opts, &addr);
    qos_node_create_driver("virtio-iommu-pci", virtio_iommu_pci_create);
    qos_node_consumes("virtio-iommu-pci", "pci-bus", &opts);
    qos_node_produces("virtio-iommu-pci", "pci-device");
    qos_node_produces("virtio-iommu-pci", "virtio");
    qos_node_produces("virtio-iommu-pci", "virtio-iommu");
}

libqos_init(virtio_iommu_register_nodes);
