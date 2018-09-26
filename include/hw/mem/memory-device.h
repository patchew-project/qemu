/*
 * Memory Device Interface
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MEMORY_DEVICE_H
#define MEMORY_DEVICE_H

#include "qom/object.h"
#include "hw/qdev.h"

#define TYPE_MEMORY_DEVICE "memory-device"

#define MEMORY_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(MemoryDeviceClass, (klass), TYPE_MEMORY_DEVICE)
#define MEMORY_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MemoryDeviceClass, (obj), TYPE_MEMORY_DEVICE)
#define MEMORY_DEVICE(obj) \
     INTERFACE_CHECK(MemoryDeviceState, (obj), TYPE_MEMORY_DEVICE)

typedef struct MemoryDeviceState {
    Object parent_obj;
} MemoryDeviceState;

/**
 * MemoryDeviceClass:
 * @get_addr: The address of the @md in guest physical memory. "0" means that
 * no address has been specified by the user and that no address has been
 * assigned yet.
 * @get_plugged_size: The amount of memory provided by this @md currently
 * usable ("plugged") by the guest. This is helpful for devices that
 * dynamically manage the amount of memory accessible by the guest via
 * the reserved memory region. For most devices, this corresponds to the
 * size of the memory region.
 * @get_region_size: The size of the memory region of the @md that's mapped
 * in guest physical memory at @get_addr.
 * @fill_device_info: Translate current @md state into #MemoryDeviceInfo.
 */
typedef struct MemoryDeviceClass {
    /* private */
    InterfaceClass parent_class;

    /* public */
    uint64_t (*get_addr)(const MemoryDeviceState *md);
    uint64_t (*get_plugged_size)(const MemoryDeviceState *md, Error **errp);
    uint64_t (*get_region_size)(const MemoryDeviceState *md, Error **errp);
    void (*fill_device_info)(const MemoryDeviceState *md,
                             MemoryDeviceInfo *info);
} MemoryDeviceClass;

MemoryDeviceInfoList *qmp_memory_device_list(void);
uint64_t get_plugged_memory_size(void);
uint64_t memory_device_get_free_addr(MachineState *ms, const uint64_t *hint,
                                     uint64_t align, uint64_t size,
                                     Error **errp);
void memory_device_plug_region(MachineState *ms, MemoryRegion *mr,
                               uint64_t addr);
void memory_device_unplug_region(MachineState *ms, MemoryRegion *mr);
uint64_t memory_device_get_region_size(const MemoryDeviceState *md,
                                       Error **errp);

#endif
