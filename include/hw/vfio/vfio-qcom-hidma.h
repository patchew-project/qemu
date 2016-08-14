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

#ifndef HW_VFIO_VFIO_QCOM_HIDMA_H
#define HW_VFIO_VFIO_QCOM_HIDMA_H

#include "hw/vfio/vfio-platform.h"

#define TYPE_VFIO_QCOM_HIDMA "vfio-qcom-hidma"

/**
 * This device exposes:
 * - MMIO regions corresponding to its register space
 * - Active high IRQs
 * - Optional property 'dma-coherent'
 */
typedef struct VFIOQcomHidmaDevice {
    VFIOPlatformDevice vdev;
} VFIOQcomHidmaDevice;

typedef struct VFIOQcomHidmaDeviceClass {
    /*< private >*/
    VFIOPlatformDeviceClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
} VFIOQcomHidmaDeviceClass;

#define VFIO_QCOM_HIDMA_DEVICE(obj) \
     OBJECT_CHECK(VFIOQcomHidmaDevice, (obj), TYPE_VFIO_QCOM_HIDMA)
#define VFIO_QCOM_HIDMA_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(VFIOQcomHidmaDeviceClass, (klass), \
                        TYPE_VFIO_QCOM_HIDMA)
#define VFIO_QCOM_HIDMA_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VFIOQcomHidmaDeviceClass, (obj), \
                      TYPE_VFIO_QCOM_HIDMA)

#endif
