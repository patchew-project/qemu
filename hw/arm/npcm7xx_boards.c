/*
 * Machine definitions for boards featuring an NPCM7xx SoC.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"

#include "hw/arm/boot.h"
#include "hw/arm/npcm7xx.h"
#include "hw/core/cpu.h"
#include "qapi/error.h"
#include "qemu/units.h"

static struct arm_boot_info npcm7xx_binfo = {
    .loader_start       = NPCM7XX_LOADER_START,
    .smp_loader_start   = NPCM7XX_SMP_LOADER_START,
    .smp_bootreg_addr   = NPCM7XX_SMP_BOOTREG_ADDR,
    .gic_cpu_if_addr    = NPCM7XX_GIC_CPU_IF_ADDR,
    .write_secondary_boot = npcm7xx_write_secondary_boot,
    .board_id           = -1,
};

static void npcm7xx_machine_init(MachineState *machine)
{
    NPCM7xxMachineClass *nmc = NPCM7XX_MACHINE_GET_CLASS(machine);
    NPCM7xxState *soc;

    soc = NPCM7XX(object_new_with_props(nmc->soc_type, OBJECT(machine), "soc",
                                        &error_abort, NULL));
    object_property_set_int(OBJECT(soc), machine->smp.cpus, "num-cpus",
                            &error_abort);
    object_property_set_link(OBJECT(soc), OBJECT(machine->ram), "dram",
                             &error_abort);
    object_property_set_bool(OBJECT(soc), true, "realized", &error_abort);

    npcm7xx_binfo.ram_size = machine->ram_size;
    npcm7xx_binfo.nb_cpus = machine->smp.cpus;

    arm_load_kernel(soc->cpu[0], machine, &npcm7xx_binfo);
}

static void npcm7xx_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init            = npcm7xx_machine_init;
    mc->max_cpus        = NPCM7XX_MAX_NUM_CPUS;
    mc->default_cpus    = NPCM7XX_MAX_NUM_CPUS;
    mc->no_floppy       = 1;
    mc->no_cdrom        = 1;
    mc->no_parallel     = 1;
    mc->default_ram_id  = "ram";
}

/*
 * Schematics:
 * https://github.com/Nuvoton-Israel/nuvoton-info/blob/master/npcm7xx-poleg/evaluation-board/board_deliverables/NPCM750x_EB_ver.A1.1_COMPLETE.pdf
 */
static void npcm750_evb_machine_class_init(ObjectClass *oc, void *data)
{
    NPCM7xxMachineClass *nmc = NPCM7XX_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc            = "Nuvoton NPCM750 Evaluation Board (Cortex A9)";
    nmc->soc_type       = TYPE_NPCM750;
    mc->default_ram_size = 512 * MiB;
};

static void gsj_machine_class_init(ObjectClass *oc, void *data)
{
    NPCM7xxMachineClass *nmc = NPCM7XX_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc            = "Quanta GSJ (Cortex A9)";
    nmc->soc_type       = TYPE_NPCM730;
    mc->default_ram_size = 512 * MiB;
};

static const TypeInfo npcm7xx_machine_types[] = {
    {
        .name           = TYPE_NPCM7XX_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(NPCM7xxMachine),
        .class_size     = sizeof(NPCM7xxMachineClass),
        .class_init     = npcm7xx_machine_class_init,
        .abstract       = true,
    }, {
        .name           = MACHINE_TYPE_NAME("npcm750-evb"),
        .parent         = TYPE_NPCM7XX_MACHINE,
        .class_init     = npcm750_evb_machine_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("quanta-gsj"),
        .parent         = TYPE_NPCM7XX_MACHINE,
        .class_init     = gsj_machine_class_init,
    },
};

DEFINE_TYPES(npcm7xx_machine_types)
