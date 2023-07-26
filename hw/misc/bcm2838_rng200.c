/*
 * BCM2838 Random Number Generator emulation
 *
 * Copyright (C) 2022 Sergey Pushkarev <sergey.pushkarev@auriga.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/misc/bcm2838_rng200.h"
#include "trace.h"

static void bcm2838_rng200_rng_reset(BCM2838Rng200State *state)
{
    state->rng_ctrl.value = 0;

    trace_bcm2838_rng200_rng_soft_reset();
}

static uint64_t bcm2838_rng200_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    uint32_t res = 0;

    trace_bcm2838_rng200_read((void *)offset, size, res);
    return res;
}

static void bcm2838_rng200_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{

    trace_bcm2838_rng200_write((void *)offset, value, size);
}

static const MemoryRegionOps bcm2838_rng200_ops = {
    .read = bcm2838_rng200_read,
    .write = bcm2838_rng200_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void bcm2838_rng200_realize(DeviceState *dev, Error **errp)
{
    BCM2838Rng200State *s = BCM2838_RNG200(dev);

    if (s->rng == NULL) {
        Object *default_backend = object_new(TYPE_RNG_BUILTIN);

        object_property_add_child(OBJECT(dev), "default-backend",
                                  default_backend);
        object_unref(default_backend);

        object_property_set_link(OBJECT(dev), "rng", default_backend,
                                 errp);
    }

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static void bcm2838_rng200_init(Object *obj)
{
    BCM2838Rng200State *s = BCM2838_RNG200(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->clock = qdev_init_clock_in(DEVICE(s), "rbg-clock",
                                  NULL, s,
                                  ClockPreUpdate);
    if (s->clock == NULL) {
        error_setg(&error_fatal, "Failed to init RBG clock");
        return;
    }

    memory_region_init_io(&s->iomem, obj, &bcm2838_rng200_ops, s,
                          TYPE_BCM2838_RNG200, 0x28);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void bcm2838_rng200_reset(DeviceState *dev)
{
    BCM2838Rng200State *s = BCM2838_RNG200(dev);
    bcm2838_rng200_rng_reset(s);
}

static Property bcm2838_rng200_properties[] = {
    DEFINE_PROP_UINT32("rbg-period", BCM2838Rng200State, rbg_period, 250),
    DEFINE_PROP_UINT32("rng-fifo-cap", BCM2838Rng200State, rng_fifo_cap, 128),
    DEFINE_PROP_LINK("rng", BCM2838Rng200State, rng,
                     TYPE_RNG_BACKEND, RngBackend *),
    DEFINE_PROP_BOOL("use-timer", BCM2838Rng200State, use_timer, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void bcm2838_rng200_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2838_rng200_realize;
    dc->reset = bcm2838_rng200_reset;
    device_class_set_props(dc, bcm2838_rng200_properties);
}

static const TypeInfo bcm2838_rng200_info = {
    .name          = TYPE_BCM2838_RNG200,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2838Rng200State),
    .class_init    = bcm2838_rng200_class_init,
    .instance_init = bcm2838_rng200_init,
};

static void bcm2838_rng200_register_types(void)
{
    type_register_static(&bcm2838_rng200_info);
}

type_init(bcm2838_rng200_register_types)
