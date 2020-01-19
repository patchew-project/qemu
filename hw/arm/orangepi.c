/*
 * Orange Pi emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/arm/allwinner-h3.h"
#include "sysemu/sysemu.h"

static struct arm_boot_info orangepi_binfo = {
    .nb_cpus = AW_H3_NUM_CPUS,
};

typedef struct OrangePiState {
    AwH3State *h3;
    MemoryRegion sdram;
} OrangePiState;

static void orangepi_init(MachineState *machine)
{
    OrangePiState *s = g_new(OrangePiState, 1);

    /* BIOS is not supported by this board */
    if (bios_name) {
        error_report("BIOS not supported for this machine");
        exit(1);
    }

    /* Only allow Cortex-A7 for this board */
    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a7")) != 0) {
        error_report("This board can only be used with cortex-a7 CPU");
        exit(1);
    }

    s->h3 = AW_H3(object_new(TYPE_AW_H3));

    /* Setup timer properties */
    object_property_set_int(OBJECT(s->h3), 32768, "clk0-freq",
                            &error_abort);
    object_property_set_int(OBJECT(s->h3), 24 * 1000 * 1000, "clk1-freq",
                            &error_abort);

    /* Setup SID properties */
    qdev_prop_set_string(DEVICE(s->h3), "identifier",
                         "8100c002-0001-0002-0003-000044556677");

    /* Mark H3 object realized */
    object_property_set_bool(OBJECT(s->h3), true, "realized", &error_abort);

    /* SDRAM */
    if (machine->ram_size != 1 * GiB) {
        error_report("This machine can only be used with 1GiB of RAM");
        exit(1);
    }
    memory_region_allocate_system_memory(&s->sdram, NULL, "sdram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), s->h3->memmap[AW_H3_SDRAM],
                                &s->sdram);

    orangepi_binfo.loader_start = s->h3->memmap[AW_H3_SDRAM];
    orangepi_binfo.ram_size = machine->ram_size;
    arm_load_kernel(ARM_CPU(first_cpu), machine, &orangepi_binfo);
}

static void orangepi_machine_init(MachineClass *mc)
{
    mc->desc = "Orange Pi PC";
    mc->init = orangepi_init;
    mc->min_cpus = AW_H3_NUM_CPUS;
    mc->max_cpus = AW_H3_NUM_CPUS;
    mc->default_cpus = AW_H3_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    mc->default_ram_size = 1 * GiB;
}

DEFINE_MACHINE("orangepi-pc", orangepi_machine_init)
