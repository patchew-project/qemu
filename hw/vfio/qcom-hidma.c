/*
 * Qualcomm Technologies, Inc VFIO HiDMA platform device
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "qemu/osdep.h"
#include "hw/vfio/vfio-qcom-hidma.h"

static void qcom_hidma_realize(DeviceState *dev, Error **errp)
{
    VFIOPlatformDevice *vdev = VFIO_PLATFORM_DEVICE(dev);
    VFIOQcomHidmaDeviceClass *k = VFIO_QCOM_HIDMA_DEVICE_GET_CLASS(dev);

    vdev->compat = g_strdup("qcom,hidma-1.0");

    k->parent_realize(dev, errp);
}

static const VMStateDescription vfio_platform_vmstate = {
    .name = TYPE_VFIO_QCOM_HIDMA,
    .unmigratable = 1,
};

static void vfio_qcom_hidma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VFIOQcomHidmaDeviceClass *vcxc = VFIO_QCOM_HIDMA_DEVICE_CLASS(klass);

    vcxc->parent_realize = dc->realize;
    dc->realize = qcom_hidma_realize;
    dc->desc = "VFIO QCOM HIDMA";
    dc->vmsd = &vfio_platform_vmstate;
}

static const TypeInfo vfio_qcom_hidma_dev_info = {
    .name = TYPE_VFIO_QCOM_HIDMA,
    .parent = TYPE_VFIO_PLATFORM,
    .instance_size = sizeof(VFIOQcomHidmaDevice),
    .class_init = vfio_qcom_hidma_class_init,
    .class_size = sizeof(VFIOQcomHidmaDeviceClass),
};

static void register_qcom_hidma_dev_type(void)
{
    type_register_static(&vfio_qcom_hidma_dev_info);
}

type_init(register_qcom_hidma_dev_type)
