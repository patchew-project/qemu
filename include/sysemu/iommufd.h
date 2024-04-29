/*
 * iommufd container backend declaration
 *
 * Copyright (C) 2024 Intel Corporation.
 * Copyright Red Hat, Inc. 2024
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
 *          Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSEMU_IOMMUFD_H
#define SYSEMU_IOMMUFD_H

#include "qom/object.h"
#include "exec/hwaddr.h"
#include "exec/cpu-common.h"
#include <linux/iommufd.h>
#include "sysemu/host_iommu_device.h"

#define TYPE_IOMMUFD_BACKEND "iommufd"
OBJECT_DECLARE_TYPE(IOMMUFDBackend, IOMMUFDBackendClass, IOMMUFD_BACKEND)

struct IOMMUFDBackendClass {
    ObjectClass parent_class;
};

struct IOMMUFDBackend {
    Object parent;

    /*< protected >*/
    int fd;            /* /dev/iommu file descriptor */
    bool owned;        /* is the /dev/iommu opened internally */
    uint32_t users;

    /*< public >*/
};

int iommufd_backend_connect(IOMMUFDBackend *be, Error **errp);
void iommufd_backend_disconnect(IOMMUFDBackend *be);

int iommufd_backend_alloc_ioas(IOMMUFDBackend *be, uint32_t *ioas_id,
                               Error **errp);
void iommufd_backend_free_id(IOMMUFDBackend *be, uint32_t id);
int iommufd_backend_map_dma(IOMMUFDBackend *be, uint32_t ioas_id, hwaddr iova,
                            ram_addr_t size, void *vaddr, bool readonly);
int iommufd_backend_unmap_dma(IOMMUFDBackend *be, uint32_t ioas_id,
                              hwaddr iova, ram_addr_t size);
int iommufd_backend_get_device_info(IOMMUFDBackend *be, uint32_t devid,
                                    enum iommu_hw_info_type *type,
                                    void *data, uint32_t len, Error **errp);

#define TYPE_HOST_IOMMU_DEVICE_IOMMUFD TYPE_HOST_IOMMU_DEVICE "-iommufd"
OBJECT_DECLARE_TYPE(HostIOMMUDeviceIOMMUFD, HostIOMMUDeviceIOMMUFDClass,
                    HOST_IOMMU_DEVICE_IOMMUFD)

/* Abstract of host IOMMU device with iommufd backend */
struct HostIOMMUDeviceIOMMUFD {
    HostIOMMUDevice parent_obj;

    IOMMUFDBackend *iommufd;
    uint32_t devid;
};

struct HostIOMMUDeviceIOMMUFDClass {
    HostIOMMUDeviceClass parent_class;
};
#endif
