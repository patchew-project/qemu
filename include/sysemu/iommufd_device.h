/*
 * IOMMUFD Device
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSEMU_IOMMUFD_DEVICE_H
#define SYSEMU_IOMMUFD_DEVICE_H

#include <linux/iommufd.h>
#include "sysemu/iommufd.h"

typedef struct IOMMUFDDevice IOMMUFDDevice;

/* This is an abstraction of host IOMMUFD device */
struct IOMMUFDDevice {
    IOMMUFDBackend *iommufd;
    uint32_t dev_id;
};

int iommufd_device_get_info(IOMMUFDDevice *idev,
                            enum iommu_hw_info_type *type,
                            uint32_t len, void *data);
void iommufd_device_init(void *_idev, size_t instance_size,
                         IOMMUFDBackend *iommufd, uint32_t dev_id);
#endif
