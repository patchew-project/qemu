/*
 * Microchip PIC32MK GPK/MCM with CAN FD — board emulation
 * Datasheet: DS60001519E
 *
 * Phase 2: CPU core, EVIC, UART×6, Timers×9, GPIO A-G, SPI×6, I2C×4, DMA×8
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/mips/mips.h"
#include "hw/mips/pic32mk.h"
#include "hw/mips/pic32mk_evic.h"
#include "hw/mips/pic32mk_usb.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "net/can_emu.h"
#include "chardev/char.h"
#include "cpu.h"

/* Device type strings from our peripheral files */
#define TYPE_PIC32MK_UART   "pic32mk-uart"
#define TYPE_PIC32MK_TIMER  "pic32mk-timer"
#define TYPE_PIC32MK_GPIO   "pic32mk-gpio"
#define TYPE_PIC32MK_SPI    "pic32mk-spi"
#define TYPE_PIC32MK_I2C    "pic32mk-i2c"
#define TYPE_PIC32MK_DMA    "pic32mk-dma"
#define TYPE_PIC32MK_CANFD  "pic32mk-canfd"
#define TYPE_PIC32MK_USB    "pic32mk-usb"
#define TYPE_PIC32MK_WDT    "pic32mk-wdt"
#define TYPE_PIC32MK_CRU    "pic32mk-cru"
#define TYPE_PIC32MK_CFG    "pic32mk-cfg"
#define TYPE_PIC32MK_ADCHS  "pic32mk-adchs"
#define TYPE_PIC32MK_NVM    "pic32mk-nvm"
#define TYPE_PIC32MK_DATAEE "pic32mk-dataee"
#define TYPE_PIC32MK_OC     "pic32mk-oc"
#define TYPE_PIC32MK_IC     "pic32mk-ic"

/*
 * Board state.
 */
typedef struct {
    MIPSCPU        *cpu;
    MemoryRegion    boot_rom;
    MemoryRegion    pflash;
    MemoryRegion    bflash1;
    MemoryRegion    bflash2;
    MemoryRegion    sfr;
    MemoryRegion    sfr_unimpl;
    MemoryRegion    pps_stub;   /* PPS input/output regs 0xBF801400-0xBF8017FF */

    DeviceState    *evic;
} PIC32MKState;

/*
 * SFR catch-all stub — logs every unimplemented register access.
 * -----------------------------------------------------------------------
 */

static uint64_t sfr_unimpl_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP,
                  "pic32mk: unimplemented SFR read  @ 0x%08" HWADDR_PRIx
                  " (size %u)\n",
                  (hwaddr)(PIC32MK_SFR_BASE + addr), size);
    return 0;
}

static void sfr_unimpl_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    qemu_log_mask(LOG_UNIMP,
                  "pic32mk: unimplemented SFR write @ 0x%08" HWADDR_PRIx
                  " = 0x%08" PRIx64 " (size %u)\n",
                  (hwaddr)(PIC32MK_SFR_BASE + addr), val, size);
}

static const MemoryRegionOps sfr_unimpl_ops = {
    .read       = sfr_unimpl_read,
    .write      = sfr_unimpl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/*
 * Silent stub — used for PPS and other write-only config registers that
 * need no emulation but should not generate unimplemented warnings.
 */
static uint64_t sfr_ignore_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}
static void sfr_ignore_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    /* silently accept */
}
static const MemoryRegionOps sfr_ignore_ops = {
    .read       = sfr_ignore_read,
    .write      = sfr_ignore_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};



/*
 * Helper: create a peripheral SysBusDevice, map its MMIO into the SFR
 * window at the given offset (overriding the catch-all at priority 1).
 * Returns the DeviceState for further property/IRQ wiring.
 * -----------------------------------------------------------------------
 */

static DeviceState *sfr_device_create(MemoryRegion *sfr, const char *type,
                                      hwaddr sfr_offset, Error **errp)
{
    DeviceState *dev = qdev_new(type);
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), errp)) {
        return NULL;
    }

    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion_overlap(sfr, sfr_offset, mr, 1);
    return dev;
}

/*
 * Memory map initialisation
 * -----------------------------------------------------------------------
 */

static void pic32mk_memory_init(PIC32MKState *s, MachineState *machine)
{
    MemoryRegion *sys_mem = get_system_memory();

    /* 256 KB SRAM */
    memory_region_add_subregion(sys_mem, PIC32MK_RAM_BASE, machine->ram);

    /* 1 MB Program Flash — RAM-backed so NVM controller can write */
    memory_region_init_ram(&s->pflash, NULL, "pic32mk.pflash",
                           PIC32MK_PFLASH_SIZE, &error_fatal);
    memory_region_add_subregion(sys_mem, PIC32MK_PFLASH_BASE, &s->pflash);

    /* Boot Flash 1 — firmware loaded here via -bios */
    memory_region_init_rom(&s->bflash1, NULL, "pic32mk.bflash1",
                           PIC32MK_BFLASH1_SIZE, &error_fatal);
    memory_region_add_subregion(sys_mem, PIC32MK_BFLASH1_BASE, &s->bflash1);

    /* Boot Flash 2 */
    memory_region_init_rom(&s->bflash2, NULL, "pic32mk.bflash2",
                           PIC32MK_BFLASH2_SIZE, &error_fatal);
    memory_region_add_subregion(sys_mem, PIC32MK_BFLASH2_BASE, &s->bflash2);

    /*
     * Boot vector ROM — physical 0x1FC00000 to 0x1FC3FFFF.
     * Contains a two-instruction trampoline:
     *   j  0xBFC40000   (Boot Flash 1, KSEG1)
     *   nop             (branch delay slot)
     *
     * j-encoding when PC = 0xBFC00000:
     *   instr_index = (0xBFC40000 >> 2) & 0x3FFFFFF = 0x03F10000
     *   word = (2 << 26) | 0x03F10000 = 0x0BF10000
     */
    memory_region_init_rom(&s->boot_rom, NULL, "pic32mk.boot-rom",
                           PIC32MK_BOOTVEC_SIZE, &error_fatal);
    memory_region_add_subregion(sys_mem, PIC32MK_BOOTVEC_BASE, &s->boot_rom);
    {
        uint32_t *p = memory_region_get_ram_ptr(&s->boot_rom);
        p[0] = 0x0BF10000;  /* j 0xBFC40000 */
        p[1] = 0x00000000;  /* nop (delay slot) */
    }

    /* SFR window: 1 MB container */
    memory_region_init(&s->sfr, NULL, "pic32mk.sfr", PIC32MK_SFR_SIZE);
    memory_region_add_subregion(sys_mem, PIC32MK_SFR_BASE, &s->sfr);

    /* Catch-all at priority 0 */
    memory_region_init_io(&s->sfr_unimpl, NULL, &sfr_unimpl_ops, s,
                          "pic32mk.sfr-unimpl", PIC32MK_SFR_SIZE);
    memory_region_add_subregion_overlap(&s->sfr, 0, &s->sfr_unimpl, 0);

    /* PPS (Peripheral Pin Select) 0xBF801400–0xBF8017FF — silent stub */
    memory_region_init_io(&s->pps_stub, NULL, &sfr_ignore_ops, NULL,
                          "pic32mk.pps", PIC32MK_PPS_SIZE);
    memory_region_add_subregion_overlap(&s->sfr, PIC32MK_PPS_OFFSET,
                                        &s->pps_stub, 1);
}

/*
 * CPU initialisation
 * -----------------------------------------------------------------------
 */

static void pic32mk_cpu_init(PIC32MKState *s, MachineState *machine)
{
    Clock *cpuclk;

    cpuclk = clock_new(OBJECT(machine), "cpu-refclk");
    clock_set_hz(cpuclk, PIC32MK_CPU_HZ);

    s->cpu = mips_cpu_create_with_clock(machine->cpu_type, cpuclk, false);
    if (!s->cpu) {
        error_report("pic32mk: failed to create CPU '%s'", machine->cpu_type);
        exit(1);
    }

    /*
     * Allocate the 8 CPU interrupt lines (env->irq[0..7]).
     * Must be called before wiring EVIC to CPU pins.
     */
    cpu_mips_irq_init_cpu(s->cpu);
    cpu_mips_clock_init(s->cpu);
}

/*
 * EVIC initialisation — create device, map MMIO, wire CPU pins
 * -----------------------------------------------------------------------
 */

static void pic32mk_evic_init(PIC32MKState *s)
{
    DeviceState *evic = qdev_new(TYPE_PIC32MK_EVIC);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(evic), &error_fatal);

    MemoryRegion *evic_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(evic), 0);
    memory_region_add_subregion_overlap(&s->sfr, PIC32MK_EVIC_OFFSET,
                                        evic_mr, 1);
    s->evic = evic;

    /*
     * Wire EVIC output pins to CPU interrupt inputs.
     * The EVIC device stores these handles and calls qemu_set_irq()
     * when a pending+enabled interrupt is found at a given priority level.
     */
    PIC32MKEVICState *evic_s = PIC32MK_EVIC(evic);
    CPUMIPSState *env = &s->cpu->env;
    for (int i = 0; i < 8; i++) {
        evic_s->cpu_irq[i] = env->irq[i];
    }

    /*
     * Intercept the CP0 Core Timer's interrupt output (normally fires on
     * env->irq[7]) and redirect it through EVIC source 0 (CT = Core Timer).
     *
     * This makes CP0 timer interrupts subject to IEC0[0] (CTIE) and
     * IPC0 priority configuration, as on real hardware.
     *
     * cp0_timer.c calls: qemu_irq_raise(env->irq[IPTI & 7])
     * With IPTI=7 (M14K default, CP0_IntCtl = 0xe0000000), that is irq[7].
     * We replace irq[7] with the EVIC's irq_in[PIC32MK_IRQ_CT].
     */
    env->irq[7] = qdev_get_gpio_in(evic, PIC32MK_IRQ_CT);

    /*
     * Intercept CP0 Software Interrupts 0 and 1 (env->irq[0..1]) and
     * redirect them through the EVIC (sources CS0=1, CS1=2).
     *
     * On real PIC32MK hardware, writing Cause.IP0 triggers EVIC source
     * "Core Software Interrupt 0" (CS0) at whatever priority is configured
     * in IPC0.  The EVIC then delivers the interrupt through the normal
     * priority comparison, which prevents same-priority nesting.
     *
     * In QEMU VEIC mode the pending-vs-status comparison is
     *   (Cause & 0xFF00) > (Status & 0xFF00)
     * If IP0 (bit 8) is left in Cause while the tick ISR sets IPL=1
     * (bit 10), 0x0500 > 0x0400 causes an unwanted nested interrupt.
     *
     * The custom handler below:
     *   1) routes SW0/SW1 through the EVIC input lines, and
     *   2) clears the direct Cause.IP0/IP1 bit so the VEIC comparison
     *      only sees the EVIC-asserted priority pin (bit 10+).
     */
    env->irq[0] = qdev_get_gpio_in(evic, PIC32MK_IRQ_CS0);
    env->irq[1] = qdev_get_gpio_in(evic, PIC32MK_IRQ_CS1);

    /*
     * Store a reference to the CPU env in the EVIC state so the
     * evic_set_irq handler can clear the direct Cause.IP bits for
     * software interrupt sources routed through the EVIC.
     */
    evic_s->cpu = s->cpu;
}

/*
 * Peripheral initialisation helpers
 * -----------------------------------------------------------------------
 */

/*
 * Create a UART instance, attach a chardev, map into the SFR window,
 * and connect its RX/TX/error IRQ outputs to the EVIC input lines.
 */
static void pic32mk_uart_create(PIC32MKState *s, int index,
                                hwaddr sfr_offset,
                                int irq_rx, int irq_tx, int irq_err,
                                Chardev *chr)
{
    DeviceState *dev = qdev_new(TYPE_PIC32MK_UART);
    if (chr) {
        qdev_prop_set_chr(dev, "chardev", chr);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion_overlap(&s->sfr, sfr_offset, mr, 1);

    /* Connect UART IRQ outputs → EVIC inputs */
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(s->evic, irq_rx));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1,
                       qdev_get_gpio_in(s->evic, irq_tx));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2,
                       qdev_get_gpio_in(s->evic, irq_err));
}

/*
 * Create a Timer instance, map into the SFR window, connect IRQ to EVIC.
 */
static void pic32mk_timer_create(PIC32MKState *s,
                                 hwaddr sfr_offset, int irq_src, bool type_a)
{
    DeviceState *dev = qdev_new(TYPE_PIC32MK_TIMER);
    qdev_prop_set_bit(dev, "type-a", type_a);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion_overlap(&s->sfr, sfr_offset,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), 1);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(s->evic, irq_src));
}

static void pic32mk_oc_create(PIC32MKState *s, int index,
                              hwaddr sfr_offset, int irq_src)
{
    DeviceState *dev = qdev_new(TYPE_PIC32MK_OC);
    qdev_prop_set_uint8(dev, "index", (uint8_t)index);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion_overlap(&s->sfr, sfr_offset,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), 1);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(s->evic, irq_src));
    /* Optional chardev for waveform event streaming */
    Chardev *chr = qemu_chr_find("oc-events");
    if (chr) {
        pic32mk_oc_set_chardev(dev, chr);
    }
}

/*
 * pic32mk_ic_create — instantiate one IC peripheral and map it into the SFR
 * window.  irq_cap = capture IRQ index, irq_err = error IRQ index (Table 8-3).
 * The optional chardev "ic-events" is used to inject capture events from the
 * host; must be set before sysbus_realize_and_unref.
 */
static void pic32mk_ic_create(PIC32MKState *s, int index,
                              hwaddr sfr_offset, int irq_cap, int irq_err)
{
    DeviceState *dev = qdev_new(TYPE_PIC32MK_IC);
    qdev_prop_set_uint8(dev, "index", (uint8_t)index);
    /*
     * Only IC1 owns the "ic-events" chardev; it dispatches to all instances
     * via a global routing table registered during realize.
      */
    if (index == 1) {
        Chardev *chr = qemu_chr_find("ic-events");
        if (chr) {
            qdev_prop_set_chr(dev, "chardev", chr);
        }
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion_overlap(&s->sfr, sfr_offset,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), 1);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(s->evic, irq_cap));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1,
                       qdev_get_gpio_in(s->evic, irq_err));
}

static const char * const pic32mk_gpio_port_names[PIC32MK_GPIO_NPORTS] = {
    "gpio-portA", "gpio-portB", "gpio-portC", "gpio-portD",
    "gpio-portE", "gpio-portF", "gpio-portG",
};

/*
 * Create a GPIO port instance, map into the SFR window.
 * The CN interrupt output (sysbus IRQ 0) is wired to the EVIC.
 * The device is registered as a named child of the machine object so that
 * QOM paths are predictable: /machine/gpio-portA … /machine/gpio-portG.
 */
static void pic32mk_gpio_create(PIC32MKState *s, MachineState *machine,
                                int port_idx, hwaddr sfr_offset, int cn_irq,
                                Chardev *gpio_chr)
{
    /*
     * Create the device first, register it as a named child of the machine
     * BEFORE calling sysbus_realize_and_unref().  If the object already has
     * a parent when device_realize() runs it will NOT be placed under the
     * anonymous machine/unattached container, so object_property_add_child()
     * won't hit the "!child->parent" assertion.
     */
    DeviceState *dev = qdev_new(TYPE_PIC32MK_GPIO);
    qdev_prop_set_uint8(dev, "port-index", (uint8_t)port_idx);
    object_property_add_child(OBJECT(machine),
                              pic32mk_gpio_port_names[port_idx], OBJECT(dev));
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal)) {
        return;
    }

    /* Shared chardev for GPIO event streaming (all ports write to same chardev) */
    if (gpio_chr) {
        pic32mk_gpio_set_chardev(dev, gpio_chr);
    }

    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion_overlap(&s->sfr, sfr_offset, mr, 1);

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(s->evic, cn_irq));
}

/*
 * Create a SPI instance, map into the SFR window.
 */
static void pic32mk_spi_create(PIC32MKState *s, int index, hwaddr sfr_offset,
                               int irq_rx, int irq_tx, int irq_err)
{
    DeviceState *dev = qdev_new(TYPE_PIC32MK_SPI);
    char chr_name[16];

    qdev_prop_set_uint8(dev, "spi-index", (uint8_t)index);
    snprintf(chr_name, sizeof(chr_name), "spi%d", index);
    Chardev *chr = qemu_chr_find(chr_name);
    if (chr) {
        qdev_prop_set_chr(dev, "chardev", chr);
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion_overlap(&s->sfr, sfr_offset, mr, 1);

    if (irq_rx >= 0) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                           qdev_get_gpio_in(s->evic, irq_rx));
    }
    if (irq_tx >= 0) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1,
                           qdev_get_gpio_in(s->evic, irq_tx));
    }
    if (irq_err >= 0) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2,
                           qdev_get_gpio_in(s->evic, irq_err));
    }
}

/*
 * Create an I2C instance, map into the SFR window.
 */
static void pic32mk_i2c_create(PIC32MKState *s, hwaddr sfr_offset)
{
    sfr_device_create(&s->sfr, TYPE_PIC32MK_I2C, sfr_offset, &error_fatal);
}

/*
 * pic32mk_find_canbus — look up a can-bus object created with
 *   -object can-bus,id=canbus<idx>
 * Returns NULL if no such object exists (CAN instance runs standalone).
 */
static CanBusState *pic32mk_find_canbus(int idx)
{
    char path[32];
    snprintf(path, sizeof(path), "/objects/canbus%d", idx);
    Object *obj = object_resolve_path_type(path, TYPE_CAN_BUS, NULL);
    return obj ? CAN_BUS(obj) : NULL;
}

/*
 * Create a CAN FD instance:
 *  - SFR region (mmio 0) mapped into the SFR window at sfr_offset
 *  - Message RAM (mmio 1) mapped into system memory at msgram_phys_base
 *  - Single IRQ wired to EVIC input irq_src
 *  - canbus: optional virtual bus (NULL = standalone/loopback only)
 */
static void pic32mk_canfd_create(PIC32MKState *s,
                                 hwaddr sfr_offset,
                                 hwaddr msgram_phys_base,
                                 int irq_src,
                                 CanBusState *canbus,
                                 uint32_t instance_id)
{
    DeviceState *dev = qdev_new(TYPE_PIC32MK_CANFD);
    qdev_prop_set_uint32(dev, "msg-ram-base", (uint32_t)msgram_phys_base);
    qdev_prop_set_uint32(dev, "instance-id", instance_id);
    if (canbus) {
        object_property_set_link(OBJECT(dev), "canbus",
                                 OBJECT(canbus), &error_fatal);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* SFR block overrides the catch-all at priority 1 */
    memory_region_add_subregion_overlap(&s->sfr, sfr_offset,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), 1);

    /* Message RAM mapped into the flat system address space */
    memory_region_add_subregion(get_system_memory(), msgram_phys_base,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1));

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(s->evic, irq_src));
}

/*
 * Create a USB OTG instance, map SFR into the SFR window, connect IRQ.
 */
static void pic32mk_usb_create(PIC32MKState *s,
                               hwaddr sfr_offset, int irq_src,
                               const char *chardev_id)
{
    DeviceState *dev = qdev_new(TYPE_PIC32MK_USB);
    if (chardev_id) {
        Chardev *chr = qemu_chr_find(chardev_id);
        if (chr) {
            qdev_prop_set_chr(dev, "chardev", chr);
        }
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion_overlap(&s->sfr, sfr_offset,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), 1);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(s->evic, irq_src));
}

/*
 * Firmware loading
 * -----------------------------------------------------------------------
 */

static void pic32mk_load_firmware(MachineState *machine)
{
    if (!machine->firmware) {
        return;
    }

    char *filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
    if (!filename) {
        error_report("pic32mk: could not find firmware '%s'",
                     machine->firmware);
        exit(1);
    }

    ssize_t bios_size = load_image_targphys(filename,
                                            PIC32MK_BFLASH1_BASE,
                                            PIC32MK_BFLASH1_SIZE,
                                            NULL);
    g_free(filename);

    if (bios_size < 0) {
        error_report("pic32mk: could not load firmware '%s'",
                     machine->firmware);
        exit(1);
    }
}

/*
 * Machine entry point
 * -----------------------------------------------------------------------
 */

static void pic32mk_machine_init(MachineState *machine)
{
    PIC32MKState *s = g_new0(PIC32MKState, 1);

    pic32mk_cpu_init(s, machine);
    pic32mk_memory_init(s, machine);

    /* EVIC — must come before peripherals so we can wire IRQs */
    pic32mk_evic_init(s);

    /* DMA — mapped into the EVIC's 4 KB page (DMA_OFFSET = EVIC+0x1000) */
    {
        DeviceState *dma = qdev_new(TYPE_PIC32MK_DMA);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dma), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dma), 0);
        memory_region_add_subregion_overlap(&s->sfr, PIC32MK_DMA_OFFSET, mr, 1);
        for (int i = 0; i < PIC32MK_DMA_NCHANNELS; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(dma), i,
                               qdev_get_gpio_in(s->evic,
                                                PIC32MK_IRQ_DMA0 + i));
        }
    }

    /* UART1 on serial_hd(0), UART2–6 without chardev (stub only) */
    pic32mk_uart_create(s, 1, PIC32MK_UART1_OFFSET,
                        PIC32MK_IRQ_U1RX, PIC32MK_IRQ_U1TX, PIC32MK_IRQ_U1E,
                        serial_hd(0));
    pic32mk_uart_create(s, 2, PIC32MK_UART2_OFFSET,
                        PIC32MK_IRQ_U2RX, PIC32MK_IRQ_U2TX, PIC32MK_IRQ_U2E,
                        serial_hd(1));
    pic32mk_uart_create(s, 3, PIC32MK_UART3_OFFSET,
                        PIC32MK_IRQ_U3RX, PIC32MK_IRQ_U3TX, PIC32MK_IRQ_U3E,
                        serial_hd(2));
    pic32mk_uart_create(s, 4, PIC32MK_UART4_OFFSET,
                        PIC32MK_IRQ_U4RX, PIC32MK_IRQ_U4TX, PIC32MK_IRQ_U4E,
                        serial_hd(3));
    pic32mk_uart_create(s, 5, PIC32MK_UART5_OFFSET,
                        PIC32MK_IRQ_U5RX, PIC32MK_IRQ_U5TX, PIC32MK_IRQ_U5E,
                        serial_hd(4));
    pic32mk_uart_create(s, 6, PIC32MK_UART6_OFFSET,
                        PIC32MK_IRQ_U6RX, PIC32MK_IRQ_U6TX, PIC32MK_IRQ_U6E,
                        serial_hd(5));

    /* Timers 1–9: Timer1 is Type A (2-bit TCKPS {1,8,64,256}); 2–9 are Type B/C */
    pic32mk_timer_create(s, PIC32MK_T1_OFFSET, PIC32MK_IRQ_T1, true);
    pic32mk_timer_create(s, PIC32MK_T2_OFFSET, PIC32MK_IRQ_T2, false);
    pic32mk_timer_create(s, PIC32MK_T3_OFFSET, PIC32MK_IRQ_T3, false);
    pic32mk_timer_create(s, PIC32MK_T4_OFFSET, PIC32MK_IRQ_T4, false);
    pic32mk_timer_create(s, PIC32MK_T5_OFFSET, PIC32MK_IRQ_T5, false);
    pic32mk_timer_create(s, PIC32MK_T6_OFFSET, PIC32MK_IRQ_T6, false);
    pic32mk_timer_create(s, PIC32MK_T7_OFFSET, PIC32MK_IRQ_T7, false);
    pic32mk_timer_create(s, PIC32MK_T8_OFFSET, PIC32MK_IRQ_T8, false);
    pic32mk_timer_create(s, PIC32MK_T9_OFFSET, PIC32MK_IRQ_T9, false);

    /* Output Compare OC1–OC16 (diagnostic emulation) */
    pic32mk_oc_create(s, 1,  PIC32MK_OC1_OFFSET,  PIC32MK_IRQ_OC1);
    pic32mk_oc_create(s, 2,  PIC32MK_OC2_OFFSET,  PIC32MK_IRQ_OC2);
    pic32mk_oc_create(s, 3,  PIC32MK_OC3_OFFSET,  PIC32MK_IRQ_OC3);
    pic32mk_oc_create(s, 4,  PIC32MK_OC4_OFFSET,  PIC32MK_IRQ_OC4);
    pic32mk_oc_create(s, 5,  PIC32MK_OC5_OFFSET,  PIC32MK_IRQ_OC5);
    pic32mk_oc_create(s, 6,  PIC32MK_OC6_OFFSET,  PIC32MK_IRQ_OC6);
    pic32mk_oc_create(s, 7,  PIC32MK_OC7_OFFSET,  PIC32MK_IRQ_OC7);
    pic32mk_oc_create(s, 8,  PIC32MK_OC8_OFFSET,  PIC32MK_IRQ_OC8);
    pic32mk_oc_create(s, 9,  PIC32MK_OC9_OFFSET,  PIC32MK_IRQ_OC9);
    pic32mk_oc_create(s, 10, PIC32MK_OC10_OFFSET, PIC32MK_IRQ_OC10);
    pic32mk_oc_create(s, 11, PIC32MK_OC11_OFFSET, PIC32MK_IRQ_OC11);
    pic32mk_oc_create(s, 12, PIC32MK_OC12_OFFSET, PIC32MK_IRQ_OC12);
    pic32mk_oc_create(s, 13, PIC32MK_OC13_OFFSET, PIC32MK_IRQ_OC13);
    pic32mk_oc_create(s, 14, PIC32MK_OC14_OFFSET, PIC32MK_IRQ_OC14);
    pic32mk_oc_create(s, 15, PIC32MK_OC15_OFFSET, PIC32MK_IRQ_OC15);
    pic32mk_oc_create(s, 16, PIC32MK_OC16_OFFSET, PIC32MK_IRQ_OC16);

    /* Input Capture IC1–IC16 (full register model with FIFO) */
    pic32mk_ic_create(s, 1,  PIC32MK_IC1_OFFSET,  PIC32MK_IRQ_IC1,  PIC32MK_IRQ_IC1E);
    pic32mk_ic_create(s, 2,  PIC32MK_IC2_OFFSET,  PIC32MK_IRQ_IC2,  PIC32MK_IRQ_IC2E);
    pic32mk_ic_create(s, 3,  PIC32MK_IC3_OFFSET,  PIC32MK_IRQ_IC3,  PIC32MK_IRQ_IC3E);
    pic32mk_ic_create(s, 4,  PIC32MK_IC4_OFFSET,  PIC32MK_IRQ_IC4,  PIC32MK_IRQ_IC4E);
    pic32mk_ic_create(s, 5,  PIC32MK_IC5_OFFSET,  PIC32MK_IRQ_IC5,  PIC32MK_IRQ_IC5E);
    pic32mk_ic_create(s, 6,  PIC32MK_IC6_OFFSET,  PIC32MK_IRQ_IC6,  PIC32MK_IRQ_IC6E);
    pic32mk_ic_create(s, 7,  PIC32MK_IC7_OFFSET,  PIC32MK_IRQ_IC7,  PIC32MK_IRQ_IC7E);
    pic32mk_ic_create(s, 8,  PIC32MK_IC8_OFFSET,  PIC32MK_IRQ_IC8,  PIC32MK_IRQ_IC8E);
    pic32mk_ic_create(s, 9,  PIC32MK_IC9_OFFSET,  PIC32MK_IRQ_IC9,  PIC32MK_IRQ_IC9E);
    pic32mk_ic_create(s, 10, PIC32MK_IC10_OFFSET, PIC32MK_IRQ_IC10, PIC32MK_IRQ_IC10E);
    pic32mk_ic_create(s, 11, PIC32MK_IC11_OFFSET, PIC32MK_IRQ_IC11, PIC32MK_IRQ_IC11E);
    pic32mk_ic_create(s, 12, PIC32MK_IC12_OFFSET, PIC32MK_IRQ_IC12, PIC32MK_IRQ_IC12E);
    pic32mk_ic_create(s, 13, PIC32MK_IC13_OFFSET, PIC32MK_IRQ_IC13, PIC32MK_IRQ_IC13E);
    pic32mk_ic_create(s, 14, PIC32MK_IC14_OFFSET, PIC32MK_IRQ_IC14, PIC32MK_IRQ_IC14E);
    pic32mk_ic_create(s, 15, PIC32MK_IC15_OFFSET, PIC32MK_IRQ_IC15, PIC32MK_IRQ_IC15E);
    pic32mk_ic_create(s, 16, PIC32MK_IC16_OFFSET, PIC32MK_IRQ_IC16, PIC32MK_IRQ_IC16E);

    /* GPIO ports A–G: CN interrupt vectors 44–50 (_CHANGE_NOTICE_x_VECTOR) */
    static const int cn_irqs[PIC32MK_GPIO_NPORTS] = {
        PIC32MK_IRQ_CNA, PIC32MK_IRQ_CNB, PIC32MK_IRQ_CNC, PIC32MK_IRQ_CND,
        PIC32MK_IRQ_CNE, PIC32MK_IRQ_CNF, PIC32MK_IRQ_CNG,
    };
    /* Optional: look up shared chardev "gpio-events" for GUI event streaming */
    Chardev *gpio_chr = qemu_chr_find("gpio-events");
    for (int port = 0; port < PIC32MK_GPIO_NPORTS; port++) {
        pic32mk_gpio_create(s, machine, port,
                            PIC32MK_GPIO_OFFSET
                            + (hwaddr)port * PIC32MK_GPIO_PORT_SIZE,
                            cn_irqs[port], gpio_chr);
    }

    /* SPI 1–6 */
    pic32mk_spi_create(s, 1, PIC32MK_SPI1_OFFSET,
                       PIC32MK_IRQ_SPI1_RX,
                       PIC32MK_IRQ_SPI1_TX,
                       PIC32MK_IRQ_SPI1_FAULT);
    pic32mk_spi_create(s, 2, PIC32MK_SPI2_OFFSET,
                       PIC32MK_IRQ_SPI2_RX,
                       PIC32MK_IRQ_SPI2_TX,
                       PIC32MK_IRQ_SPI2_FAULT);
    pic32mk_spi_create(s, 3, PIC32MK_SPI3_OFFSET,
                       PIC32MK_IRQ_SPI3_RX,
                       PIC32MK_IRQ_SPI3_TX,
                       PIC32MK_IRQ_SPI3_FAULT);
    pic32mk_spi_create(s, 4, PIC32MK_SPI4_OFFSET,
                       PIC32MK_IRQ_SPI4_RX,
                       PIC32MK_IRQ_SPI4_TX,
                       PIC32MK_IRQ_SPI4_FAULT);
    pic32mk_spi_create(s, 5, PIC32MK_SPI5_OFFSET,
                       PIC32MK_IRQ_SPI5_RX,
                       PIC32MK_IRQ_SPI5_TX,
                       PIC32MK_IRQ_SPI5_FAULT);
    pic32mk_spi_create(s, 6, PIC32MK_SPI6_OFFSET,
                       PIC32MK_IRQ_SPI6_RX,
                       PIC32MK_IRQ_SPI6_TX,
                       PIC32MK_IRQ_SPI6_FAULT);

    /* I2C 1–4 */
    pic32mk_i2c_create(s, PIC32MK_I2C1_OFFSET);
    pic32mk_i2c_create(s, PIC32MK_I2C2_OFFSET);
    pic32mk_i2c_create(s, PIC32MK_I2C3_OFFSET);
    pic32mk_i2c_create(s, PIC32MK_I2C4_OFFSET);

    /* ADCHS — High-Speed ADC at 0xBF887000 */
    {
        DeviceState *adc = qdev_new(TYPE_PIC32MK_ADCHS);
        object_property_add_child(OBJECT(machine), "adchs", OBJECT(adc));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(adc), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(adc), 0);
        memory_region_add_subregion_overlap(&s->sfr, PIC32MK_ADC_OFFSET,
                                            mr, 1);
        /* IRQ 0 = EOS (101), IRQ 1 = main ADC (92) */
        sysbus_connect_irq(SYS_BUS_DEVICE(adc), 0,
                           qdev_get_gpio_in(s->evic, PIC32MK_IRQ_ADC_EOS));
        sysbus_connect_irq(SYS_BUS_DEVICE(adc), 1,
                           qdev_get_gpio_in(s->evic, PIC32MK_IRQ_ADC));
    }

    /* CAN FD 1–4 */
    pic32mk_canfd_create(s, PIC32MK_CAN1_OFFSET,
                         PIC32MK_CAN1_MSGRAM_BASE, PIC32MK_IRQ_CAN1,
                         pic32mk_find_canbus(0), 0);
    pic32mk_canfd_create(s, PIC32MK_CAN2_OFFSET,
                         PIC32MK_CAN2_MSGRAM_BASE, PIC32MK_IRQ_CAN2,
                         pic32mk_find_canbus(1), 1);
    pic32mk_canfd_create(s, PIC32MK_CAN3_OFFSET,
                         PIC32MK_CAN3_MSGRAM_BASE, PIC32MK_IRQ_CAN3,
                         pic32mk_find_canbus(2), 2);
    pic32mk_canfd_create(s, PIC32MK_CAN4_OFFSET,
                         PIC32MK_CAN4_MSGRAM_BASE, PIC32MK_IRQ_CAN4,
                         pic32mk_find_canbus(3), 3);

    /* USB OTG 1–2 (Phase 4A — register-file stub) */
    pic32mk_usb_create(s, PIC32MK_USB1_OFFSET, PIC32MK_IRQ_USB1, "usbcdc");
    pic32mk_usb_create(s, PIC32MK_USB2_OFFSET, PIC32MK_IRQ_USB2, NULL);

    /* WDT — register-file stub; absorbs WDTCON reads/writes and clear-key */
    sfr_device_create(&s->sfr, TYPE_PIC32MK_WDT, PIC32MK_WDT_OFFSET,
                      &error_fatal);

    /* CFG / PMD / SYSKEY — register block at 0xBF800000 */
    sfr_device_create(&s->sfr, TYPE_PIC32MK_CFG, PIC32MK_CFG_OFFSET,
                      &error_fatal);

    /* CRU — Clock Reference Unit at 0xBF801200 (includes RCON/RSWRST) */
    sfr_device_create(&s->sfr, TYPE_PIC32MK_CRU, PIC32MK_CRU_OFFSET,
                      &error_fatal);

    /* NVM / Flash Controller at 0xBF800A00 */
    {
        DeviceState *nvm = qdev_new(TYPE_PIC32MK_NVM);
        object_property_set_link(OBJECT(nvm), "pflash",
                                 OBJECT(&s->pflash), &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(nvm), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(nvm), 0);
        memory_region_add_subregion_overlap(&s->sfr, PIC32MK_NVM_OFFSET,
                                            mr, 1);
        sysbus_connect_irq(SYS_BUS_DEVICE(nvm), 0,
                           qdev_get_gpio_in(s->evic, PIC32MK_IRQ_FCE));
    }

    /* Data EEPROM at 0xBF829000 — 4 KB, optionally backed by host file */
    {
        DeviceState *ee = qdev_new(TYPE_PIC32MK_DATAEE);
        /* Backing file: use -global pic32mk-dataee.filename=<path> */
        sysbus_realize_and_unref(SYS_BUS_DEVICE(ee), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(ee), 0);
        memory_region_add_subregion_overlap(&s->sfr, PIC32MK_DATAEE_OFFSET,
                                            mr, 1);
        sysbus_connect_irq(SYS_BUS_DEVICE(ee), 0,
                           qdev_get_gpio_in(s->evic, PIC32MK_IRQ_DATAEE));
    }

    pic32mk_load_firmware(machine);
}

/*
 * MachineClass registration
 * -----------------------------------------------------------------------
 */

static void pic32mk_machine_class_init(MachineClass *mc)
{
    mc->desc           = "Microchip PIC32MK GPK/MCM with CAN FD";
    mc->init           = pic32mk_machine_init;
    mc->max_cpus       = 1;
    /*
     * microAptiv: MIPS32r2 + FPU + DSP R2 + microMIPS + MCU ASE,
     * fixed-mapping MMU, VEIC — matches PIC32MK DS60001519E §3.
     */
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("microAptiv");
    mc->default_ram_id   = "pic32mk.ram";
    mc->default_ram_size = PIC32MK_RAM_SIZE;
    mc->no_parallel      = 1;
    mc->no_floppy        = 1;
    mc->no_cdrom         = 1;
}

DEFINE_MACHINE("pic32mk", pic32mk_machine_class_init)
