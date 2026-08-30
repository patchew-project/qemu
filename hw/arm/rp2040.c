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
#include "system/address-spaces.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"

#define RP2040_UART0_BASE 0x40034000
#define RP2040_UART0_IRQ  20
#define RP2040_UART1_BASE 0x40038000
#define RP2040_UART1_IRQ  21
#define RP2040_IO_IRQ_BANK0 13
#define RP2040_SIO_IRQ_PROC0 15
#define RP2040_SIO_IRQ_PROC1 16
#define RP2040_PROC1          1

/*
 * Temporary boot ROM used until a faithful RP2040 boot ROM is requested.  It
 * uses SIO_CPUID to split core behavior: core 0 copies the 256-byte XIP
 * second-stage boot code into SRAM and branches to the SRAM copy; core 1 waits
 * in ROM for the Pico SDK launch FIFO sequence, echoes the received words,
 * installs VTOR/MSP, then branches to the received entry point.  Real RP2040
 * mask ROM performs more checks, but boot2 expects to run from SRAM while it
 * configures XIP.
 */
static const uint8_t rp2040_bootrom[] = {
    0x00, 0x20, 0x04, 0x20, /* initial SP: 0x20042000 */
    0x41, 0x00, 0x00, 0x00, /* reset handler: 0x00000041 */
    0xb3, 0x00, 0x00, 0x00, /* NMI handler: 0x000000b3 */
    0xb3, 0x00, 0x00, 0x00, /* HardFault handler: 0x000000b3 */
    0xb3, 0x00, 0x00, 0x00, /* reserved */
    0xb3, 0x00, 0x00, 0x00, /* reserved */
    0xb3, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0xb3, 0x00, 0x00, 0x00, /* SVC handler: 0x000000b3 */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0xb3, 0x00, 0x00, 0x00, /* PendSV handler: 0x000000b3 */
    0xb3, 0x00, 0x00, 0x00, /* SysTick handler: 0x000000b3 */
    0x26, 0x4c,             /* ldr r4, [pc, #152] ; SIO_BASE */
    0x20, 0x68,             /* ldr r0, [r4] ; SIO_CPUID */
    0x00, 0x28,             /* cmp r0, #0 */
    0x13, 0xd1,             /* bne core1 path */
    0x25, 0x48,             /* ldr r0, [pc, #148] ; 0x10000000 */
    0x26, 0x49,             /* ldr r1, [pc, #152] ; 0x20041f00 */
    0x40, 0x22,             /* movs r2, #64 */
    0x03, 0x68,             /* ldr r3, [r0] */
    0x0b, 0x60,             /* str r3, [r1] */
    0x04, 0x30,             /* adds r0, #4 */
    0x04, 0x31,             /* adds r1, #4 */
    0x01, 0x3a,             /* subs r2, #1 */
    0xf9, 0xd1,             /* bne copy loop */
    0x23, 0x4b,             /* ldr r3, [pc, #140] ; launch entry */
    0x9e, 0x46,             /* mov lr, r3 */
    0x23, 0x48,             /* ldr r0, [pc, #140] ; 0x20041f01 */
    0x00, 0x47,             /* bx r0 */
    0x23, 0x48,             /* ldr r0, [pc, #140] ; 0x10000100 */
    0x23, 0x49,             /* ldr r1, [pc, #140] ; VTOR */
    0x08, 0x60,             /* str r0, [r1] */
    0x06, 0xc8,             /* ldm r0!, {r1, r2} */
    0x81, 0xf3, 0x08, 0x88, /* msr msp, r1 */
    0x10, 0x47,             /* bx r2 */
    0x21, 0x4d,             /* ldr r5, [pc, #132] ; sequence */
    0x00, 0x26,             /* movs r6, #0 */
    0x00, 0xf0, 0x1e, 0xf8, /* bl fifo_pop */
    0x03, 0x2e,             /* cmp r6, #3 */
    0x07, 0xd2,             /* bhs echo */
    0xb1, 0x00,             /* lsls r1, r6, #2 */
    0x6a, 0x58,             /* ldr r2, [r5, r1] */
    0x90, 0x42,             /* cmp r0, r2 */
    0x0c, 0xd0,             /* beq echo */
    0x00, 0x26,             /* movs r6, #0 */
    0x00, 0xf0, 0x1c, 0xf8, /* bl fifo_push */
    0xf3, 0xe7,             /* b core1 loop */
    0x03, 0x2e,             /* cmp r6, #3 */
    0x01, 0xd1,             /* bne maybe SP */
    0x07, 0x46,             /* mov r7, r0 */
    0x04, 0xe0,             /* b echo */
    0x04, 0x2e,             /* cmp r6, #4 */
    0x01, 0xd1,             /* bne save PC */
    0x03, 0x46,             /* mov r3, r0 */
    0x00, 0xe0,             /* b echo */
    0x05, 0x46,             /* mov r5, r0 */
    0x00, 0xf0, 0x10, 0xf8, /* bl fifo_push */
    0x01, 0x36,             /* adds r6, #1 */
    0x06, 0x2e,             /* cmp r6, #6 */
    0xe5, 0xd1,             /* bne core1 loop */
    0x12, 0x49,             /* ldr r1, [pc, #72] ; VTOR */
    0x0f, 0x60,             /* str r7, [r1] */
    0x83, 0xf3, 0x08, 0x88, /* msr msp, r3 */
    0x28, 0x47,             /* bx r5 */
    0xfe, 0xe7,             /* hang */
    0x09, 0x4c,             /* ldr r4, [pc, #36] ; SIO_BASE */
    0x20, 0x6d,             /* ldr r0, [r4, #0x50] */
    0x01, 0x21,             /* movs r1, #1 */
    0x08, 0x42,             /* tst r0, r1 */
    0xfb, 0xd0,             /* beq fifo_pop */
    0xa0, 0x6d,             /* ldr r0, [r4, #0x58] */
    0x70, 0x47,             /* bx lr */
    0x06, 0x4c,             /* ldr r4, [pc, #24] ; SIO_BASE */
    0x21, 0x6d,             /* ldr r1, [r4, #0x50] */
    0x02, 0x22,             /* movs r2, #2 */
    0x11, 0x42,             /* tst r1, r2 */
    0xfb, 0xd0,             /* beq fifo_push */
    0x60, 0x65,             /* str r0, [r4, #0x54] */
    0x70, 0x47,             /* bx lr */
    0x00, 0x00, 0x00, 0x00, /* core1 sequence[0] */
    0x00, 0x00, 0x00, 0x00, /* core1 sequence[1] */
    0x01, 0x00, 0x00, 0x00, /* core1 sequence[2] */
    0x00, 0x00, 0x00, 0xd0, /* SIO_BASE */
    0x00, 0x00, 0x00, 0x10, /* boot2 source: 0x10000000 */
    0x00, 0x1f, 0x04, 0x20, /* boot2 SRAM copy: 0x20041f00 */
    0x63, 0x00, 0x00, 0x00, /* post-boot2 launch entry: 0x00000063 */
    0x01, 0x1f, 0x04, 0x20, /* boot2 SRAM entry: 0x20041f01 */
    0x00, 0x01, 0x00, 0x10, /* application vectors: 0x10000100 */
    0x08, 0xed, 0x00, 0xe0, /* VTOR: 0xe000ed08 */
    0xd0, 0x00, 0x00, 0x00, /* core1 sequence table */
};

static const struct {
    const char *name;
    hwaddr base;
    hwaddr size;
} rp2040_unimplemented[] = {
    { "rp2040.busctrl",  0x40030000, 0x4000 },
    { "rp2040.spi0",     0x4003c000, 0x4000 },
    { "rp2040.spi1",     0x40040000, 0x4000 },
    { "rp2040.i2c0",     0x40044000, 0x4000 },
    { "rp2040.i2c1",     0x40048000, 0x4000 },
    { "rp2040.adc",      0x4004c000, 0x4000 },
    { "rp2040.pwm",      0x40050000, 0x4000 },
    { "rp2040.rtc",      0x4005c000, 0x4000 },
    { "rp2040.dma",      0x50000000, 0x1000 },
    { "rp2040.usbctrl_dpram", 0x50100000, 0x10000 },
    { "rp2040.usbctrl_regs",  0x50110000, 0x10000 },
    { "rp2040.pio0",     0x50200000, 0x10000 },
    { "rp2040.pio1",     0x50300000, 0x10000 },
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

        bool irq_level = s->irq_level[0][i];

        qemu_set_irq(s->cpu_irq[0][i], irq_level && !route_to_nmi);
        nmi_level |= irq_level && route_to_nmi;
    }
    qemu_set_irq(s->nmi_irq[0], nmi_level);
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
    s->irq_level[0][irq] = level;
    rp2040_update_nmi(s);
}

static void rp2040_start_core1_async_work(CPUState *cs, run_on_cpu_data data)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    cpu_reset(cs);
    cpu->power_state = PSCI_ON;
    env->halt_reason = NOT_HALTED;
    arm_rebuild_hflags(env);
    cs->halted = 0;
    cpu_resume(cs);
}

static bool rp2040_core1_powered_off(RP2040State *s)
{
    ARMCPU *cpu = s->armv7m[RP2040_PROC1].cpu;

    return !cpu || cpu->power_state == PSCI_OFF;
}

static void rp2040_start_core1(RP2040State *s)
{
    if (!s->armv7m[RP2040_PROC1].cpu ||
        !rp2040_core1_powered_off(s)) {
        return;
    }

    async_run_on_cpu(CPU(s->armv7m[RP2040_PROC1].cpu),
                     rp2040_start_core1_async_work, RUN_ON_CPU_NULL);
}

static void rp2040_stop_core1_async_work(CPUState *cs, run_on_cpu_data data)
{
    ARMCPU *cpu = ARM_CPU(cs);

    cpu->power_state = PSCI_OFF;
    cpu->env.halt_reason = HALT_PSCI;
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
}

static void rp2040_stop_core1(RP2040State *s)
{
    if (!s->armv7m[RP2040_PROC1].cpu ||
        rp2040_core1_powered_off(s)) {
        return;
    }

    async_run_on_cpu(CPU(s->armv7m[RP2040_PROC1].cpu),
                     rp2040_stop_core1_async_work, RUN_ON_CPU_NULL);
}

static void rp2040_psm_update(void *opaque)
{
    RP2040State *s = opaque;
    bool proc1_forced_off = rp2040_psm_get_frce_off(&s->psm) &
                            RP2040_PSM_PROC1;

    if (proc1_forced_off) {
        rp2040_stop_core1(s);
    } else {
        rp2040_start_core1(s);
    }
}

static void rp2040_update_uart_pins(RP2040State *s)
{
    pl011_set_tx_connected(&s->uart[0],
                           !s->strict_uart_pins ||
                           s->uart0_tx_pin_enabled);
    pl011_set_rx_connected(&s->uart[0],
                           !s->strict_uart_pins ||
                           s->uart0_rx_pin_enabled);
    pl011_set_tx_connected(&s->uart[1],
                           !s->strict_uart_pins ||
                           s->uart1_tx_pin_enabled);
    pl011_set_rx_connected(&s->uart[1],
                           !s->strict_uart_pins ||
                           s->uart1_rx_pin_enabled);
}

static void rp2040_set_uart_pin(void *opaque, int pin, int level)
{
    RP2040State *s = opaque;

    switch (pin) {
    case 0:
        s->uart0_tx_pin_enabled = level;
        break;
    case 1:
        s->uart0_rx_pin_enabled = level;
        break;
    case 2:
        s->uart1_tx_pin_enabled = level;
        break;
    case 3:
        s->uart1_rx_pin_enabled = level;
        break;
    default:
        g_assert_not_reached();
    }

    rp2040_update_uart_pins(s);
}

static void rp2040_soc_init(Object *obj)
{
    RP2040State *s = RP2040(obj);
    int i;

    qdev_init_gpio_in_named(DEVICE(obj), rp2040_set_uart_pin,
                            "uart-pin", 4);

    for (i = 0; i < RP2040_NUM_CORES; i++) {
        g_autofree char *name = g_strdup_printf("proc%d", i);

        object_initialize_child(obj, name, &s->armv7m[i], TYPE_ARMV7M);
        qdev_prop_set_string(DEVICE(&s->armv7m[i]), "cpu-type",
                             ARM_CPU_TYPE_NAME("cortex-m0"));
        qdev_prop_set_uint32(DEVICE(&s->armv7m[i]), "num-irq",
                             RP2040_NUM_IRQS);
        qdev_prop_set_uint32(DEVICE(&s->armv7m[i]), "mpu-ns-regions", 8);
    }
    qdev_prop_set_bit(DEVICE(&s->armv7m[RP2040_PROC1]),
                      "start-powered-off", true);

    for (i = 0; i < ARRAY_SIZE(s->uart); i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i);
        g_autofree char *property = g_strdup_printf("serial%d", i);

        object_initialize_child(obj, name, &s->uart[i], TYPE_PL011);
        object_property_add_alias(obj, property, OBJECT(&s->uart[i]),
                                  "chardev");
    }

    object_initialize_child(obj, "xip", &s->xip, TYPE_RP2040_XIP);

    object_initialize_child(obj, "clocks", &s->clocks, TYPE_RP2040_CLOCKS);
    object_initialize_child(obj, "iobank0", &s->iobank0,
                            TYPE_RP2040_IOBANK0);
    object_initialize_child(obj, "ioqspi", &s->ioqspi,
                            TYPE_RP2040_IOQSPI);
    object_initialize_child(obj, "pads-bank0", &s->pads_bank0,
                            TYPE_RP2040_PADS_BANK0);
    object_initialize_child(obj, "pads-qspi", &s->pads_qspi,
                            TYPE_RP2040_PADS_QSPI);

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

    object_initialize_child(obj, "psm", &s->psm, TYPE_RP2040_PSM);
    object_initialize_child(obj, "resets", &s->resets, TYPE_RP2040_RESETS);
    object_initialize_child(obj, "syscfg", &s->syscfg, TYPE_RP2040_SYSCFG);
    object_initialize_child(obj, "sysinfo", &s->sysinfo, TYPE_RP2040_SYSINFO);
    object_initialize_child(obj, "rosc", &s->rosc, TYPE_RP2040_ROSC);
    object_initialize_child(obj, "sio", &s->sio, TYPE_RP2040_SIO);
    object_initialize_child(obj, "tbman", &s->tbman, TYPE_RP2040_TBMAN);
    object_initialize_child(obj, "timer", &s->timer, TYPE_RP2040_TIMER);
    object_initialize_child(obj, "vreg", &s->vreg, TYPE_RP2040_VREG);
    object_initialize_child(obj, "watchdog", &s->watchdog,
                            TYPE_RP2040_WATCHDOG);
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

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xip), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xip), 0, RP2040_XIP_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xip), 1, RP2040_XIP_CTRL_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xip), 2, RP2040_XIP_SSI_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xip), 3, RP2040_XIP_AUX_BASE);
    memory_region_add_subregion(get_system_memory(), RP2040_XIP_NOALLOC_BASE,
                                &s->xip.xip_noalloc);
    memory_region_add_subregion(get_system_memory(), RP2040_XIP_NOCACHE_BASE,
                                &s->xip.xip_nocache);
    memory_region_add_subregion(get_system_memory(),
                                RP2040_XIP_NOCACHE_NOALLOC_BASE,
                                &s->xip.xip_nocache_noalloc);

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

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->psm), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->psm), 0, RP2040_PSM_BASE);
    rp2040_psm_set_update_callback(&s->psm, rp2040_psm_update, s);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->resets), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->resets), 0, RP2040_RESETS_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sio), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sio), 0, RP2040_SIO_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sio), 0,
                       s->irq[RP2040_SIO_IRQ_PROC0]);

    for (i = 0; i < RP2040_NUM_CORES; i++) {
        int irq;
        g_autofree char *name = g_strdup_printf("rp2040.proc%d-memory", i);

        qdev_connect_clock_in(DEVICE(&s->armv7m[i]), "cpuclk", s->sysclk);
        memory_region_init_alias(&s->cpu_memory[i], OBJECT(dev), name,
                                 s->board_memory, 0,
                                 memory_region_size(s->board_memory));
        object_property_set_link(OBJECT(&s->armv7m[i]), "memory",
                                 OBJECT(&s->cpu_memory[i]), &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m[i]), errp)) {
            return;
        }
        for (irq = 0; irq < RP2040_NUM_IRQS; irq++) {
            s->cpu_irq[i][irq] = qdev_get_gpio_in(DEVICE(&s->armv7m[i]),
                                                  irq);
        }
        s->nmi_irq[i] = qdev_get_gpio_in_named(DEVICE(&s->armv7m[i]),
                                               "NMI", 0);
    }
    rp2040_update_nmi(s);

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

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->iobank0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->iobank0), 0, RP2040_IOBANK0_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->iobank0), 0,
                       s->irq[RP2040_IO_IRQ_BANK0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->iobank0), 1,
                       s->cpu_irq[RP2040_PROC1][RP2040_IO_IRQ_BANK0]);
    qdev_connect_gpio_out_named(DEVICE(&s->iobank0), "uart0-pin", 0,
                                qdev_get_gpio_in_named(dev, "uart-pin", 0));
    qdev_connect_gpio_out_named(DEVICE(&s->iobank0), "uart0-pin", 1,
                                qdev_get_gpio_in_named(dev, "uart-pin", 1));
    qdev_connect_gpio_out_named(DEVICE(&s->iobank0), "uart1-pin", 0,
                                qdev_get_gpio_in_named(dev, "uart-pin", 2));
    qdev_connect_gpio_out_named(DEVICE(&s->iobank0), "uart1-pin", 1,
                                qdev_get_gpio_in_named(dev, "uart-pin", 3));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sio), 1,
                       s->cpu_irq[RP2040_PROC1][RP2040_SIO_IRQ_PROC1]);

    object_property_set_link(OBJECT(&s->ioqspi), "xip", OBJECT(&s->xip),
                             &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ioqspi), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ioqspi), 0, RP2040_IOQSPI_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rosc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rosc), 0, RP2040_ROSC_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tbman), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tbman), 0, RP2040_TBMAN_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->timer), 0, RP2040_TIMER_BASE);
    for (i = 0; i < RP2040_TIMER_NUM_ALARMS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer), i, s->irq[i]);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->vreg), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->vreg), 0, RP2040_VREG_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pads_bank0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pads_bank0), 0,
                    RP2040_PADS_BANK0_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pads_qspi), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pads_qspi), 0,
                    RP2040_PADS_QSPI_BASE);

    qdev_connect_clock_in(DEVICE(&s->watchdog), "clk-ref",
                          qdev_get_clock_out(DEVICE(&s->clocks), "clk-ref"));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->watchdog), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->watchdog), 0,
                    RP2040_WATCHDOG_BASE);

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
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 1,
                        uart_base[i] + 0x1000);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           s->irq[uart_irq[i]]);
    }
    rp2040_update_uart_pins(s);
}

static const Property rp2040_soc_properties[] = {
    DEFINE_PROP_LINK("memory", RP2040State, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_STRING("bootrom-file", RP2040State, bootrom_file),
    DEFINE_PROP_BOOL("strict-uart-pins", RP2040State, strict_uart_pins, true),
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
