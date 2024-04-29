/*
 * Host IOMMU device abstract
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors: Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/host_iommu_device.h"

OBJECT_DEFINE_ABSTRACT_TYPE(HostIOMMUDevice,
                            host_iommu_device,
                            HOST_IOMMU_DEVICE,
                            OBJECT)

static void host_iommu_device_class_init(ObjectClass *oc, void *data)
{
}

static void host_iommu_device_init(Object *obj)
{
}

static void host_iommu_device_finalize(Object *obj)
{
}

/* Wrapper of HostIOMMUDeviceClass:check_cap */
int host_iommu_device_check_cap(HostIOMMUDevice *hiod, int cap, Error **errp)
{
    HostIOMMUDeviceClass *hiodc = HOST_IOMMU_DEVICE_GET_CLASS(hiod);
    if (!hiodc->check_cap) {
        error_setg(errp, ".check_cap() not implemented");
        return -EINVAL;
    }

    return hiodc->check_cap(hiod, cap, errp);
}

/* Implement check on common IOMMU capabilities */
int host_iommu_device_check_cap_common(HostIOMMUDevice *hiod, int cap,
                                       Error **errp)
{
    HostIOMMUDeviceCaps *caps = &hiod->caps;

    switch (cap) {
    case HOST_IOMMU_DEVICE_CAP_IOMMU_TYPE:
        return caps->type;
    case HOST_IOMMU_DEVICE_CAP_AW_BITS:
        return caps->aw_bits;
    default:
        error_setg(errp, "Not support query cap %x", cap);
        return -EINVAL;
    }
}
