/*
 * RP2040 voltage regulator and chip reset emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_vreg.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define VREG_VREG          0x00
#define VREG_BOD           0x04
#define VREG_CHIP_RESET    0x08

#define VREG_ROK           BIT(12)
#define VREG_VSEL_MASK     0x000000f0
#define VREG_HIZ           BIT(1)
#define VREG_EN            BIT(0)
#define VREG_RW_MASK       (VREG_VSEL_MASK | VREG_HIZ | VREG_EN)
#define VREG_RESET         0x000000b1

#define BOD_VSEL_MASK      0x000000f0
#define BOD_EN             BIT(0)
#define BOD_RW_MASK        (BOD_VSEL_MASK | BOD_EN)
#define BOD_RESET          0x00000091

#define CHIP_RESET_RW_MASK 0x01000000

#define ATOMIC_ALIAS_MASK  0x3000
#define ATOMIC_XOR         0x1000
#define ATOMIC_SET         0x2000
#define ATOMIC_CLR         0x3000

static uint32_t rp2040_vreg_apply_alias(uint32_t old, uint32_t value,
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

static uint32_t rp2040_vreg_read_vreg(RP2040VregState *s)
{
    uint32_t value = s->vreg;

    if ((value & VREG_EN) && !(value & VREG_HIZ)) {
        value |= VREG_ROK;
    }

    return value;
}

static uint64_t rp2040_vreg_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040VregState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    switch (offset) {
    case VREG_VREG:
        value = rp2040_vreg_read_vreg(s);
        break;
    case VREG_BOD:
        value = s->bod;
        break;
    case VREG_CHIP_RESET:
        value = s->chip_reset;
        break;
    default:
        value = 0;
        rp2040_log_unimplemented_read("vreg_and_chip_reset", size,
                                      RP2040_VREG_BASE + addr, offset,
                                      value);
        break;
    }

    return value;
}

static void rp2040_vreg_write(void *opaque, hwaddr addr,
                              uint64_t value64, unsigned size)
{
    RP2040VregState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t value = value64;

    switch (offset) {
    case VREG_VREG:
        s->vreg = rp2040_vreg_apply_alias(s->vreg, value, alias) &
                  VREG_RW_MASK;
        break;
    case VREG_BOD:
        s->bod = rp2040_vreg_apply_alias(s->bod, value, alias) & BOD_RW_MASK;
        break;
    case VREG_CHIP_RESET:
        s->chip_reset =
            rp2040_vreg_apply_alias(s->chip_reset, value, alias) &
            CHIP_RESET_RW_MASK;
        break;
    default:
        rp2040_log_unimplemented_write("vreg_and_chip_reset", size,
                                       RP2040_VREG_BASE + addr, offset,
                                       value64);
        break;
    }
}

static const MemoryRegionOps rp2040_vreg_ops = {
    .read = rp2040_vreg_read,
    .write = rp2040_vreg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_vreg_reset(DeviceState *dev)
{
    RP2040VregState *s = RP2040_VREG(dev);

    s->vreg = VREG_RESET;
    s->bod = BOD_RESET;
    s->chip_reset = 0;
}

static void rp2040_vreg_init(Object *obj)
{
    RP2040VregState *s = RP2040_VREG(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_vreg_ops, s,
                          "rp2040.vreg_and_chip_reset", RP2040_VREG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_vreg_vmstate = {
    .name = TYPE_RP2040_VREG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(vreg, RP2040VregState),
        VMSTATE_UINT32(bod, RP2040VregState),
        VMSTATE_UINT32(chip_reset, RP2040VregState),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_vreg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_vreg_reset);
    dc->vmsd = &rp2040_vreg_vmstate;
}

static const TypeInfo rp2040_vreg_info = {
    .name          = TYPE_RP2040_VREG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040VregState),
    .instance_init = rp2040_vreg_init,
    .class_init    = rp2040_vreg_class_init,
};

static void rp2040_vreg_register_types(void)
{
    type_register_static(&rp2040_vreg_info);
}
type_init(rp2040_vreg_register_types)
