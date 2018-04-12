/*
 * Memory Device Interface
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2014 Red Hat Inc
 * Copyright (c) 2018 Red Hat Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/mem/memory-device.h"
#include "hw/qdev.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "qemu/range.h"
#include "hw/virtio/vhost.h"
#include "sysemu/kvm.h"

static gint memory_device_addr_sort(gconstpointer a, gconstpointer b)
{
    MemoryDeviceState *md_a = MEMORY_DEVICE(a);
    MemoryDeviceState *md_b = MEMORY_DEVICE(b);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(a);
    const uint64_t addr_a = mdc->get_addr(md_a);
    const uint64_t addr_b = mdc->get_addr(md_b);

    if (addr_a > addr_b) {
        return 1;
    } else if (addr_a < addr_b) {
        return -1;
    }
    return 0;
}

static int memory_device_built_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_MEMORY_DEVICE)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* only realized memory devices matter */
            *list = g_slist_insert_sorted(*list, dev, memory_device_addr_sort);
        }
    }

    object_child_foreach(obj, memory_device_built_list, opaque);
    return 0;
}

MemoryDeviceInfoList *qmp_memory_device_list(void)
{
    GSList *devices = NULL, *item;
    MemoryDeviceInfoList *list = NULL, *prev = NULL;

    object_child_foreach(qdev_get_machine(), memory_device_built_list,
                         &devices);

    for (item = devices; item; item = g_slist_next(item)) {
        MemoryDeviceState *md = MEMORY_DEVICE(item->data);
        MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(item->data);
        MemoryDeviceInfoList *elem = g_new0(MemoryDeviceInfoList, 1);
        MemoryDeviceInfo *info = g_new0(MemoryDeviceInfo, 1);

        mdc->fill_device_info(md, info);

        elem->value = info;
        elem->next = NULL;
        if (prev) {
            prev->next = elem;
        } else {
            list = elem;
        }
        prev = elem;
    }

    g_slist_free(devices);

    return list;
}

static int memory_device_plugged_size(Object *obj, void *opaque)
{
    uint64_t *size = opaque;

    if (object_dynamic_cast(obj, TYPE_MEMORY_DEVICE)) {
        DeviceState *dev = DEVICE(obj);
        MemoryDeviceState *md = MEMORY_DEVICE(obj);
        MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(obj);

        if (dev->realized) {
            *size += mdc->get_plugged_size(md, &error_abort);
        }
    }

    object_child_foreach(obj, memory_device_plugged_size, opaque);
    return 0;
}

uint64_t get_plugged_memory_size(void)
{
    uint64_t size = 0;

    memory_device_plugged_size(qdev_get_machine(), &size);

    return size;
}

static int memory_device_used_region_size_internal(Object *obj, void *opaque)
{
    uint64_t *size = opaque;

    if (object_dynamic_cast(obj, TYPE_MEMORY_DEVICE)) {
        DeviceState *dev = DEVICE(obj);
        MemoryDeviceState *md = MEMORY_DEVICE(obj);
        MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(obj);

        if (dev->realized) {
            *size += mdc->get_region_size(md, &error_abort);
        }
    }

    object_child_foreach(obj, memory_device_used_region_size_internal, opaque);
    return 0;
}

static uint64_t memory_device_used_region_size(void)
{
    uint64_t size = 0;

    memory_device_used_region_size_internal(qdev_get_machine(), &size);

    return size;
}

uint64_t memory_device_get_free_addr(uint64_t *hint, uint64_t align,
                                     uint64_t size, Error **errp)
{
    const uint64_t used_region_size = memory_device_used_region_size();
    uint64_t address_space_start, address_space_end;
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryHotplugState *hpms;
    GSList *list = NULL, *item;
    uint64_t new_addr = 0;

    if (!mc->get_memory_hotplug_state) {
        error_setg(errp, "memory devices (e.g. for memory hotplug) are not "
                         "supported by the machine");
        return 0;
    }

    hpms = mc->get_memory_hotplug_state(machine);
    if (!hpms || !memory_region_size(&hpms->mr)) {
        error_setg(errp, "memory devices (e.g. for memory hotplug) are not "
                         "enabled, please specify the maxmem option");
        return 0;
    }
    address_space_start = hpms->base;
    address_space_end = address_space_start + memory_region_size(&hpms->mr);
    g_assert(address_space_end >= address_space_start);

    if (used_region_size + size > machine->maxram_size - machine->ram_size) {
        error_setg(errp, "not enough space, currently 0x%" PRIx64
                   " in use of total hot pluggable 0x" RAM_ADDR_FMT,
                   used_region_size, machine->maxram_size - machine->ram_size);
        return 0;
    }

    if (hint && QEMU_ALIGN_UP(*hint, align) != *hint) {
        error_setg(errp, "address must be aligned to 0x%" PRIx64 " bytes",
                   align);
        return 0;
    }

    if (QEMU_ALIGN_UP(size, align) != size) {
        error_setg(errp, "backend memory size must be multiple of 0x%"
                   PRIx64, align);
        return 0;
    }

    if (hint) {
        new_addr = *hint;
        if (new_addr < address_space_start) {
            error_setg(errp, "can't add memory [0x%" PRIx64 ":0x%" PRIx64
                       "] at 0x%" PRIx64, new_addr, size, address_space_start);
            return 0;
        } else if ((new_addr + size) > address_space_end) {
            error_setg(errp, "can't add memory [0x%" PRIx64 ":0x%" PRIx64
                       "] beyond 0x%" PRIx64, new_addr, size,
                       address_space_end);
            return 0;
        }
    } else {
        new_addr = address_space_start;
    }

    /* find address range that will fit new memory device */
    object_child_foreach(qdev_get_machine(), memory_device_built_list, &list);
    for (item = list; item; item = g_slist_next(item)) {
        MemoryDeviceState *md = item->data;
        MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(OBJECT(md));
        uint64_t md_size, md_addr;

        md_addr = mdc->get_addr(md);
        md_size = mdc->get_region_size(md, errp);
        if (*errp) {
            goto out;
        }

        if (ranges_overlap(md_addr, md_size, new_addr, size)) {
            if (hint) {
                DeviceState *d = DEVICE(md);
                error_setg(errp, "address range conflicts with '%s'", d->id);
                goto out;
            }
            new_addr = QEMU_ALIGN_UP(md_addr + md_size, align);
        }
    }

    if (new_addr + size > address_space_end) {
        error_setg(errp, "could not find position in guest address space for "
                   "memory device - memory fragmented due to alignments");
        goto out;
    }
out:
    g_slist_free(list);
    return new_addr;
}

void memory_device_plug_region(MemoryRegion *mr, uint64_t addr, Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryHotplugState *hpms;

    /* we expect a previous call to memory_device_get_free_addr() */
    g_assert(mc->get_memory_hotplug_state);
    hpms = mc->get_memory_hotplug_state(machine);
    g_assert(hpms);

    /* we will need a new memory slot for kvm and vhost */
    if (kvm_enabled() && !kvm_has_free_slot(machine)) {
        error_setg(errp, "hypervisor has no free memory slots left");
        return;
    }
    if (!vhost_has_free_slot()) {
        error_setg(errp, "a used vhost backend has no free memory slots left");
        return;
    }

    memory_region_add_subregion(&hpms->mr, addr - hpms->base, mr);
}

void memory_device_unplug_region(MemoryRegion *mr)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryHotplugState *hpms;

    /* we expect a previous call to memory_device_get_free_addr() */
    g_assert(mc->get_memory_hotplug_state);
    hpms = mc->get_memory_hotplug_state(machine);
    g_assert(hpms);

    memory_region_del_subregion(&hpms->mr, mr);
}

static const TypeInfo memory_device_info = {
    .name          = TYPE_MEMORY_DEVICE,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(MemoryDeviceClass),
};

static void memory_device_register_types(void)
{
    type_register_static(&memory_device_info);
}

type_init(memory_device_register_types)
