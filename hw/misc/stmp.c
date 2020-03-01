/*
 * MXS "STMP" dummy device
 *
 * This is a dummy device which follows MXS "STMP" register layout.
 * It's useful for stubbing out regions of an SoC or board
 * map which correspond to devices that have not yet been
 * implemented, yet require "STMP" device specific reset support.
 * This is often sufficient to placate initial guest device
 * driver probing such that the system will come up.
 *
 * Derived from "unimplemented" device code.
 *      Copyright Linaro Limited, 2017
 *      Written by Peter Maydell
 *
 * Written by Guenter Roeck
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/misc/stmp.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

#define REG_VAL                 0x0
#define REG_SET                 0x4
#define REG_CLR                 0x8
#define REG_TOG                 0xc

#define STMP_MODULE_CLKGATE     (1 << 30)
#define STMP_MODULE_SFTRST      (1 << 31)

static uint64_t stmp_read(void *opaque, hwaddr offset, unsigned size)
{
    StmpDeviceState *s = STMP_DEVICE(opaque);

    switch (offset) {
    case REG_VAL:
        return s->regval;
    default:
        return 0;
    }
}

static void stmp_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    StmpDeviceState *s = STMP_DEVICE(opaque);

    switch (offset) {
    case REG_VAL:
        s->regval = value;
        break;
    case REG_SET:
        s->regval |= value;
        if (s->have_reset && (value & STMP_MODULE_SFTRST)) {
            s->regval |= STMP_MODULE_CLKGATE;
        }
        break;
    case REG_CLR:
        s->regval &= ~value;
        break;
    case REG_TOG:
        s->regval ^= value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps stmp_ops = {
    .read = stmp_read,
    .write = stmp_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stmp_realize(DeviceState *dev, Error **errp)
{
    StmpDeviceState *s = STMP_DEVICE(dev);

    if (s->name == NULL) {
        error_setg(errp, "property 'name' not specified");
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &stmp_ops, s,
                          s->name, 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static Property stmp_properties[] = {
    DEFINE_PROP_STRING("name", StmpDeviceState, name),
    DEFINE_PROP_BOOL("have-reset", StmpDeviceState, have_reset, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void stmp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stmp_realize;
    device_class_set_props(dc, stmp_properties);
}

static const TypeInfo stmp_info = {
    .name = TYPE_STMP_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StmpDeviceState),
    .class_init = stmp_class_init,
};

static void stmp_register_types(void)
{
    type_register_static(&stmp_info);
}

type_init(stmp_register_types)
