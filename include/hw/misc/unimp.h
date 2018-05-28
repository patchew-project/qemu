/*
 * "Unimplemented" device
 *
 * Copyright Linaro Limited, 2017
 * Written by Peter Maydell
 */

#ifndef HW_MISC_UNIMP_H
#define HW_MISC_UNIMP_H

#include "hw/sysbus.h"

#define TYPE_UNIMPLEMENTED_DEVICE "unimplemented-device"

#define UNIMPLEMENTED_DEVICE(obj) \
    OBJECT_CHECK(UnimplementedDeviceState, (obj), TYPE_UNIMPLEMENTED_DEVICE)

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    char *name;
    uint64_t size;
} UnimplementedDeviceState;

/**
 * create_unimplemented_subregion_device: create and map a dummy device
 *
 * @mr: the #MemoryRegion to contain the new device.
 * @name: name of the device for debug logging
 * @addr: base address of the device's MMIO region, or
 *        offset relative to @mr where the device is added.
 * @size: size of the device's MMIO region
 *
 * This utility function creates and maps an instance of unimplemented-device,
 * which is a dummy device which simply logs all guest accesses to
 * it via the qemu_log LOG_UNIMP debug log.
 * The device is mapped at priority -1000, which means that you can
 * use it to cover a large region and then map other devices on top of it
 * if necessary.
 */
static inline void create_unimplemented_subregion_device(MemoryRegion *mr,
                                                         const char *name,
                                                         hwaddr addr,
                                                         hwaddr size)
{
    DeviceState *dev = qdev_create(NULL, TYPE_UNIMPLEMENTED_DEVICE);

    qdev_prop_set_string(dev, "name", name);
    qdev_prop_set_uint64(dev, "size", size);
    qdev_init_nofail(dev);

    if (mr) {
        MemoryRegion *submr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion_overlap(mr, addr, submr, -1000);
    } else {
        sysbus_mmio_map_overlap(SYS_BUS_DEVICE(dev), 0, addr, -1000);
    }
}

/**
 * create_unimplemented_device: create and map a dummy SysBus device
 *
 * @name: name of the device for debug logging
 * @base: base address of the device's MMIO region
 * @size: size of the device's MMIO region
 *
 * This utility function creates and maps an instance of unimplemented-device,
 * which is a dummy device which simply logs all guest accesses to
 * it via the qemu_log LOG_UNIMP debug log.
 * The device is mapped at priority -1000, which means that you can
 * use it to cover a large region and then map other devices on top of it
 * if necessary.
 */
static inline void create_unimplemented_device(const char *name,
                                               hwaddr base,
                                               hwaddr size)
{
    create_unimplemented_subregion_device(NULL, name, base, size);
}

#endif
