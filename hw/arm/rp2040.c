/*
 * RP2040 SoC Emulation
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/armv7m.h"
#include "hw/arm/rp2040.h"
#include "hw/misc/unimp.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"

typedef struct RP2040Class {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *name;
    const char *cpu_type;
} RP2040Class;

#define RP2040_CLASS(klass) \
    OBJECT_CLASS_CHECK(RP2040Class, (klass), TYPE_RP2040)
#define RP2040_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RP2040Class, (obj), TYPE_RP2040)

/* See Table 2.2.2 in the RP2040 Datasheet */
#define RP2040_SRAM_BASE  0x20000000
#define RP2040_SRAM4_BASE 0x20040000
#define RP2040_SRAM5_BASE 0x20041000

static void rp2040_init(Object *obj)
{
    RP2040State *s = RP2040(obj);
    int n;

    for (n = 0; n < RP2040_NCPUS; n++) {
        g_autofree char *name = g_strdup_printf("cpu[%d]", n);
        object_initialize_child(obj, name, &s->armv7m[n], TYPE_ARMV7M);
        qdev_prop_set_string(DEVICE(&s->armv7m[n]), "cpu-type",
                             ARM_CPU_TYPE_NAME("cortex-m0"));
        /*
         * Confusingly ARMv7M creates it's own per-core container so
         * we need to alias additional regions to avoid trying to give
         * a region two parents.
         */
        if (n > 0) {
            memory_region_init_alias(&s->memory_alias[n - 1], obj,
                                     "system-memory.alias", s->memory,
                                     0, -1);
        }
    }
}

static void rp2040_realize(DeviceState *dev, Error **errp)
{
    RP2040State *s = RP2040(dev);
    Object *obj = OBJECT(dev);
    int n;

    if (!s->memory) {
        error_setg(errp, "%s: memory property was not set", __func__);
        return;
    }

    /* initialize internal 16 KB internal ROM */
    memory_region_init_rom(&s->rom, obj, "rp2040.rom0", 16 * KiB, errp);
    memory_region_add_subregion(s->memory, 0, &s->rom);

    /* SRAM (Main 256k bank + two 4k banks)*/
    memory_region_init_ram(&s->sram03, obj, "rp2040.sram03", 256 * KiB, errp);
    memory_region_add_subregion(s->memory, RP2040_SRAM_BASE, &s->sram03);

    memory_region_init_ram(&s->sram4, obj, "rp2040.sram4", 4 * KiB, errp);
    memory_region_add_subregion(s->memory, RP2040_SRAM4_BASE, &s->sram4);

    memory_region_init_ram(&s->sram5, obj, "rp2040.sram5", 4 * KiB, errp);
    memory_region_add_subregion(s->memory, RP2040_SRAM5_BASE, &s->sram5);

    /* Map all the devices - see table 2.2.2 from the datasheet */

    /* APB Peripherals */
    create_unimplemented_device("rp2040.sysinfo",  0x40000000, 0x4000);
    create_unimplemented_device("rp2040.syscfg",   0x40004000, 0x4000);
    create_unimplemented_device("rp2040.clocks",   0x40008000, 0x4000);
    create_unimplemented_device("rp2040.resets",   0x4000c000, 0x4000);
    create_unimplemented_device("rp2040.psm",      0x40010000, 0x4000);
    create_unimplemented_device("rp2040.iobnk0",   0x40014000, 0x4000);
    create_unimplemented_device("rp2040.ioqspi",   0x40018000, 0x4000);
    create_unimplemented_device("rp2040.padsbnk0", 0x4001c000, 0x4000);
    create_unimplemented_device("rp2040.padsqspi", 0x40020000, 0x4000);
    create_unimplemented_device("rp2040.xosc",     0x40024000, 0x4000);

    create_unimplemented_device("rp2040.pllsys", 0x40028000, 0x4000);
    create_unimplemented_device("rp2040.pllusb", 0x4002c000, 0x4000);
    create_unimplemented_device("rp2040.busctrl", 0x40030000, 0x4000);
    create_unimplemented_device("rp2040.uart0", 0x40034000, 0x4000);
    create_unimplemented_device("rp2040.uart1", 0x40038000, 0x4000);
    create_unimplemented_device("rp2040.spi0", 0x4003c000, 0x4000);
    create_unimplemented_device("rp2040.spi1", 0x40040000, 0x4000);
    create_unimplemented_device("rp2040.i2c0", 0x40044000, 0x4000);
    create_unimplemented_device("rp2040.i2c1", 0x40048000, 0x4000);
    create_unimplemented_device("rp2040.adc", 0x4004c000, 0x4000);
    create_unimplemented_device("rp2040.pwm", 0x40050000, 0x4000);
    create_unimplemented_device("rp2040.timer", 0x40054000, 0x4000);
    create_unimplemented_device("rp2040.watchdog", 0x40058000, 0x4000);
    create_unimplemented_device("rp2040.rtc", 0x4005c000, 0x4000);
    create_unimplemented_device("rp2040.rosc", 0x40060000, 0x4000);
    create_unimplemented_device("rp2040.vreg&reset", 0x40064000, 0x4000);
    create_unimplemented_device("rp2040.tbman", 0x4006c000, 0x4000);

    /* AHB-Lite Peripherals */
    create_unimplemented_device("rp2040.dmabase",  0x50000000, 0x1000);

    /* USB */
    create_unimplemented_device("rp2040.usbram",   0x50100000, 0x10000);
    create_unimplemented_device("rp2040.usbregs",  0x50110000, 0x10000);

    /* Remaining AHB-Lite peripherals */
    create_unimplemented_device("rp2040.pi00",     0x50200000, 0x10000);
    create_unimplemented_device("rp2040.pi01",     0x50300000, 0x10000);

    /* IOPORT Peripherals */
    create_unimplemented_device("rp2040.sio",      0xd0000000, 0x10000000);

    /*
     * Cortex-M0+ internal peripherals (PPB_BASE) are handled by
     * the v7m model and live at 0xe0000000.
     */

    /* TODO: deal with stripped aliases */

    for (n = 0; n < RP2040_NCPUS; n++) {
        Object *cpuobj = OBJECT(&s->armv7m[n]);
        MemoryRegion *mr = n == 0 ? s->memory : &s->memory_alias[n - 1];
        object_property_set_link(cpuobj, "memory", OBJECT(mr), errp);

        /*
         * FIXME: temp hack - until more of the logic is emulated we
         * can't let the second CPU run off into the wild.
         */
        if (n > 0) {
            object_property_set_bool(cpuobj, "start-powered-off",
                                     true, &error_fatal);
        }

        if (!sysbus_realize(SYS_BUS_DEVICE(cpuobj), errp)) {
            return;
        }
    }
}

static Property rp2040_soc_properties[] = {
    DEFINE_PROP_LINK("memory", RP2040State, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void rp2040_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    RP2040Class *bc = RP2040_CLASS(oc);

    bc->cpu_type = ARM_CPU_TYPE_NAME("cortex-m0");
    dc->realize = rp2040_realize;
    device_class_set_props(dc, rp2040_soc_properties);
};

static const TypeInfo rp2040_types[] = {
    {
        .name           = TYPE_RP2040,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(RP2040State),
        .instance_init  = rp2040_init,
        .class_size     = sizeof(RP2040Class),
        .class_init     = rp2040_class_init,
    }
};

DEFINE_TYPES(rp2040_types)
