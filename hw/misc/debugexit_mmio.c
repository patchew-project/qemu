/*
 * Exit with status X when the guest writes X (little-endian) to a specified
 * address. For testing purposes only.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"

#define TYPE_MMIO_DEBUG_EXIT_DEVICE "mmio-debug-exit"
OBJECT_DECLARE_SIMPLE_TYPE(MMIODebugExitState, MMIO_DEBUG_EXIT_DEVICE)

struct MMIODebugExitState {
    DeviceState parent_obj;

    uint32_t base;
    uint32_t size;
    MemoryRegion region;
};

static uint64_t mmio_debug_exit_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void mmio_debug_exit_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned width)
{
    exit(val);
}

static const MemoryRegionOps mmio_debug_exit_ops = {
    .read = mmio_debug_exit_read,
    .write = mmio_debug_exit_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mmio_debug_exit_realizefn(DeviceState *d, Error **errp)
{
    MMIODebugExitState *s = MMIO_DEBUG_EXIT_DEVICE(d);

    memory_region_init_io(&s->region, OBJECT(s), &mmio_debug_exit_ops, s,
                          TYPE_MMIO_DEBUG_EXIT_DEVICE, s->size);
    memory_region_add_subregion(get_system_memory(), s->base, &s->region);
}

static Property mmio_debug_exit_properties[] = {
    DEFINE_PROP_UINT32("base", MMIODebugExitState, base, 0),
    DEFINE_PROP_UINT32("size", MMIODebugExitState, size, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void mmio_debug_exit_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mmio_debug_exit_realizefn;
    device_class_set_props(dc, mmio_debug_exit_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo mmio_debug_exit_info = {
    .name          = TYPE_MMIO_DEBUG_EXIT_DEVICE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(MMIODebugExitState),
    .class_init    = mmio_debug_exit_class_initfn,
};

static void mmio_debug_exit_register_types(void)
{
    type_register_static(&mmio_debug_exit_info);
}

type_init(mmio_debug_exit_register_types)
