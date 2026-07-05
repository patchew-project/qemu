/*
 * RP2040 reset controller emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_resets.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define RESETS_RESET       0x00
#define RESETS_WDSEL       0x04
#define RESETS_RESET_DONE  0x08

#define RESETS_VALID_MASK  0x01ffffff

#define ATOMIC_ALIAS_MASK  0x3000
#define ATOMIC_XOR         0x1000
#define ATOMIC_SET         0x2000
#define ATOMIC_CLR         0x3000

static uint32_t rp2040_resets_apply_alias(uint32_t old, uint32_t value,
                                          hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static uint64_t rp2040_resets_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040ResetsState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    switch (offset) {
    case RESETS_RESET:
        value = s->reset;
        break;
    case RESETS_WDSEL:
        value = s->wdsel;
        break;
    case RESETS_RESET_DONE:
        value = (~s->reset) & RESETS_VALID_MASK;
        break;
    default:
        value = 0;
        rp2040_log_unimplemented_read("resets", size,
                                      RP2040_RESETS_BASE + addr, offset,
                                      value);
        break;
    }

    return value;
}

static void rp2040_resets_write(void *opaque, hwaddr addr,
                                uint64_t value64, unsigned size)
{
    RP2040ResetsState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t value = value64;

    switch (offset) {
    case RESETS_RESET:
        s->reset = rp2040_resets_apply_alias(s->reset, value, alias) &
                   RESETS_VALID_MASK;
        break;
    case RESETS_WDSEL:
        s->wdsel = rp2040_resets_apply_alias(s->wdsel, value, alias) &
                   RESETS_VALID_MASK;
        break;
    case RESETS_RESET_DONE:
        break;
    default:
        rp2040_log_unimplemented_write("resets", size,
                                       RP2040_RESETS_BASE + addr, offset,
                                       value64);
        break;
    }
}

static const MemoryRegionOps rp2040_resets_ops = {
    .read = rp2040_resets_read,
    .write = rp2040_resets_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_resets_reset(DeviceState *dev)
{
    RP2040ResetsState *s = RP2040_RESETS(dev);

    s->reset = RESETS_VALID_MASK;
    s->wdsel = 0;
}

static void rp2040_resets_init(Object *obj)
{
    RP2040ResetsState *s = RP2040_RESETS(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_resets_ops, s,
                          "rp2040.resets", RP2040_RESETS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_resets_vmstate = {
    .name = TYPE_RP2040_RESETS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(reset, RP2040ResetsState),
        VMSTATE_UINT32(wdsel, RP2040ResetsState),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_resets_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_resets_reset);
    dc->vmsd = &rp2040_resets_vmstate;
}

static const TypeInfo rp2040_resets_info = {
    .name          = TYPE_RP2040_RESETS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040ResetsState),
    .instance_init = rp2040_resets_init,
    .class_init    = rp2040_resets_class_init,
};

static void rp2040_resets_register_types(void)
{
    type_register_static(&rp2040_resets_info);
}
type_init(rp2040_resets_register_types)
