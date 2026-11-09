/*
 * Orange Pi Zero 2W machine emulation
 *
 * Copyright (C) 2026 Khai Do <dovankhai97@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/arm/allwinner-h618.h"
#include "hw/arm/boot.h"

#define TYPE_ORANGEPI_ZERO2W_MACHINE MACHINE_TYPE_NAME("orangepi-zero2w")
OBJECT_DECLARE_SIMPLE_TYPE(OrangePiZero2WMachineState, ORANGEPI_ZERO2W_MACHINE)

struct OrangePiZero2WMachineState {
    MachineState parent;

    struct arm_boot_info bootinfo;
};

static void orangepi_zero2w_init(MachineState *machine)
{
    OrangePiZero2WMachineState *opms = ORANGEPI_ZERO2W_MACHINE(machine);
    AwH618State *h618;

    /* BIOS is not supported by this board */
    if (machine->firmware) {
        error_report("BIOS not supported for this machine");
        exit(1);
    }

    h618 = AW_H618(object_new(TYPE_AW_H618));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(h618));
    object_unref(OBJECT(h618));

    /* Mark H618 object realized */
    qdev_realize(DEVICE(h618), NULL, &error_abort);

    /* SDRAM */
    memory_region_add_subregion(get_system_memory(),
                                h618->memmap[AW_H618_DEV_SDRAM],
                                machine->ram);

    opms->bootinfo.loader_start = h618->memmap[AW_H618_DEV_SDRAM];
    opms->bootinfo.ram_size = machine->ram_size;
    opms->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    arm_load_kernel(&h618->cpus[0], machine, &opms->bootinfo);
}

static void orangepi_zero2w_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a53"),
        NULL
    };

    mc->desc = "Orange Pi Zero 2W (Cortex-A53)";
    mc->init = orangepi_zero2w_init;
    mc->min_cpus = AW_H618_NUM_CPUS;
    mc->max_cpus = AW_H618_NUM_CPUS;
    mc->default_cpus = AW_H618_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "orangepi-zero2w.ram";
}

DEFINE_MACHINE_EXTENDED("orangepi-zero2w", MACHINE, OrangePiZero2WMachineState,
                        orangepi_zero2w_machine_init, false, false,
                        NULL)
