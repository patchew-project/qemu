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
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/arm/allwinner-h3.h"

static struct arm_boot_info orangepi_binfo = {
    .loader_start = AW_H3_SDRAM_BASE,
    .board_id = -1,
};

typedef struct OrangePiState {
    AwH3State *h3;
    MemoryRegion sdram;
} OrangePiState;

static void orangepi_init(MachineState *machine)
{
    OrangePiState *s = g_new(OrangePiState, 1);
    DriveInfo *di;
    BlockBackend *blk;
    BusState *bus;
    DeviceState *carddev;
    Error *err = NULL;

    s->h3 = AW_H3(object_new(TYPE_AW_H3));

    /* Setup timer properties */
    object_property_set_int(OBJECT(&s->h3->timer), 32768, "clk0-freq", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't set clk0 frequency: ");
        exit(1);
    }

    object_property_set_int(OBJECT(&s->h3->timer), 24000000, "clk1-freq",
                            &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't set clk1 frequency: ");
        exit(1);
    }

    /* Mark H3 object realized */
    object_property_set_bool(OBJECT(s->h3), true, "realized", &err);
    if (err != NULL) {
        error_reportf_err(err, "Couldn't realize Allwinner H3: ");
        exit(1);
    }

    /* Create and plug in the SD card */
    di = drive_get_next(IF_SD);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    bus = qdev_get_child_bus(DEVICE(s->h3), "sd-bus");
    if (bus == NULL) {
        error_report("No SD/MMC found in H3 object");
        exit(1);
    }
    carddev = qdev_create(bus, TYPE_SD_CARD);
    qdev_prop_set_drive(carddev, "drive", blk, &error_fatal);
    object_property_set_bool(OBJECT(carddev), true, "realized", &error_fatal);

    /* RAM */
    memory_region_allocate_system_memory(&s->sdram, NULL, "orangepi.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), AW_H3_SDRAM_BASE,
                                &s->sdram);

    /* Load target kernel */
    orangepi_binfo.ram_size = machine->ram_size;
    orangepi_binfo.nb_cpus  = AW_H3_NUM_CPUS;
    arm_load_kernel(ARM_CPU(first_cpu), machine, &orangepi_binfo);
}

static void orangepi_machine_init(MachineClass *mc)
{
    mc->desc = "Orange Pi PC";
    mc->init = orangepi_init;
    mc->block_default_type = IF_SD;
    mc->units_per_default_bus = 1;
    mc->min_cpus = AW_H3_NUM_CPUS;
    mc->max_cpus = AW_H3_NUM_CPUS;
    mc->default_cpus = AW_H3_NUM_CPUS;
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("orangepi", orangepi_machine_init)
