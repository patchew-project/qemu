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
#include <libfdt.h>

static void cubieboard_modify_dtb(const struct arm_boot_info *info, void *fdt)
{
    static const char unsupported_compat[] = "allwinner,sun4i-a10-musb";
    char node_path[72];
    int offset;

    offset = fdt_node_offset_by_compatible(fdt, -1, unsupported_compat);
    while (offset >= 0) {
        int r = fdt_get_path(fdt, offset, node_path, sizeof(node_path));
        assert(r >= 0);
        r = fdt_setprop_string(fdt, offset, "status", "disabled");
        if (r < 0) {
            error_report("%s: Couldn't disable %s: %s", __func__,
                         unsupported_compat, fdt_strerror(r));
            exit(1);
        }
        warn_report("cubieboard: disabled unsupported node %s (%s) "
                    "in device tree", node_path, unsupported_compat);
        offset = fdt_node_offset_by_compatible(fdt, offset, unsupported_compat);
    }
}

static struct arm_boot_info cubieboard_binfo = {
    .loader_start = AW_A10_SDRAM_BASE,
    .board_id = 0x1008,
    .modify_dtb = cubieboard_modify_dtb,
};

typedef struct CubieBoardState {
    AwA10State *a10;
    MemoryRegion sdram;
} CubieBoardState;

static void cubieboard_init(MachineState *machine)
{
    CubieBoardState *s = g_new(CubieBoardState, 1);
    Error *err = NULL;

    s->a10 = AW_A10(object_new(TYPE_AW_A10));

    object_property_set_int(OBJECT(&s->a10->emac), 1, "phy-addr", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't set phy address: ");
        exit(1);
    }

    object_property_set_int(OBJECT(&s->a10->timer), 32768, "clk0-freq", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't set clk0 frequency: ");
        exit(1);
    }

    object_property_set_int(OBJECT(&s->a10->timer), 24000000, "clk1-freq",
                            &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't set clk1 frequency: ");
        exit(1);
    }

    object_property_set_bool(OBJECT(s->a10), true, "realized", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't realize Allwinner A10: ");
        exit(1);
    }

    memory_region_allocate_system_memory(&s->sdram, NULL, "cubieboard.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), AW_A10_SDRAM_BASE,
                                &s->sdram);

    /* TODO create and connect IDE devices for ide_drive_get() */

    cubieboard_binfo.ram_size = machine->ram_size;
    arm_load_kernel(&s->a10->cpu, machine, &cubieboard_binfo);
}

static void cubieboard_machine_init(MachineClass *mc)
{
    mc->desc = "cubietech cubieboard (Cortex-A9)";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a9");
    mc->init = cubieboard_init;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("cubieboard", cubieboard_machine_init)
