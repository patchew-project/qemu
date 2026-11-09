/*
 * Allwinner H618 System on Chip emulation
 *
 * Copyright (C) 2026 Khai Do <dovankhai97@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "hw/char/serial-mm.h"
#include "hw/misc/unimp.h"
#include "system/system.h"
#include "system/address-spaces.h"
#include "hw/arm/allwinner-h618.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"

/* Memory map */
const hwaddr allwinner_h618_memmap[] = {
    [AW_H618_DEV_SRAM_A1]  = 0x00020000,
    [AW_H618_DEV_SRAM_A2]  = 0x00100000,
    [AW_H618_DEV_GIC_DIST] = 0x03021000,
    [AW_H618_DEV_GIC_CPU]  = 0x03022000,
    [AW_H618_DEV_GIC_HYP]  = 0x03024000,
    [AW_H618_DEV_GIC_VCPU] = 0x03026000,
    [AW_H618_DEV_UART0]    = 0x05000000,
    [AW_H618_DEV_UART1]    = 0x05000400,
    [AW_H618_DEV_UART2]    = 0x05000800,
    [AW_H618_DEV_UART3]    = 0x05000c00,
    [AW_H618_DEV_SDRAM]    = 0x40000000,
};

/* List of unimplemented devices */
static const struct AwH618Unimplemented {
    const char *device_name;
    hwaddr base;
    hwaddr size;
} unimplemented[] = {
    { "ccu",       0x03001000, 4 * KiB },
    { "dma",       0x03002000, 4 * KiB },
    { "sid",       0x03006000, 4 * KiB },
    { "smc",       0x03007000, 4 * KiB },
    { "timer",     0x03009000, 4 * KiB },
    { "pwm",       0x0300a000, 4 * KiB },
    { "pio",       0x0300b000, 4 * KiB },
    { "mmc0",      0x04020000, 4 * KiB },
    { "mmc1",      0x04021000, 4 * KiB },
    { "mmc2",      0x04022000, 4 * KiB },
    { "rtc",       0x07000000, 4 * KiB },
    { "prcm",      0x07010000, 4 * KiB },
    { "r_pio",     0x07022000, 4 * KiB },
    { "r_uart",    0x07080000, 4 * KiB },
};

/* Interrupt mapping */
enum {
    AW_H618_GIC_PPI_MAINT     =  9,
    AW_H618_GIC_PPI_HYPTIMER  = 10,
    AW_H618_GIC_PPI_VIRTTIMER = 11,
    AW_H618_GIC_PPI_SECTIMER  = 13,
    AW_H618_GIC_PPI_PHYSTIMER = 14
};

enum {
    AW_H618_GIC_SPI_UART0 = 0,
    AW_H618_GIC_SPI_UART1 = 1,
    AW_H618_GIC_SPI_UART2 = 2,
    AW_H618_GIC_SPI_UART3 = 3,
};

enum {
    AW_H618_GIC_NUM_SPI = 128
};

static void allwinner_h618_init(Object *obj)
{
    AwH618State *s = AW_H618(obj);
    int i;

    s->memmap = allwinner_h618_memmap;

    for (i = 0; i < AW_H618_NUM_CPUS; i++) {
        object_initialize_child(obj, "cpu[*]", &s->cpus[i],
                                ARM_CPU_TYPE_NAME("cortex-a53"));
    }

    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GIC);
}

static void allwinner_h618_realize(DeviceState *dev, Error **errp)
{
    AwH618State *s = AW_H618(dev);
    unsigned int i;

    /* CPUs */
    for (i = 0; i < AW_H618_NUM_CPUS; i++) {
        /* Disable secondary CPUs until brought up by firmware/kernel */
        qdev_prop_set_bit(DEVICE(&s->cpus[i]), "start-powered-off",
                          i > 0);

        /*
         * Enable PSCI emulation with SMC calls so secondary CPUs
         * can be brought up.
         */
        qdev_prop_set_uint32(DEVICE(&s->cpus[i]), "psci-conduit",
                             QEMU_PSCI_CONDUIT_SMC);

        /* Mark realized */
        qdev_realize(DEVICE(&s->cpus[i]), NULL, &error_fatal);
    }

    /* Generic Interrupt Controller */
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", AW_H618_GIC_NUM_SPI +
                                                     GIC_INTERNAL);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 2);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", AW_H618_NUM_CPUS);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-security-extensions", false);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-virtualization-extensions", true);
    sysbus_realize(SYS_BUS_DEVICE(&s->gic), &error_fatal);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 0,
                    s->memmap[AW_H618_DEV_GIC_DIST]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 1,
                    s->memmap[AW_H618_DEV_GIC_CPU]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 2,
                    s->memmap[AW_H618_DEV_GIC_VCPU]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 3,
                    s->memmap[AW_H618_DEV_GIC_HYP]);

    /* Connect GIC outputs to CPU interrupt inputs and CPU timer to GIC PPI */
    for (i = 0; i < AW_H618_NUM_CPUS; i++) {
        DeviceState *cpudev = DEVICE(&s->cpus[i]);
        int ppibase = AW_H618_GIC_NUM_SPI + i * GIC_INTERNAL + GIC_NR_SGIS;
        int irq;
        /*
         * Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs used for this board.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = AW_H618_GIC_PPI_PHYSTIMER,
            [GTIMER_VIRT] = AW_H618_GIC_PPI_VIRTTIMER,
            [GTIMER_HYP]  = AW_H618_GIC_PPI_HYPTIMER,
            [GTIMER_SEC]  = AW_H618_GIC_PPI_SECTIMER,
        };

        /* Connect CPU timer outputs to GIC PPI inputs */
        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(DEVICE(&s->gic),
                                                   ppibase + timer_irq[irq]));
        }

        /* Connect GIC outputs to CPU interrupt inputs */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + AW_H618_NUM_CPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + (2 * AW_H618_NUM_CPUS),
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + (3 * AW_H618_NUM_CPUS),
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));

        /* GIC maintenance signal */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + (4 * AW_H618_NUM_CPUS),
                           qdev_get_gpio_in(DEVICE(&s->gic),
                                            ppibase + AW_H618_GIC_PPI_MAINT));
    }

    /* SRAM */
    memory_region_init_ram(&s->sram_a1, OBJECT(dev), "sram A1",
                           64 * KiB, &error_abort);
    memory_region_init_ram(&s->sram_a2, OBJECT(dev), "sram A2",
                           48 * KiB, &error_abort);
    memory_region_add_subregion(get_system_memory(),
                                s->memmap[AW_H618_DEV_SRAM_A1], &s->sram_a1);
    memory_region_add_subregion(get_system_memory(),
                                s->memmap[AW_H618_DEV_SRAM_A2], &s->sram_a2);

    /* UART0 */
    serial_mm_init(get_system_memory(), s->memmap[AW_H618_DEV_UART0], 2,
                   qdev_get_gpio_in(DEVICE(&s->gic), AW_H618_GIC_SPI_UART0),
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);
    /* UART1 */
    serial_mm_init(get_system_memory(), s->memmap[AW_H618_DEV_UART1], 2,
                   qdev_get_gpio_in(DEVICE(&s->gic), AW_H618_GIC_SPI_UART1),
                   115200, serial_hd(1), DEVICE_LITTLE_ENDIAN);

    /* Unimplemented devices */
    for (i = 0; i < ARRAY_SIZE(unimplemented); i++) {
        create_unimplemented_device(unimplemented[i].device_name,
                                    unimplemented[i].base,
                                    unimplemented[i].size);
    }
}

static void allwinner_h618_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = allwinner_h618_realize;
    /* Reason: uses serial_hd() in realize function */
    dc->user_creatable = false;
}

static const TypeInfo allwinner_h618_type_info = {
    .name = TYPE_AW_H618,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AwH618State),
    .instance_init = allwinner_h618_init,
    .class_init = allwinner_h618_class_init,
};

static void allwinner_h618_register_types(void)
{
    type_register_static(&allwinner_h618_type_info);
}

type_init(allwinner_h618_register_types)
