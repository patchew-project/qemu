/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Sun-3 Board Emulation
 *
 * Copyright (c) 2026
 */

#include "qemu/osdep.h"

#include "chardev/char.h"
#include "hw/char/escc.h"
#include "hw/core/boards.h"
#include "hw/core/irq.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/net/lance.h"
#include "hw/intc/m68k_irqc.h"
#include "hw/m68k/sun3mmu.h"
#include "hw/timer/intersil7170.h"
#include "qapi/error.h"
#include "system/qtest.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "sun3_eeprom_data.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/m68k/cpu.h"
#include "qom/object.h"

#define SUN3_PROM_BASE 0x0FEF0000
#define SUN3_PROM_SIZE (64 * 1024)

#define TYPE_SUN3_MACHINE MACHINE_TYPE_NAME("sun3")
OBJECT_DECLARE_SIMPLE_TYPE(Sun3MachineState, SUN3_MACHINE)

struct Sun3MachineState {
    MachineState parent_obj;

    /* Embedded Memory Regions */
    MemoryRegion rom;
    MemoryRegion rom_alias;
    MemoryRegion idprom;
    MemoryRegion intreg_iomem;
    MemoryRegion memerr_iomem;
    MemoryRegion eeprom;
    MemoryRegion nvram;
    MemoryRegion timeout_net;

    /* Devices */
    DeviceState *irqc_dev;
    DeviceState *sun3mmu;

    /* Boot State */
    uint32_t boot_sp;
    uint32_t boot_pc;

    /* Interrupt Register State */
    uint8_t intreg;
    bool clock_pending;

    /* Memory Error Register (Parity Spoof) State */
    uint8_t memerr_reg;
    uint8_t spoof_parity_lane;
    uint8_t parity_bit_counter;
    bool test_parity_written;
};

static void sun3_update_clock_irq(Sun3MachineState *s)
{
    if (!s->irqc_dev) {
        return;
    }

    /* Lower everything first */
    qemu_irq_lower(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_5));
    qemu_irq_lower(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_7));

    /* Assert the currently enabled level if the clock is pulsing */
    if (s->clock_pending && (s->intreg & 0x01)) {
        if (s->intreg & 0x20) {
            qemu_irq_raise(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_5));
        } else if (s->intreg & 0x80) {
            qemu_irq_raise(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_7));
        }
    }
}

static void sun3_clock_irq_handler(void *opaque, int n, int level)
{
    Sun3MachineState *s = SUN3_MACHINE(opaque);
    s->clock_pending = !!level;
    sun3_update_clock_irq(s);
}

static uint64_t sun3_intreg_read(void *opaque, hwaddr addr, unsigned size)
{
    Sun3MachineState *s = SUN3_MACHINE(opaque);
    return s->intreg;
}

static void sun3_intreg_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    Sun3MachineState *s = SUN3_MACHINE(opaque);
    s->intreg = val;

    qemu_log_mask(
        LOG_GUEST_ERROR,
        "[SUN3 INTREG] Write: 0x%02x (Enable=%d, L1=%d, L2=%d, L3=%d)\n",
        (uint8_t)val, !!(val & 0x01), !!(val & 0x02), !!(val & 0x04),
        !!(val & 0x08));

    if ((val & 0x01) == 0) {
        /* Master Interrupt Enable is CLEAR. Mask everything! */
        qemu_set_irq(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_1), 0);
        qemu_set_irq(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_2), 0);
        qemu_set_irq(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_3), 0);
        sun3_update_clock_irq(s);
        return;
    }

    /* Master Enable is SET. Fire the Soft Interrupts! */
    qemu_set_irq(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_1),
                 !!(val & 0x02));
    qemu_set_irq(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_2),
                 !!(val & 0x04));
    qemu_set_irq(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_3),
                 !!(val & 0x08));

    /*
     * Retrigger clock IRQs because the Master Enable or Local Enables might
     * have changed
     */
    sun3_update_clock_irq(s);
}

static const MemoryRegionOps sun3_intreg_ops = {.read = sun3_intreg_read,
                                                .write = sun3_intreg_write,
                                                .endianness =
                                                    DEVICE_BIG_ENDIAN,
                                                .valid = {
                                                    .min_access_size = 1,
                                                    .max_access_size = 4,
                                                } };

static uint64_t sun3_memerr_read(void *opaque, hwaddr addr, unsigned size)
{
    Sun3MachineState *s = SUN3_MACHINE(opaque);
    return s->memerr_reg;
}

static void sun3_memerr_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    Sun3MachineState *s = SUN3_MACHINE(opaque);

    if (addr == 4) {
        s->memerr_reg &= ~0x80;
        if (s->irqc_dev) {
            qemu_irq_lower(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_7));
        }
        return;
    }

    s->memerr_reg = val & 0xFF;
    if (val == 0x20) {
        s->test_parity_written = true;
    } else if (val == 0) {
        s->test_parity_written = false;
    }

    if (s->irqc_dev) {
        if ((val & 0x10) && (val & 0x40) && s->test_parity_written) {
            s->memerr_reg |=
                0x80 | s->spoof_parity_lane; /* active & spoofed bit lane */
            s->parity_bit_counter++;
            if (s->parity_bit_counter == 8) {
                s->parity_bit_counter = 0;
                s->spoof_parity_lane >>= 1;
                if (s->spoof_parity_lane == 0) {
                    s->spoof_parity_lane = 8;
                }
            }

            qemu_irq_raise(qdev_get_gpio_in(
                s->irqc_dev, M68K_IRQC_LEVEL_7)); /* M68K_IRQC_LEVEL_7 */
        } else {
            s->memerr_reg &= ~0x80;
            qemu_irq_lower(qdev_get_gpio_in(s->irqc_dev, M68K_IRQC_LEVEL_7));
        }
    }
}

static const MemoryRegionOps sun3_memerr_ops = {
    .read = sun3_memerr_read,
    .write = sun3_memerr_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
            .min_access_size = 1,
            .max_access_size = 4,
            .unaligned = true,
        },
    .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
            .unaligned = true,
        },
};

static MemTxResult sun3_timeout_read_with_attrs(void *opaque, hwaddr addr,
                                                uint64_t *data, unsigned size,
                                                MemTxAttrs attrs)
{
    Sun3MMUState *mmu = SUN3_MMU(opaque);
    mmu->buserr_reg |= 0x20; /* Timeout */
    return MEMTX_ERROR;
}

static MemTxResult sun3_timeout_write_with_attrs(void *opaque, hwaddr addr,
                                                 uint64_t val, unsigned size,
                                                 MemTxAttrs attrs)
{
    Sun3MMUState *mmu = SUN3_MMU(opaque);
    mmu->buserr_reg |= 0x20; /* Timeout */
    return MEMTX_ERROR;
}

static const MemoryRegionOps sun3_timeout_ops = {
    .read_with_attrs = sun3_timeout_read_with_attrs,
    .write_with_attrs = sun3_timeout_write_with_attrs,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void sun3_cpu_reset(void *opaque)
{
    M68kCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUM68KState *env = cpu_env(cs);
    Sun3MachineState *s = SUN3_MACHINE(current_machine);

    /*
     * Execute generic QEMU system reset (wipes everything to 0).
     * This includes setting env->pc = 0 and env->aregs[7] = 0.
     */
    cpu_reset(cs);

    /*
     * Forcefully inject the Boot PROM SP/PC vectors on EVERY reset.
     * The Sun-3 hardware uses a temporary MMU override to map the PROM to
     * 0x00000000 during the first few cycles of reset. Since QEMU does not
     * emulate this specific micro-architectural quirk, we must manually
     * restore the vectors here to prevent the CPU from executing uninitialized
     * RAM at 0x00000000 and getting stuck in a zero-pitch orib loop.
     */
    env->aregs[7] = s->boot_sp;
    env->sp[0] = s->boot_sp; /* M68K_SSP (Master) */
    env->sp[1] = s->boot_sp; /* M68K_USP (User) */
    env->sp[2] = s->boot_sp; /* M68K_ISP (Interrupt) */
    env->pc = s->boot_pc;
}

static void sun3_init(MachineState *machine)
{
    Sun3MachineState *s_mach = SUN3_MACHINE(machine);
    M68kCPU *cpu;
    CPUM68KState *env;
    DeviceState *sun3mmu;
    DeviceState *dev;
    DeviceState *irqc_dev;
    SysBusDevice *s;

    /* Initialize defaults */
    s_mach->memerr_reg = 0x40;
    s_mach->spoof_parity_lane = 8;
    s_mach->parity_bit_counter = 0;
    s_mach->test_parity_written = false;
    s_mach->intreg = 0;
    s_mach->clock_pending = false;

    /* Initialize the CPU. The Sun 3/60 uses a 68020. */
    cpu = M68K_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;
    qemu_register_reset(sun3_cpu_reset, cpu);

    /* Use automatically allocated main RAM */
    memory_region_add_subregion(get_system_memory(), 0x00000000, machine->ram);

    /* Allocate and map ROM as writable RAM! */
    memory_region_init_ram(&s_mach->rom, NULL, "sun3.prom",
                           SUN3_PROM_SIZE, &error_fatal);
    memory_region_set_readonly(&s_mach->rom, false);
    memory_region_add_subregion(get_system_memory(), SUN3_PROM_BASE,
                                &s_mach->rom);

    memory_region_init_alias(&s_mach->rom_alias, NULL, "sun3.prom.alias",
                             &s_mach->rom, 0,
                             SUN3_PROM_SIZE);
    memory_region_add_subregion(get_system_memory(), 0x0FF00000,
                                &s_mach->rom_alias);

    const char *bios_name = machine->firmware ?: "sun3.prom";
    if (bios_name) {
        int load_size = load_image_targphys(bios_name, SUN3_PROM_BASE,
                                            SUN3_PROM_SIZE,
                             qtest_enabled() ? NULL : &error_fatal);
        if (load_size < 0) {
            if (!qtest_enabled()) {
                error_report("sun3: could not load prom '%s'", bios_name);
                exit(1);
            }
        }
        error_report("Sun3 Init: Loaded %d bytes from '%s' at 0x%08x",
                     load_size,
                     bios_name, SUN3_PROM_BASE);

        /* Initial PC is always at offset 4 in firmware binaries */
        uint8_t *ptr = rom_ptr(SUN3_PROM_BASE, 8);
        if (ptr) {
            s_mach->boot_sp = ldl_be_p(ptr);
            s_mach->boot_pc = ldl_be_p(ptr + 4);
            error_report("Sun3 Init: Saved Firmware Vectors "
                         "SP=0x%08x PC=0x%08x",
                         s_mach->boot_sp, s_mach->boot_pc);
        }
    } else {
        error_report(
            "Sun3 Init: No firmware specified! Use -bios or -machine firmware=");
    }

    /* Set up the custom Sun-3 MMU */
    sun3mmu = qdev_new(TYPE_SUN3_MMU);
    s_mach->sun3mmu = sun3mmu;
    s = SYS_BUS_DEVICE(sun3mmu);
    sysbus_realize_and_unref(s, &error_fatal);

    /* Intercept CPU memory translations with our custom MMU hook */
    env->custom_mmu_opaque = sun3mmu;
    env->custom_mmu_get_physical_address = sun3mmu_get_physical_address;

    sysbus_mmio_map(s, 0, 0x80000000); /* Context Register */
    sysbus_mmio_map(s, 1, 0x90000000); /* Segment Map */
    sysbus_mmio_map(s, 2, 0xA0000000); /* Page Map */
    sysbus_mmio_map(s, 3, 0xB0000000); /* Control / System Enable */
    sysbus_mmio_map(s, 4, 0xC0000000); /* Bus Error Register */

    memory_region_init_ram(&s_mach->idprom, NULL, "sun3.idprom", 8192,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x08000000,
                                &s_mach->idprom);

    uint8_t idprom_data[32] = {
        0x01,                               /* Format: 1 */
        0x17,                               /* Machine Type: Sun-3/60 (0x17) */
        0x08, 0x00, 0x20, 0x00, 0x00, 0x01, /* MAC Address */
        0x00, 0x00, 0x00, 0x00,             /* Date */
        0x00, 0x00, 0x01,                   /* Serial */
        0x00                                /* Checksum */
    };
    uint8_t chksum = 0;
    for (int i = 0; i < 15; i++) {
        chksum ^= idprom_data[i];
    }
    idprom_data[15] = chksum;

    rom_add_blob_fixed("sun3.idprom_content", idprom_data, sizeof(idprom_data),
                       0x08000000);

    /*
     * Set up the Interrupt Controller (IRQC) to route IRQs to
     * CPU autovectors
     */
    irqc_dev = qdev_new(TYPE_M68K_IRQC);
    object_property_set_link(OBJECT(irqc_dev), "m68k-cpu", OBJECT(cpu),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(irqc_dev), &error_fatal);
    s_mach->irqc_dev = irqc_dev;

    dev = qdev_new(TYPE_INTERSIL_7170);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, 0x0FE60000);
    sysbus_connect_irq(s, 0,
                       qemu_allocate_irq(sun3_clock_irq_handler, s_mach, 0));

    dev = qdev_new(TYPE_ESCC);
    qdev_prop_set_bit(dev, "force-hw-ready", true);
    qdev_prop_set_uint32(dev, "disabled", 0);
    qdev_prop_set_uint32(dev, "frequency", 4915200); /* 4.9152 MHz */
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_prop_set_bit(dev, "bit_swap", false); /* Control/Data interleaving */
    qdev_prop_set_uint32(dev, "mmio_size", 8192);
    qdev_prop_set_chr(dev, "chrB", serial_hd(0)); /* Keyboard/Mouse A */
    qdev_prop_set_chr(dev, "chrA", serial_hd(1)); /* Keyboard/Mouse B */
    qdev_prop_set_uint32(dev, "chnBtype", escc_serial);
    qdev_prop_set_uint32(dev, "chnAtype", escc_serial);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, 0x0FE00000);
    sysbus_connect_irq(s, 0,
                       qdev_get_gpio_in(irqc_dev, M68K_IRQC_LEVEL_6));
    /* IPL 6 */
    sysbus_connect_irq(s, 1,
                       qdev_get_gpio_in(irqc_dev, M68K_IRQC_LEVEL_6));
    /* IPL 6 */

    dev = qdev_new(TYPE_ESCC);
    qdev_prop_set_bit(dev, "force-hw-ready", true);
    qdev_prop_set_uint32(dev, "disabled", 0);
    qdev_prop_set_uint32(dev, "frequency", 4915200); /* 4.9152 MHz */
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_prop_set_bit(dev, "bit_swap", false); /* Control/Data interleaving */
    qdev_prop_set_uint32(dev, "mmio_size", 8192);
    qdev_prop_set_chr(dev, "chrB", serial_hd(2)); /* Serial B */
    qdev_prop_set_chr(dev, "chrA", serial_hd(3)); /* Serial A */
    qdev_prop_set_uint32(dev, "chnBtype", escc_serial);
    qdev_prop_set_uint32(dev, "chnAtype", escc_serial);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, 0x0FE20000);
    sysbus_connect_irq(s, 0,
                       qdev_get_gpio_in(irqc_dev, M68K_IRQC_LEVEL_6));
    /* IPL 6 */
    sysbus_connect_irq(s, 1,
                       qdev_get_gpio_in(irqc_dev, M68K_IRQC_LEVEL_6));
    /* IPL 6 */

    memory_region_init_io(&s_mach->intreg_iomem, NULL, &sun3_intreg_ops, s_mach,
                          "sun3.intreg", 8192);
    memory_region_add_subregion(get_system_memory(), 0x0FEA0000,
                                &s_mach->intreg_iomem);

    memory_region_init_io(&s_mach->memerr_iomem, NULL, &sun3_memerr_ops, s_mach,
                          "sun3.memerr", 32);
    memory_region_add_subregion(get_system_memory(), 0x0FE80000,
                                &s_mach->memerr_iomem);

    dev = qdev_new("lance");
    qemu_configure_nic_device(dev, true, NULL);
    object_property_set_link(OBJECT(dev), "dma_mr",
                             OBJECT(&SUN3_MMU(sun3mmu)->dvma_iommu),
                             &error_abort);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, 0x0FF20000);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(irqc_dev, M68K_IRQC_LEVEL_3));

    memory_region_init_ram(&s_mach->eeprom, NULL, "sun3.eeprom", 2048,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x0FE40000,
                                &s_mach->eeprom);
    memcpy(memory_region_get_ram_ptr(&s_mach->eeprom), sun3_eeprom_blob,
           sizeof(sun3_eeprom_blob));

    memory_region_init_ram(&s_mach->nvram, NULL, "sun3.nvram", 8192,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x0FE50000,
                                &s_mach->nvram);

    memory_region_init_io(&s_mach->timeout_net, NULL, &sun3_timeout_ops,
                          sun3mmu,
                          "sun3.timeout", 0x06000000);
    memory_region_add_subregion_overlap(get_system_memory(), 0x0A000000,
                                        &s_mach->timeout_net, -10);
}

static void sun3_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun-3 (3/60)";
    mc->init = sun3_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68020");
    /* Minimum of 4MB for a 3/60, typical maximum ~24MB */
    mc->default_ram_size = 4 * MiB;
    mc->default_ram_id = "sun3.ram";

    mc->ignore_memory_transaction_failures = false;
}

static const TypeInfo sun3_machine_type = {
    .name = TYPE_SUN3_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = sun3_machine_class_init,
    .instance_size = sizeof(Sun3MachineState),
};

static void sun3_machine_register_types(void)
{
    type_register_static(&sun3_machine_type);
}

type_init(sun3_machine_register_types)
