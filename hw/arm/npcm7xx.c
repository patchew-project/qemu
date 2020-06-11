/*
 * Nuvoton NPCM7xx SoC family.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"

#include "exec/address-spaces.h"
#include "hw/arm/npcm7xx.h"
#include "hw/char/serial.h"
#include "hw/loader.h"
#include "hw/misc/unimp.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/units.h"
#include "sysemu/sysemu.h"

/* The first half of the address space is reserved for DDR4 DRAM. */
#define NPCM7XX_DRAM_BA         (0x00000000)
#define NPCM7XX_DRAM_SZ         (2 * GiB)

/*
 * This covers the whole MMIO space. We'll use this to catch any MMIO accesses
 * that aren't handled by any device.
 */
#define NPCM7XX_MMIO_BA         (0x80000000)
#define NPCM7XX_MMIO_SZ         (0x7FFD0000)

/* OTP key storage and fuse strap array */
#define NPCM7XX_OTP1_BA         (0xF0189000)
#define NPCM7XX_OTP2_BA         (0xF018a000)

/* Core system modules. */
#define NPCM7XX_L2C_BA          (0xF03FC000)
#define NPCM7XX_CPUP_BA         (0xF03FE000)
#define NPCM7XX_GCR_BA          (0xF0800000)
#define NPCM7XX_CLK_BA          (0xF0801000)
#define NPCM7XX_MC_BA           (0xF0824000)

/* Memory blocks at the end of the address space */
#define NPCM7XX_RAM2_BA         (0xFFFD0000)
#define NPCM7XX_RAM2_SZ         (128 * KiB)
#define NPCM7XX_ROM_BA          (0xFFFF0000)
#define NPCM7XX_ROM_SZ          (64 * KiB)

/*
 * Interrupt lines going into the GIC. This does not include internal Cortex-A9
 * interrupts.
 */
enum NPCM7xxInterrupt {
    NPCM7XX_UART0_IRQ           = 2,
    NPCM7XX_UART1_IRQ,
    NPCM7XX_UART2_IRQ,
    NPCM7XX_UART3_IRQ,
    NPCM7XX_TIMER0_IRQ          = 32,   /* Timer Module 0 */
    NPCM7XX_TIMER1_IRQ,
    NPCM7XX_TIMER2_IRQ,
    NPCM7XX_TIMER3_IRQ,
    NPCM7XX_TIMER4_IRQ,
    NPCM7XX_TIMER5_IRQ,                 /* Timer Module 1 */
    NPCM7XX_TIMER6_IRQ,
    NPCM7XX_TIMER7_IRQ,
    NPCM7XX_TIMER8_IRQ,
    NPCM7XX_TIMER9_IRQ,
    NPCM7XX_TIMER10_IRQ,                /* Timer Module 2 */
    NPCM7XX_TIMER11_IRQ,
    NPCM7XX_TIMER12_IRQ,
    NPCM7XX_TIMER13_IRQ,
    NPCM7XX_TIMER14_IRQ,
};

/* Total number of GIC interrupts, including internal Cortex-A9 interrupts. */
#define NPCM7XX_NUM_IRQ         (160)

/* Register base address for each Timer Module */
static const hwaddr npcm7xx_tim_addr[] = {
    0xF0008000,
    0xF0009000,
    0xF000A000,
};

/* Register base address for each 16550 UART */
static const hwaddr npcm7xx_uart_addr[] = {
    0xF0001000,
    0xF0002000,
    0xF0003000,
    0xF0004000,
};

static const hwaddr npcm7xx_fiu0_flash_addr[] = {
    0x80000000,
    0x88000000,
};

static const hwaddr npcm7xx_fiu3_flash_addr[] = {
    0xa0000000,
    0xa8000000,
    0xb0000000,
    0xb8000000,
};

static const struct {
    const char *name;
    hwaddr regs_addr;
    int cs_count;
    const hwaddr *flash_addr;
} npcm7xx_fiu[] = {
    {
        .name = "fiu0",
        .regs_addr = 0xfb000000,
        .cs_count = ARRAY_SIZE(npcm7xx_fiu0_flash_addr),
        .flash_addr = npcm7xx_fiu0_flash_addr,
    }, {
        .name = "fiu3",
        .regs_addr = 0xc0000000,
        .cs_count = ARRAY_SIZE(npcm7xx_fiu3_flash_addr),
        .flash_addr = npcm7xx_fiu3_flash_addr,
    },
};

void npcm7xx_write_secondary_boot(ARMCPU *cpu, const struct arm_boot_info *info)
{
    /*
     * The default smpboot stub halts the secondary CPU with a 'wfi'
     * instruction, but the arch/arm/mach-npcm/platsmp.c in the Linux kernel
     * does not send an IPI to wake it up, so the second CPU fails to boot. So
     * we need to provide our own smpboot stub that can not use 'wfi', it has
     * to spin the secondary CPU until the first CPU writes to the SCRPAD reg.
     */
    static const uint8_t smpboot[] = {
        0x18, 0x20, 0x9f, 0xe5,     /* ldr r2, bootreg_addr */
        0x00, 0x00, 0xa0, 0xe3,     /* mov r0, #0 */
        0x00, 0x00, 0x82, 0xe5,     /* str r0, [r2] */
        0x02, 0xf0, 0x20, 0xe3,     /* wfe */
        0x00, 0x10, 0x92, 0xe5,     /* ldr r1, [r2] */
        0x01, 0x00, 0x11, 0xe1,     /* tst r1, r1 */
        0xfb, 0xff, 0xff, 0x0a,     /* beq <wfe> */
        0x11, 0xff, 0x2f, 0xe1,     /* bx r1 */
        (NPCM7XX_SMP_BOOTREG_ADDR >>  0) & 0xff,
        (NPCM7XX_SMP_BOOTREG_ADDR >>  8) & 0xff,
        (NPCM7XX_SMP_BOOTREG_ADDR >> 16) & 0xff,
        (NPCM7XX_SMP_BOOTREG_ADDR >> 24) & 0xff,
    };

    rom_add_blob_fixed("smpboot", smpboot, sizeof(smpboot),
                       NPCM7XX_SMP_LOADER_START);
}

static void npcm7xx_init_fuses(NPCM7xxState *s)
{
    NPCM7xxClass *nc = NPCM7XX_GET_CLASS(s);
    uint32_t value;

    value = tswap32(nc->disabled_modules);
    npcm7xx_otp_array_write(&s->fuse_array, &value, 64, sizeof(value));
}

static qemu_irq npcm7xx_irq(NPCM7xxState *s, int n)
{
    return qdev_get_gpio_in(DEVICE(&s->a9mpcore), n);
}

static void npcm7xx_init(Object *obj)
{
    NPCM7xxState *s = NPCM7XX(obj);
    int i;

    for (i = 0; i < NPCM7XX_MAX_NUM_CPUS; i++) {
        object_initialize_child(obj, "cpu[*]", OBJECT(&s->cpu[i]),
                                sizeof(s->cpu[i]),
                                ARM_CPU_TYPE_NAME("cortex-a9"),
                                &error_abort, NULL);
    }

    sysbus_init_child_obj(obj, "a9mpcore", &s->a9mpcore,
                          sizeof(s->a9mpcore), TYPE_A9MPCORE_PRIV);
    sysbus_init_child_obj(obj, "gcr", OBJECT(&s->gcr), sizeof(s->gcr),
                          TYPE_NPCM7XX_GCR);
    sysbus_init_child_obj(obj, "clk", OBJECT(&s->clk), sizeof(s->clk),
                          TYPE_NPCM7XX_CLK);
    sysbus_init_child_obj(obj, "otp1", OBJECT(&s->key_storage),
                          sizeof(s->key_storage), TYPE_NPCM7XX_KEY_STORAGE);
    sysbus_init_child_obj(obj, "otp2", OBJECT(&s->fuse_array),
                          sizeof(s->fuse_array), TYPE_NPCM7XX_FUSE_ARRAY);
    sysbus_init_child_obj(obj, "mc", OBJECT(&s->mc), sizeof(s->mc),
                          TYPE_NPCM7XX_MC);

    for (i = 0; i < ARRAY_SIZE(s->tim); i++) {
        sysbus_init_child_obj(obj, "tim[*]", OBJECT(&s->tim[i]),
                              sizeof(s->tim[i]), TYPE_NPCM7XX_TIMER);
    }

    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_fiu) != ARRAY_SIZE(s->fiu));
    for (i = 0; i < ARRAY_SIZE(s->fiu); i++) {
        sysbus_init_child_obj(obj, npcm7xx_fiu[i].name,
                              OBJECT(&s->fiu[i]), sizeof(s->fiu[i]),
                              TYPE_NPCM7XX_FIU);
    }
}

static void npcm7xx_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxState *s = NPCM7XX(dev);
    NPCM7xxClass *sc = NPCM7XX_GET_CLASS(s);
    Error *err = NULL;
    int i;

    /* I/O space -- unimplemented unless overridden below. */
    create_unimplemented_device("npcm7xx.io", NPCM7XX_MMIO_BA, NPCM7XX_MMIO_SZ);

    /* CPUs */
    for (i = 0; i < sc->num_cpus; i++) {
        object_property_set_int(OBJECT(&s->cpu[i]),
                                arm_cpu_mp_affinity(i, NPCM7XX_MAX_NUM_CPUS),
                                "mp-affinity", &error_abort);
        object_property_set_int(OBJECT(&s->cpu[i]), NPCM7XX_GIC_CPU_IF_ADDR,
                                "reset-cbar", &error_abort);
        object_property_set_bool(OBJECT(&s->cpu[i]), true,
                                 "reset-hivecs", &error_abort);

        /* Disable security extensions. */
        if (object_property_find(OBJECT(&s->cpu[i]), "has_el3", NULL)) {
            object_property_set_bool(OBJECT(&s->cpu[i]), false, "has_el3",
                                     &error_abort);
        }

        object_property_set_bool(OBJECT(&s->cpu[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    /* A9MPCORE peripherals */
    object_property_set_int(OBJECT(&s->a9mpcore), sc->num_cpus, "num-cpu",
                            &error_abort);
    object_property_set_int(OBJECT(&s->a9mpcore), NPCM7XX_NUM_IRQ, "num-irq",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->a9mpcore), true, "realized",
                             &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->a9mpcore), 0, NPCM7XX_CPUP_BA);

    for (i = 0; i < sc->num_cpus; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->a9mpcore), i,
                           qdev_get_gpio_in(DEVICE(&s->cpu[i]), ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->a9mpcore), i + sc->num_cpus,
                           qdev_get_gpio_in(DEVICE(&s->cpu[i]), ARM_CPU_FIQ));
    }

    /* L2 cache controller */
    sysbus_create_simple("l2x0", NPCM7XX_L2C_BA, NULL);

    /* System Global Control Registers (GCR) */
    object_property_set_int(OBJECT(&s->gcr), sc->disabled_modules,
                            "disabled-modules", &err);
    object_property_set_link(OBJECT(&s->gcr), OBJECT(s->dram), "dram", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->gcr), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gcr), 0, NPCM7XX_GCR_BA);

    /* Clock Control Registers (CLK) */
    object_property_set_bool(OBJECT(&s->clk), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->clk), 0, NPCM7XX_CLK_BA);

    /* OTP key storage and fuse strap array */
    object_property_set_bool(OBJECT(&s->key_storage), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->key_storage), 0, NPCM7XX_OTP1_BA);
    object_property_set_bool(OBJECT(&s->fuse_array), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fuse_array), 0, NPCM7XX_OTP2_BA);
    npcm7xx_init_fuses(s);

    /* Fake Memory Controller (MC) */
    object_property_set_bool(OBJECT(&s->mc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mc), 0, NPCM7XX_MC_BA);

    /* Timer Modules (TIM) */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_tim_addr) != ARRAY_SIZE(s->tim));
    for (i = 0; i < ARRAY_SIZE(s->tim); i++) {
        Object *t = OBJECT(&s->tim[i]);
        int first_irq;
        int j;

        object_property_set_bool(t, true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(t), 0, npcm7xx_tim_addr[i]);

        first_irq = NPCM7XX_TIMER0_IRQ + i * NPCM7XX_TIMERS_PER_CTRL;
        for (j = 0; j < NPCM7XX_TIMERS_PER_CTRL; j++) {
            qemu_irq irq = npcm7xx_irq(s, first_irq + j);
            sysbus_connect_irq(SYS_BUS_DEVICE(t), j, irq);
        }
    }

    /* UART0..3 (16550 compatible) */
    for (i = 0; i < ARRAY_SIZE(npcm7xx_uart_addr); i++) {
        serial_mm_init(get_system_memory(), npcm7xx_uart_addr[i], 2,
                       npcm7xx_irq(s, NPCM7XX_UART0_IRQ + i), 115200,
                       serial_hd(i), DEVICE_LITTLE_ENDIAN);
    }

    /* Flash Interface Unit (FIU) */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_fiu) != ARRAY_SIZE(s->fiu));
    for (i = 0; i < ARRAY_SIZE(s->fiu); i++) {
        Object *o = OBJECT(&s->fiu[i]);
        int j;

        object_property_set_int(o, npcm7xx_fiu[i].cs_count, "cs-count",
                                &error_abort);
        object_property_set_bool(o, true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(o), 0, npcm7xx_fiu[i].regs_addr);
        for (j = 0; j < npcm7xx_fiu[i].cs_count; j++) {
            sysbus_mmio_map(SYS_BUS_DEVICE(o), j + 1,
                            npcm7xx_fiu[i].flash_addr[j]);
        }
    }

    /* RAM2 (SRAM) */
    memory_region_init_ram(&s->sram, OBJECT(dev), "ram2",
                           NPCM7XX_RAM2_SZ, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(get_system_memory(), NPCM7XX_RAM2_BA, &s->sram);

    /* Internal ROM */
    memory_region_init_rom(&s->irom, OBJECT(dev), "irom", NPCM7XX_ROM_SZ, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(get_system_memory(), NPCM7XX_ROM_BA, &s->irom);

    if (bios_name) {
        g_autofree char *filename = NULL;
        int ret;

        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (!filename) {
            error_setg(errp, "Could not find ROM image '%s'", bios_name);
            return;
        }
        ret = load_image_mr(filename, &s->irom);
        if (ret < 0) {
            error_setg(errp, "Failed to load ROM image '%s'", filename);
            return;
        }
    }

    /* External DDR4 SDRAM */
    memory_region_add_subregion(get_system_memory(), NPCM7XX_DRAM_BA, s->dram);
}

static Property npcm7xx_properties[] = {
    DEFINE_PROP_LINK("dram", NPCM7xxState, dram, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void npcm7xx_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = npcm7xx_realize;
    dc->user_creatable = false;
    device_class_set_props(dc, npcm7xx_properties);
}

static void npcm730_class_init(ObjectClass *oc, void *data)
{
    NPCM7xxClass *nc = NPCM7XX_CLASS(oc);

    /* NPCM730 is optimized for data center use, so no graphics, etc. */
    nc->disabled_modules = 0x00300395;
    nc->num_cpus = 2;
}

static void npcm750_class_init(ObjectClass *oc, void *data)
{
    NPCM7xxClass *nc = NPCM7XX_CLASS(oc);

    /* NPCM750 has 2 cores and a full set of peripherals */
    nc->disabled_modules = 0x00000000;
    nc->num_cpus = 2;
}

static const TypeInfo npcm7xx_soc_types[] = {
    {
        .name           = TYPE_NPCM7XX,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(NPCM7xxState),
        .instance_init  = npcm7xx_init,
        .class_size     = sizeof(NPCM7xxClass),
        .class_init     = npcm7xx_class_init,
        .abstract       = true,
    }, {
        .name           = TYPE_NPCM730,
        .parent         = TYPE_NPCM7XX,
        .class_init     = npcm730_class_init,
    }, {
        .name           = TYPE_NPCM750,
        .parent         = TYPE_NPCM7XX,
        .class_init     = npcm750_class_init,
    },
};

DEFINE_TYPES(npcm7xx_soc_types);
