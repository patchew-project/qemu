/*
 * RP2040 SoC emulation
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/rp2040.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/misc/unimp.h"
#include "qemu/datadir.h"
#include "target/arm/cpu-qom.h"

#define RP2040_UART0_BASE 0x40034000
#define RP2040_UART0_IRQ  20
#define RP2040_UART1_BASE 0x40038000
#define RP2040_UART1_IRQ  21

/*
 * Temporary boot ROM used until the synthetic ROM is introduced. It loads
 * the reset handler from a vector table at the base of the XIP window.
 */
static const uint8_t rp2040_bootrom[] = {
    0x00, 0x20, 0x04, 0x20, /* initial SP: 0x20042000 */
    0x09, 0x00, 0x00, 0x00, /* reset handler: 0x00000009 */
    0x03, 0x48,             /* ldr r0, vtor */
    0x04, 0x49,             /* ldr r1, xip_base */
    0x01, 0x60,             /* str r1, [r0] */
    0x04, 0x48,             /* ldr r0, xip_reset_vector */
    0x01, 0x68,             /* ldr r1, [r0] */
    0x08, 0x47,             /* bx r1 */
    0xfe, 0xe7,             /* b . */
    0xc0, 0x46,             /* nop */
    0x08, 0xed, 0x00, 0xe0, /* VTOR: 0xe000ed08 */
    0x00, 0x00, 0x00, 0x10, /* XIP base: 0x10000000 */
    0x04, 0x00, 0x00, 0x10, /* XIP reset vector: 0x10000004 */
};

static const struct {
    const char *name;
    hwaddr base;
    hwaddr size;
} rp2040_unimplemented[] = {
    { "rp2040.resets",   0x4000c000, 0x4000 },
    { "rp2040.psm",      0x40010000, 0x4000 },
    { "rp2040.iobank0",  0x40014000, 0x4000 },
    { "rp2040.ioqspi",   0x40018000, 0x4000 },
    { "rp2040.padsbank0", 0x4001c000, 0x4000 },
    { "rp2040.padsqspi", 0x40020000, 0x4000 },
    { "rp2040.busctrl",  0x40030000, 0x4000 },
    { "rp2040.uart0_aliases", 0x40035000, 0x3000 },
    { "rp2040.uart1_aliases", 0x40039000, 0x3000 },
    { "rp2040.spi0",     0x4003c000, 0x4000 },
    { "rp2040.spi1",     0x40040000, 0x4000 },
    { "rp2040.i2c0",     0x40044000, 0x4000 },
    { "rp2040.i2c1",     0x40048000, 0x4000 },
    { "rp2040.adc",      0x4004c000, 0x4000 },
    { "rp2040.pwm",      0x40050000, 0x4000 },
    { "rp2040.timer",    0x40054000, 0x4000 },
    { "rp2040.watchdog", 0x40058000, 0x4000 },
    { "rp2040.rtc",      0x4005c000, 0x4000 },
    { "rp2040.dma",      0x50000000, 0x1000 },
    { "rp2040.usbctrl_dpram", 0x50100000, 0x10000 },
    { "rp2040.usbctrl_regs",  0x50110000, 0x10000 },
    { "rp2040.pio0",     0x50200000, 0x10000 },
    { "rp2040.pio1",     0x50300000, 0x10000 },
    { "rp2040.sio",      0xd0000000, 0x1000 },
};

static MemTxResult rp2040_powered_off_read(void *opaque, hwaddr addr,
                                           uint64_t *data, unsigned size,
                                           MemTxAttrs attrs)
{
    *data = 0;
    return MEMTX_ERROR;
}

static MemTxResult rp2040_powered_off_write(void *opaque, hwaddr addr,
                                            uint64_t data, unsigned size,
                                            MemTxAttrs attrs)
{
    return MEMTX_ERROR;
}

static const MemoryRegionOps rp2040_powered_off_ops = {
    .read_with_attrs = rp2040_powered_off_read,
    .write_with_attrs = rp2040_powered_off_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void rp2040_update_mempowerdown(RP2040State *s)
{
    uint32_t mempowerdown;
    int i;

    if (!s->mempowerdown_ready) {
        return;
    }

    mempowerdown = rp2040_syscfg_get_mempowerdown(&s->syscfg);
    for (i = 0; i < ARRAY_SIZE(s->sram_poweroff); i++) {
        memory_region_set_enabled(&s->sram_poweroff[i],
                                  mempowerdown & BIT(i));
    }
    memory_region_set_enabled(&s->rom_poweroff, mempowerdown & BIT(7));
}

static void rp2040_update_nmi(RP2040State *s)
{
    uint32_t nmi_mask = rp2040_syscfg_get_proc0_nmi_mask(&s->syscfg);
    bool nmi_level = false;
    int i;

    for (i = 0; i < RP2040_NUM_IRQS; i++) {
        bool route_to_nmi = nmi_mask & BIT(i);

        qemu_set_irq(s->cpu_irq[i], s->irq_level[i] && !route_to_nmi);
        nmi_level |= s->irq_level[i] && route_to_nmi;
    }
    qemu_set_irq(s->nmi_irq, nmi_level);
}

static void rp2040_syscfg_update(void *opaque)
{
    RP2040State *s = opaque;

    rp2040_update_mempowerdown(s);
    rp2040_update_nmi(s);
}

static void rp2040_set_irq(void *opaque, int irq, int level)
{
    RP2040State *s = opaque;

    assert(irq >= 0 && irq < RP2040_NUM_IRQS);
    s->irq_level[irq] = level;
    rp2040_update_nmi(s);
}

static void rp2040_soc_init(Object *obj)
{
    RP2040State *s = RP2040(obj);
    int i;

    object_initialize_child(obj, "proc0", &s->armv7m, TYPE_ARMV7M);
    qdev_prop_set_string(DEVICE(&s->armv7m), "cpu-type",
                         ARM_CPU_TYPE_NAME("cortex-m0"));
    qdev_prop_set_uint32(DEVICE(&s->armv7m), "num-irq", RP2040_NUM_IRQS);
    qdev_prop_set_uint32(DEVICE(&s->armv7m), "mpu-ns-regions", 8);

    for (i = 0; i < ARRAY_SIZE(s->uart); i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i);
        g_autofree char *property = g_strdup_printf("serial%d", i);

        object_initialize_child(obj, name, &s->uart[i], TYPE_PL011);
        object_property_add_alias(obj, property, OBJECT(&s->uart[i]),
                                  "chardev");
    }

    object_initialize_child(obj, "clocks", &s->clocks, TYPE_RP2040_CLOCKS);

    object_initialize_child(obj, "pll-sys", &s->pll_sys, TYPE_RP2040_PLL);
    qdev_prop_set_string(DEVICE(&s->pll_sys), "trace-name",
                         "rp2040.pll_sys");
    qdev_prop_set_uint32(DEVICE(&s->pll_sys), "base", RP2040_PLL_SYS_BASE);
    qdev_prop_set_uint32(DEVICE(&s->pll_sys), "fallback-hz", 125000000);

    object_initialize_child(obj, "pll-usb", &s->pll_usb, TYPE_RP2040_PLL);
    qdev_prop_set_string(DEVICE(&s->pll_usb), "trace-name",
                         "rp2040.pll_usb");
    qdev_prop_set_uint32(DEVICE(&s->pll_usb), "base", RP2040_PLL_USB_BASE);
    qdev_prop_set_uint32(DEVICE(&s->pll_usb), "fallback-hz", 48000000);

    object_initialize_child(obj, "syscfg", &s->syscfg, TYPE_RP2040_SYSCFG);
    object_initialize_child(obj, "sysinfo", &s->sysinfo, TYPE_RP2040_SYSINFO);
    object_initialize_child(obj, "rosc", &s->rosc, TYPE_RP2040_ROSC);
    object_initialize_child(obj, "tbman", &s->tbman, TYPE_RP2040_TBMAN);
    object_initialize_child(obj, "vreg", &s->vreg, TYPE_RP2040_VREG);
    object_initialize_child(obj, "xosc", &s->xosc, TYPE_RP2040_XOSC);

    s->irq = qemu_allocate_irqs(rp2040_set_irq, s, RP2040_NUM_IRQS);
    s->sysclk = clock_new(obj, "sysclk");
}

static bool rp2040_init_memory(RP2040State *s, Error **errp)
{
    int i;

    if (!memory_region_init_rom(&s->rom, OBJECT(s), "rp2040.rom",
                                RP2040_ROM_SIZE, errp)) {
        return false;
    }
    memory_region_add_subregion(s->board_memory, RP2040_ROM_BASE, &s->rom);
    memory_region_init_io(&s->rom_poweroff, OBJECT(s),
                          &rp2040_powered_off_ops, s,
                          "rp2040.rom.poweroff", RP2040_ROM_SIZE);
    memory_region_add_subregion_overlap(s->board_memory, RP2040_ROM_BASE,
                                        &s->rom_poweroff, 1);
    memory_region_set_enabled(&s->rom_poweroff, false);

    for (i = 0; i < 4; i++) {
        g_autofree char *name = g_strdup_printf("rp2040.sram%d", i);
        g_autofree char *poweroff_name =
            g_strdup_printf("rp2040.sram%d.poweroff", i);

        if (!memory_region_init_ram(&s->sram[i], OBJECT(s), name,
                                    RP2040_SRAM_BANK_SIZE, errp)) {
            return false;
        }
        memory_region_add_subregion(s->board_memory,
                                    RP2040_SRAM_BASE +
                                    i * RP2040_SRAM_BANK_SIZE,
                                    &s->sram[i]);
        memory_region_init_io(&s->sram_poweroff[i], OBJECT(s),
                              &rp2040_powered_off_ops, s, poweroff_name,
                              RP2040_SRAM_BANK_SIZE);
        memory_region_add_subregion_overlap(s->board_memory,
                                            RP2040_SRAM_BASE +
                                            i * RP2040_SRAM_BANK_SIZE,
                                            &s->sram_poweroff[i], 1);
        memory_region_set_enabled(&s->sram_poweroff[i], false);
    }

    if (!memory_region_init_ram(&s->sram[4], OBJECT(s), "rp2040.sram4",
                                RP2040_SRAM_HI_SIZE, errp)) {
        return false;
    }
    memory_region_add_subregion(s->board_memory, RP2040_SRAM4_BASE,
                                &s->sram[4]);
    memory_region_init_io(&s->sram_poweroff[4], OBJECT(s),
                          &rp2040_powered_off_ops, s,
                          "rp2040.sram4.poweroff", RP2040_SRAM_HI_SIZE);
    memory_region_add_subregion_overlap(s->board_memory, RP2040_SRAM4_BASE,
                                        &s->sram_poweroff[4], 1);
    memory_region_set_enabled(&s->sram_poweroff[4], false);

    if (!memory_region_init_ram(&s->sram[5], OBJECT(s), "rp2040.sram5",
                                RP2040_SRAM_HI_SIZE, errp)) {
        return false;
    }
    memory_region_add_subregion(s->board_memory, RP2040_SRAM5_BASE,
                                &s->sram[5]);
    memory_region_init_io(&s->sram_poweroff[5], OBJECT(s),
                          &rp2040_powered_off_ops, s,
                          "rp2040.sram5.poweroff", RP2040_SRAM_HI_SIZE);
    memory_region_add_subregion_overlap(s->board_memory, RP2040_SRAM5_BASE,
                                        &s->sram_poweroff[5], 1);
    memory_region_set_enabled(&s->sram_poweroff[5], false);

    s->mempowerdown_ready = true;

    return true;
}

static bool rp2040_load_bootrom(RP2040State *s, Error **errp)
{
    g_autofree char *filename = NULL;

    if (!s->bootrom_file) {
        rom_add_blob_fixed("rp2040.bootrom", rp2040_bootrom,
                           sizeof(rp2040_bootrom), RP2040_ROM_BASE);
        return true;
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, s->bootrom_file);
    if (!filename) {
        error_setg(errp, "could not find RP2040 boot ROM image '%s'",
                   s->bootrom_file);
        return false;
    }

    return load_image_targphys(filename, RP2040_ROM_BASE,
                               RP2040_ROM_SIZE, errp) >= 0;
}

static void rp2040_soc_realize(DeviceState *dev, Error **errp)
{
    RP2040State *s = RP2040(dev);
    static const hwaddr uart_base[] = { RP2040_UART0_BASE, RP2040_UART1_BASE };
    static const int uart_irq[] = { RP2040_UART0_IRQ, RP2040_UART1_IRQ };
    Error *err = NULL;
    int i;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }
    if (!rp2040_init_memory(s, errp) || !rp2040_load_bootrom(s, errp)) {
        return;
    }

    for (i = 0; i < ARRAY_SIZE(rp2040_unimplemented); i++) {
        create_unimplemented_device(rp2040_unimplemented[i].name,
                                    rp2040_unimplemented[i].base,
                                    rp2040_unimplemented[i].size);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pll_sys), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pll_sys), 0, RP2040_PLL_SYS_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pll_usb), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pll_usb), 0, RP2040_PLL_USB_BASE);

    qdev_connect_clock_in(DEVICE(&s->clocks), "pll-sys",
                          qdev_get_clock_out(DEVICE(&s->pll_sys), "clk"));
    qdev_connect_clock_in(DEVICE(&s->clocks), "pll-usb",
                          qdev_get_clock_out(DEVICE(&s->pll_usb), "clk"));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->clocks), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->clocks), 0, RP2040_CLOCKS_BASE);
    clock_set_source(s->sysclk, qdev_get_clock_out(DEVICE(&s->clocks),
                                                   "clk-sys"));

    qdev_connect_clock_in(DEVICE(&s->armv7m), "cpuclk", s->sysclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(s->board_memory), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }
    for (i = 0; i < RP2040_NUM_IRQS; i++) {
        s->cpu_irq[i] = qdev_get_gpio_in(DEVICE(&s->armv7m), i);
    }
    s->nmi_irq = qdev_get_gpio_in_named(DEVICE(&s->armv7m), "NMI", 0);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->syscfg), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->syscfg), 0, RP2040_SYSCFG_BASE);
    rp2040_syscfg_set_update_callback(&s->syscfg, rp2040_syscfg_update, s);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sysinfo), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysinfo), 0, RP2040_SYSINFO_BASE);
    rp2040_syscfg_update(s);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rosc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rosc), 0, RP2040_ROSC_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tbman), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tbman), 0, RP2040_TBMAN_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->vreg), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->vreg), 0, RP2040_VREG_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xosc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xosc), 0, RP2040_XOSC_BASE);

    for (i = 0; i < ARRAY_SIZE(s->uart); i++) {
        qdev_connect_clock_in(DEVICE(&s->uart[i]), "clk",
                              qdev_get_clock_out(DEVICE(&s->clocks),
                                                 "clk-peri"));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, uart_base[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           s->irq[uart_irq[i]]);
    }
}

static const Property rp2040_soc_properties[] = {
    DEFINE_PROP_LINK("memory", RP2040State, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_STRING("bootrom-file", RP2040State, bootrom_file),
};

static void rp2040_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rp2040_soc_realize;
    device_class_set_props(dc, rp2040_soc_properties);
}

static const TypeInfo rp2040_soc_info = {
    .name = TYPE_RP2040,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040State),
    .instance_init = rp2040_soc_init,
    .class_init = rp2040_soc_class_init,
};

static void rp2040_soc_types(void)
{
    type_register_static(&rp2040_soc_info);
}
type_init(rp2040_soc_types)
