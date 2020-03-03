/*
 * cubieboard emulation
 *
 * Copyright (C) 2013 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/arm/allwinner-a10.h"

static struct arm_boot_info cubieboard_binfo = {
    .loader_start = AW_A10_SDRAM_BASE,
    .board_id = 0x1008,
};

static void cubieboard_init(MachineState *machine)
{
    AwA10State *a10;
    Error *err = NULL;

    a10 = AW_A10(object_new(TYPE_AW_A10));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(a10),
                              &error_abort);
    object_unref(OBJECT(a10));

    object_property_set_int(OBJECT(&a10->emac), 1, "phy-addr", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't set phy address: ");
        exit(1);
    }

    object_property_set_int(OBJECT(&a10->timer), 32768, "clk0-freq", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't set clk0 frequency: ");
        exit(1);
    }

    object_property_set_int(OBJECT(&a10->timer), 24000000, "clk1-freq", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't set clk1 frequency: ");
        exit(1);
    }

    object_property_set_bool(OBJECT(a10), true, "realized", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't realize Allwinner A10: ");
        exit(1);
    }

    memory_region_add_subregion(get_system_memory(), AW_A10_SDRAM_BASE,
                                machine->ram);

    /* TODO create and connect IDE devices for ide_drive_get() */

    cubieboard_binfo.ram_size = machine->ram_size;
    arm_load_kernel(&a10->cpu, machine, &cubieboard_binfo);
}

static void cubieboard_machine_init(MachineClass *mc)
{
    mc->desc = "cubietech cubieboard (Cortex-A9)";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a9");
    mc->init = cubieboard_init;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "cubieboard.ram";
}

DEFINE_MACHINE("cubieboard", cubieboard_machine_init)
