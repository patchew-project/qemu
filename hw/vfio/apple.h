/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Apple/macOS VFIO passthrough common definitions.
 *
 * Copyright (c) 2026 Scott J. Goldman
 */

#ifndef HW_VFIO_APPLE_H
#define HW_VFIO_APPLE_H

#include <stdint.h>

#include "hw/vfio/pci.h"
#include "hw/vfio/vfio-container.h"
#include "qapi/error.h"
#include "qemu/event_notifier.h"

#ifdef CONFIG_DARWIN
#include <IOKit/IOKitLib.h>
#else
typedef uintptr_t io_connect_t;
#define IO_OBJECT_NULL ((io_connect_t)0)
#endif

OBJECT_DECLARE_SIMPLE_TYPE(AppleVFIOContainer, VFIO_IOMMU_APPLE)

struct AppleVFIOContainer {
    VFIOContainer parent_obj;
    io_connect_t dext_conn;
    uint8_t host_bus;
    uint8_t host_device;
    uint8_t host_function;
};

typedef struct AppleDextInterruptNotify AppleDextInterruptNotify;

typedef struct AppleVFIOBarMap {
    void *addr;
    size_t size;
} AppleVFIOBarMap;

typedef struct AppleVFIOState {
    AppleDextInterruptNotify *irq_notify;
    EventNotifier irq_notifier;
    uint32_t num_irq_vectors;
    AppleVFIOBarMap bar_maps[PCI_ROM_SLOT];
} AppleVFIOState;

OBJECT_DECLARE_SIMPLE_TYPE(VFIOApplePCIDevice, VFIO_APPLE_PCI)

struct VFIOApplePCIDevice {
    VFIOPCIDevice parent_obj;
    AppleVFIOState *apple;
    DeviceState *dma_companion;
    bool dma_companion_autocreated;
};

extern VFIODeviceIOOps apple_vfio_device_io_ops;

bool apple_vfio_device_setup(VFIOApplePCIDevice *adev, Error **errp);
void apple_vfio_device_cleanup(VFIOApplePCIDevice *adev);
bool apple_vfio_get_bar_info(VFIOApplePCIDevice *adev, uint8_t bar,
                             uint8_t *mem_idx, uint64_t *size,
                             uint8_t *type);

bool apple_vfio_dext_publish(uint8_t bus, uint8_t device, uint8_t function,
                             io_connect_t conn);
io_connect_t apple_vfio_dext_lookup(uint8_t bus, uint8_t device,
                                    uint8_t function);
void apple_vfio_dext_release(uint8_t bus, uint8_t device, uint8_t function,
                             io_connect_t conn);

#endif /* HW_VFIO_APPLE_H */
