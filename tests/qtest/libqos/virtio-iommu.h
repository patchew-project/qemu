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

#ifndef TESTS_LIBQOS_VIRTIO_IOMMU_H
#define TESTS_LIBQOS_VIRTIO_IOMMU_H

#include "libqos/qgraph.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"

typedef struct QVirtioIOMMU QVirtioIOMMU;
typedef struct QVirtioIOMMUPCI QVirtioIOMMUPCI;
typedef struct QVirtioIOMMUDevice QVirtioIOMMUDevice;

struct QVirtioIOMMU {
    QVirtioDevice *vdev;
    QVirtQueue *vq;
};

struct QVirtioIOMMUPCI {
    QVirtioPCIDevice pci_vdev;
    QVirtioIOMMU iommu;
};

struct QVirtioIOMMUDevice {
    QOSGraphObject obj;
    QVirtioIOMMU iommu;
};

#endif
