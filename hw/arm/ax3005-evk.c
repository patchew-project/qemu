/*
 * Axiado Ax3005 Evaluation Kit Emulation
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/arm/axiado-boards.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "qemu/error-report.h"
#include "qom/object.h"

static struct arm_boot_info ax3005_binfo = {
    .loader_start = AX3005_DRAM_BASE,
    .board_id = -1,
};

static void ax3005_evb_machine_init(MachineState *machine)
{
    Ax3005MachineState *ams = AX3005_MACHINE(machine);

    ams->soc = AX3005_SOC(object_new(TYPE_AX3005_SOC));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(ams->soc));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ams->soc), &error_fatal);

    ax3005_binfo.ram_size = machine->ram_size;
    arm_load_kernel(&ams->soc->cpu[0], machine, &ax3005_binfo);
}

static void ax3005_evb_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Axiado AX3005 Evaluation Board";
    mc->init = ax3005_evb_machine_init;
    mc->default_cpus = AX3005_NUM_CPUS;
    mc->min_cpus = AX3005_NUM_CPUS;
    mc->max_cpus = AX3005_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
}

static const TypeInfo ax3005_evk_types[] = {
    {
        .name          = TYPE_AX3005_MACHINE,
        .parent        = TYPE_MACHINE,
        .instance_size = sizeof(Ax3005MachineState),
        .class_init    = ax3005_evb_class_init,
        .interfaces    = aarch64_machine_interfaces,
    }
};

DEFINE_TYPES(ax3005_evk_types)
