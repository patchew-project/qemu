/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Raspberry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * Rebase onto master (c) 2017 Omar Rizwan
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/arm/bcm2835.h"
#include "hw/arm/raspi_platform.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"

/* Peripheral base address seen by the CPU */
#define BCM2835_PERI_BASE       0x20000000

static void bcm2835_init(Object *obj)
{
    BCM2835State *s = BCM2835(obj);

    object_initialize(&s->cpu, sizeof(s->cpu), "arm1176-" TYPE_ARM_CPU);
    object_property_add_child(obj, "cpu", OBJECT(&s->cpu), &error_abort);

    object_initialize(&s->peripherals, sizeof(s->peripherals),
                      TYPE_BCM2835_PERIPHERALS);
    object_property_add_child(obj, "peripherals", OBJECT(&s->peripherals),
                              &error_abort);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->peripherals),
                              "board-rev", &error_abort);
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->peripherals),
                              "vcram-size", &error_abort);
    qdev_set_parent_bus(DEVICE(&s->peripherals), sysbus_get_default());
}

static void bcm2835_realize(DeviceState *dev, Error **errp)
{
    BCM2835State *s = BCM2835(dev);
    Object *obj;
    Error *err = NULL;

    /* common peripherals from bcm2835 */
    obj = object_property_get_link(OBJECT(dev), "ram", &err);
    if (obj == NULL) {
        error_setg(errp, "%s: required ram link not found: %s",
                   __func__, error_get_pretty(err));
        return;
    }

    object_property_add_const_link(OBJECT(&s->peripherals), "ram", obj, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->peripherals), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(&s->peripherals),
                              "sd-bus", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->peripherals), 0,
                            BCM2835_PERI_BASE, 1);

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->peripherals), 1,
                            0x40000000, 1);

    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &err);
    if (err) {
        error_report_err(err);
        exit(1);
    }

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 1,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));

}

static void bcm2835_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = bcm2835_realize;

    /*
     * Reason: creates an ARM CPU, thus use after free(), see
     * arm_cpu_class_init()
     */
    dc->cannot_destroy_with_object_finalize_yet = true;
}

static const TypeInfo bcm2835_type_info = {
    .name = TYPE_BCM2835,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835State),
    .instance_init = bcm2835_init,
    .class_init = bcm2835_class_init,
};

static void bcm2835_register_types(void)
{
    type_register_static(&bcm2835_type_info);
}

type_init(bcm2835_register_types)
