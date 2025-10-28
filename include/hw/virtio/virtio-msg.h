/*
 * Virtio MSG bindings
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VIRTIO_MSG_H
#define HW_VIRTIO_MSG_H

#include "hw/sysbus.h"
#include "hw/virtio/virtio-bus.h"

#define TYPE_VIRTIO_MSG_PROXY_BUS "virtio-msg-proxy-bus"
/* This is reusing the VirtioBusState typedef from TYPE_VIRTIO_BUS */
DECLARE_OBJ_CHECKERS(VirtioBusState, VirtioBusClass,
                     VIRTIO_MSG_PROXY_BUS, TYPE_VIRTIO_MSG_PROXY_BUS)

#define TYPE_VIRTIO_MSG_OUTER_BUS "virtio-msg-outer-bus"
OBJECT_DECLARE_SIMPLE_TYPE(BusState, VIRTIO_MSG_OUTER_BUS)

#define TYPE_VIRTIO_MSG_DEV "virtio-msg-dev"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOMSGDev, VIRTIO_MSG_DEV)

#define TYPE_VIRTIO_MSG "virtio-msg"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOMSGProxy, VIRTIO_MSG)

struct VirtIOMSGDev {
    DeviceState parent_obj;

    /* virtio-bus */
    VirtioBusState bus;

    VirtIOMSGProxy *proxy;
    uint16_t dev_num;
    uint64_t guest_features;
};

#define VIRTIO_MSG_MAX_DEVS 32
struct VirtIOMSGProxy {
    DeviceState parent_obj;

    AddressSpace dma_as;
    AddressSpace *bus_as;
    IOMMUMemoryRegion mr_iommu;
    MemoryRegion *mr_bus;

    BusState devs_bus[VIRTIO_MSG_MAX_DEVS];
    VirtIOMSGDev devs[VIRTIO_MSG_MAX_DEVS];

    /* virtio-msg-bus.  */
    BusState msg_bus;
};
#endif
