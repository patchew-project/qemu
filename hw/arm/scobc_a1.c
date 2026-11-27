/*
 * Space Cubics SC-OBC Module A1 board
 *
 * Copyright (c) 2026 Space Cubics Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/arm/armv7m.h"
#include "hw/arm/boot.h"
#include "hw/char/xilinx_uartlite.h"
#include "hw/misc/unimp.h"

struct SCOBCA1MachineState {
    MachineState parent_obj;

    ARMv7MState armv7m;
    Clock *cpuclk;
    MemoryRegion hrmem_mirror;
};

#define TYPE_SCOBC_A1_MACHINE MACHINE_TYPE_NAME("scobc-a1")
OBJECT_DECLARE_SIMPLE_TYPE(SCOBCA1MachineState, SCOBC_A1_MACHINE)

/* CPU clock frequency in Hz */
#define CPUCLK_FRQ 48000000

#define SCOBC_A1_NUM_IRQ 32

#define SCOBC_A1_HRMEM_BASE   0x00000000
#define SCOBC_A1_HRMEM_MIRROR 0x60000000
#define SCOBC_A1_UART_BASE    0x4f010000
#define SCOBC_A1_UART_IRQ     0

static void scobc_a1_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    SCOBCA1MachineState *s = SCOBC_A1_MACHINE(machine);
    DeviceState *uart;

    /* Check the amount of memory is compatible with the board */
    if (machine->ram_size != mc->default_ram_size) {
        g_autofree char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        exit(EXIT_FAILURE);
    }

    /* Only direct boot into HRMEM is currently supported */
    memory_region_add_subregion(get_system_memory(), SCOBC_A1_HRMEM_BASE,
                                machine->ram);

    /* HRMEM is mirrored at 0x6000_0000 */
    memory_region_init_alias(&s->hrmem_mirror, OBJECT(s),
                             "scobc-a1.hrmem-mirror", machine->ram,
                             0, mc->default_ram_size);
    memory_region_add_subregion(get_system_memory(), SCOBC_A1_HRMEM_MIRROR,
                                &s->hrmem_mirror);

    s->cpuclk = clock_new(OBJECT(machine), "cpuclk");
    clock_set_hz(s->cpuclk, CPUCLK_FRQ);

    object_initialize_child(OBJECT(s), "armv7m", &s->armv7m, TYPE_ARMV7M);
    qdev_prop_set_string(DEVICE(&s->armv7m), "cpu-type", machine->cpu_type);
    qdev_prop_set_uint32(DEVICE(&s->armv7m), "num-irq", SCOBC_A1_NUM_IRQ);
    qdev_prop_set_uint8(DEVICE(&s->armv7m), "num-prio-bits", 3);
    qdev_connect_clock_in(DEVICE(&s->armv7m), "cpuclk", s->cpuclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), &error_fatal);

    uart = qdev_new(TYPE_XILINX_UARTLITE);
    qdev_prop_set_enum(uart, "endianness", ENDIAN_MODE_LITTLE);
    qdev_prop_set_chr(uart, "chardev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(uart), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(uart), 0, SCOBC_A1_UART_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(uart), 0,
                       qdev_get_gpio_in(DEVICE(&s->armv7m), SCOBC_A1_UART_IRQ));

    /* System peripherals */
    create_unimplemented_device("scobc-a1.qspi-config", 0x40000000, 0x10000);
    create_unimplemented_device("scobc-a1.qspi-data", 0x40100000, 0x10000);
    create_unimplemented_device("scobc-a1.qspi-fram", 0x40200000, 0x10000);
    create_unimplemented_device("scobc-a1.can", 0x40400000, 0x10000);
    create_unimplemented_device("scobc-a1.hrmem-regs", 0x40500000, 0x10000);
    create_unimplemented_device("scobc-a1.sysreg", 0x4f000000, 0x10000);
    create_unimplemented_device("scobc-a1.i2c0", 0x4f030000, 0x10000);
    create_unimplemented_device("scobc-a1.sysmon", 0x4f040000, 0x10000);
    create_unimplemented_device("scobc-a1.timer", 0x4f050000, 0x10000);

    /* Mission AXI bus peripherals (as defined in standard FPGA image) */
    create_unimplemented_device("scobc-a1.uartlite1", 0x50000000, 0x10000);
    create_unimplemented_device("scobc-a1.uartlite2", 0x50010000, 0x10000);
    create_unimplemented_device("scobc-a1.uartlite3", 0x50020000, 0x10000);
    create_unimplemented_device("scobc-a1.uartlite4", 0x50030000, 0x10000);
    create_unimplemented_device("scobc-a1.uartlite5", 0x50040000, 0x10000);
    create_unimplemented_device("scobc-a1.uartlite6", 0x50050000, 0x10000);
    create_unimplemented_device("scobc-a1.i2c1", 0x50060000, 0x10000);
    create_unimplemented_device("scobc-a1.i2c2", 0x50070000, 0x10000);
    create_unimplemented_device("scobc-a1.qspi", 0x50080000, 0x10000);
    create_unimplemented_device("scobc-a1.gpio", 0x50090000, 0x10000);

    armv7m_load_kernel(s->armv7m.cpu, machine->kernel_filename,
                       SCOBC_A1_HRMEM_BASE, machine->ram_size);
}

static void scobc_a1_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m3"),
        NULL
    };

    mc->desc = "Space Cubics SC-OBC Module A1 (Cortex-M3)";
    mc->init = scobc_a1_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m3");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = 1;
    mc->default_ram_size = 4 * MiB;
    mc->default_ram_id = "scobc-a1.hrmem";
    mc->no_parallel = true;
    mc->no_floppy = true;
    mc->no_cdrom = true;
}

DEFINE_MACHINE_EXTENDED("scobc-a1", MACHINE, SCOBCA1MachineState,
                        scobc_a1_machine_init, false, false, NULL)
