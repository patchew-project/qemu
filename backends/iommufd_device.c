/*
 * QEMU abstract of Host IOMMU
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/iommufd_device.h"

int iommufd_device_get_info(IOMMUFDDevice *idev,
                            enum iommu_hw_info_type *type,
                            uint32_t len, void *data)
{
    struct iommu_hw_info info = {
        .size = sizeof(info),
        .flags = 0,
        .dev_id = idev->dev_id,
        .data_len = len,
        .__reserved = 0,
        .data_uptr = (uintptr_t)data,
    };
    int ret;

    ret = ioctl(idev->iommufd->fd, IOMMU_GET_HW_INFO, &info);
    if (ret) {
        error_report("Failed to get info %m");
    } else {
        *type = info.out_data_type;
    }

    return ret;
}

void iommufd_device_init(void *_idev, size_t instance_size,
                         IOMMUFDBackend *iommufd, uint32_t dev_id)
{
    IOMMUFDDevice *idev = (IOMMUFDDevice *)_idev;

    g_assert(sizeof(IOMMUFDDevice) <= instance_size);

    idev->iommufd = iommufd;
    idev->dev_id = dev_id;
}
