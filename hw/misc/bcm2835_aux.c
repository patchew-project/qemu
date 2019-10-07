/*
 * BCM2835 (Raspberry Pi / Pi 2) Aux block (mini UART and SPI).
 * Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GPL.
 *
 * The following features/registers are unimplemented:
 *  - Extra control
 *  - Baudrate
 *  - SPI interfaces
 */

#include "qemu/osdep.h"
#include "hw/misc/bcm2835_aux.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"

REG32(AUX_IRQ,      0x00)
REG32(AUX_ENABLE,   0x04)

static const bool aux_enable_supported = false;

static void bcm2835_aux_update(BCM2835AuxState *s)
{
    qemu_set_irq(s->irq, !!(s->reg[R_AUX_IRQ] & s->reg[R_AUX_ENABLE]));
}

static void bcm2835_aux_set_irq(void *opaque, int irq, int level)
{
    BCM2835AuxState *s = opaque;

    s->reg[R_AUX_IRQ] = deposit32(s->reg[R_AUX_IRQ], irq, 1, !!level);

    bcm2835_aux_update(s);
}

static uint64_t bcm2835_aux_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2835AuxState *s = BCM2835_AUX(opaque);
    uint32_t res = 0;

    switch (offset) {
    case A_AUX_IRQ:
        res = s->reg[R_AUX_IRQ];
        break;

    case A_AUX_ENABLE:
        res = s->reg[R_AUX_ENABLE];
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        break;
    }
    trace_bcm2835_aux_read(offset, res);

    return res;
}

static void bcm2835_aux_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    BCM2835AuxState *s = BCM2835_AUX(opaque);

    trace_bcm2835_aux_write(offset, value);
    switch (offset) {
    case A_AUX_ENABLE:
        if (value <= 1) {
            if (aux_enable_supported) {
                memory_region_set_enabled(
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0),
                        value);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "%s: unsupported attempt to enable SPI:"
                                     " 0x%"PRIx64"\n",
                          __func__, value);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
    }

    bcm2835_aux_update(s);
}

static const MemoryRegionOps bcm2835_aux_ops = {
    .read = bcm2835_aux_read,
    .write = bcm2835_aux_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_aux = {
    .name = TYPE_BCM2835_AUX,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_aux_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    BCM2835AuxState *s = BCM2835_AUX(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_aux_ops, s,
                          TYPE_BCM2835_AUX, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qdev_init_gpio_in_named(DEVICE(obj), bcm2835_aux_set_irq, "aux-irq", 3);

    sysbus_init_child_obj(obj, "miniuart", &s->uart, sizeof(s->uart),
                          TYPE_BCM2835_MINIUART);
    object_property_add_alias(obj, "chardev", OBJECT(&s->uart), "chardev",
                              &error_abort);
}

static void bcm2835_aux_realize(DeviceState *dev, Error **errp)
{
    BCM2835AuxState *s = BCM2835_AUX(dev);
    Error *err = NULL;

    object_property_set_bool(OBJECT(&s->uart), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(&s->iomem, 0x40,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart), 0,
                qdev_get_gpio_in_named(dev, "aux-irq", 0));
}

static void bcm2835_aux_reset(DeviceState *dev)
{
    BCM2835AuxState *s = BCM2835_AUX(dev);

    s->reg[R_AUX_IRQ] = s->reg[R_AUX_ENABLE] = 0;

    if (aux_enable_supported) {
        memory_region_set_enabled(
                    sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0),
                    false);
    }
}

static Property bcm2835_aux_props[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void bcm2835_aux_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = bcm2835_aux_realize;
    dc->reset = bcm2835_aux_reset;
    dc->vmsd = &vmstate_bcm2835_aux;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = bcm2835_aux_props;
}

static const TypeInfo bcm2835_aux_info = {
    .name          = TYPE_BCM2835_AUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835AuxState),
    .instance_init = bcm2835_aux_init,
    .class_init    = bcm2835_aux_class_init,
};

static void bcm2835_aux_register_types(void)
{
    type_register_static(&bcm2835_aux_info);
}

type_init(bcm2835_aux_register_types)
