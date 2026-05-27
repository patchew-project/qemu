/*
 * Specific Purpose Memory (SPM) device
 *
 * TYPE_MEMORY_DEVICE subclass for boot-time-only memory exposed to the
 * guest as an E820 SOFT_RESERVED range with a SRAT memory-affinity entry.
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * Authors:
 *  FangSheng Huang <FangSheng.Huang@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_SPM_MEMORY_H
#define QEMU_SPM_MEMORY_H

#include "hw/mem/memory-device.h"
#include "hw/core/qdev.h"
#include "qom/object.h"
#include "system/hostmem.h"

#define TYPE_SPM_MEMORY "spm-memory"

OBJECT_DECLARE_TYPE(SpmMemoryDevice, SpmMemoryDeviceClass, SPM_MEMORY)

struct SpmMemoryDevice {
    /*< private >*/
    DeviceState parent_obj;
    QLIST_ENTRY(SpmMemoryDevice) next;

    /*< public >*/
    HostMemoryBackend *hostmem;    /* memdev= backend */
    uint32_t node;                 /* NUMA proximity domain (node=) */
    uint64_t addr;                 /* GPA (from addr= or framework-assigned) */
};

struct SpmMemoryDeviceClass {
    /*< private >*/
    DeviceClass parent_class;
};

#endif /* QEMU_SPM_MEMORY_H */
