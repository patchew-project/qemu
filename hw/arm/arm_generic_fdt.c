/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2012 Xilinx. Inc
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@xilinx.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/core/boards.h"
#include "hw/core/hw-error.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qom/object.h"
#include "system/system.h"
#include "system/qtest.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"

#include <libfdt.h>
#include "hw/core/fdt_generic_util.h"

#define QTEST_RUNNING (qtest_enabled() && qtest_driver())

struct ARMGenericFDTState {
    MachineState parent;

    struct arm_boot_info bootinfo;

    char *hw_dtb;
};

#define TYPE_ARM_GENERIC_FDT_MACHINE MACHINE_TYPE_NAME("arm-generic-fdt")

OBJECT_DECLARE_SIMPLE_TYPE(ARMGenericFDTState, ARM_GENERIC_FDT_MACHINE)

static void init_machine(void *fdt, ARMGenericFDTState *s)
{
    FDTMachineInfo *fdti;
    MemoryRegion *mem_area;
    Error *errp = NULL;
    char **node_path;

    node_path = qemu_fdt_node_unit_path(fdt, "memory", &errp);
    if (errp) {
        error_report_err(errp);
        exit(1);
    }
    if (!node_path || !g_str_has_prefix(node_path[0], "/memory")) {
        error_report("Failed to find /memory node");
        exit(1);
    }

    /* Instantiate peripherals from the FDT.  */
    fdti = fdt_generic_create_machine(fdt, NULL);

    mem_area = MEMORY_REGION(object_resolve_path(node_path[0], NULL));

    s->bootinfo.loader_start = object_property_get_int(OBJECT(mem_area),
                                                            "addr", NULL);

    s->bootinfo.ram_size = object_property_get_int(OBJECT(mem_area),
                                                          "size", NULL);

    fdt_init_destroy_fdti(fdti);
    g_strfreev(node_path);
}

static void arm_generic_fdt_init(MachineState *machine)
{
    int fdt_size;
    void *hw_fdt = NULL;
    ARMGenericFDTState *s = ARM_GENERIC_FDT_MACHINE(machine);

    if (!s->hw_dtb) {
        if (!QTEST_RUNNING) {
            /*
             * Just return without error if running qtest, as we never have a
             * device tree
             */
            hw_error("DTB must be specified for %s machine model\n",
                     MACHINE_GET_CLASS(machine)->name);
        }
        return;
    }

    hw_fdt = load_device_tree(s->hw_dtb, &fdt_size);
    if (!hw_fdt) {
        error_report("Error: Unable to load Device Tree %s", s->hw_dtb);
        exit(1);
    }

    init_machine(hw_fdt, s);

    s->bootinfo.kernel_filename = machine->kernel_filename;
    s->bootinfo.kernel_cmdline = machine->kernel_cmdline;
    s->bootinfo.initrd_filename = machine->initrd_filename;
    s->bootinfo.board_id = -1;
    s->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;

    /*
     * Expect a direct kernel boot if the '-kernel' option is specified.
     * In this case, QEMU provides the PSCI implementation.
     */
    if (machine->kernel_filename) {
        s->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    }

    arm_load_kernel(ARM_CPU(first_cpu), machine, &s->bootinfo);

    return;
}

static char *arm_generic_fdt_get_hw_dtb(Object *obj, Error **errp)
{
    ARMGenericFDTState *s = ARM_GENERIC_FDT_MACHINE(obj);

    return g_strdup(s->hw_dtb);
}

static void arm_generic_fdt_set_hw_dtb(Object *obj, const char *value,
                                       Error **errp)
{
    ARMGenericFDTState *s = ARM_GENERIC_FDT_MACHINE(obj);

    s->hw_dtb = g_strdup(value);
}

static void arm_generic_fdt_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ARM device tree driven machine model";
    mc->init = arm_generic_fdt_init;
    mc->max_cpus = 64;
    mc->default_cpus = 4;

    mc->pci_allow_0_address = true;
    mc->minimum_page_bits = 12;
    mc->smp_props.clusters_supported = true;

    object_class_property_add_str(oc, "hw-dtb",
                              arm_generic_fdt_get_hw_dtb,
                                  arm_generic_fdt_set_hw_dtb);
    object_class_property_set_description(oc, "hw-dtb",
                    "Hardware device-tree file with description "
                                "used to create the emulated machine");
}

static const TypeInfo arm_generic_fdt_machine_info = {
    .name           = TYPE_ARM_GENERIC_FDT_MACHINE,
    .parent         = TYPE_MACHINE,
    .instance_size  = sizeof(ARMGenericFDTState),
    .class_init     = arm_generic_fdt_class_init,
    .interfaces     = arm_machine_interfaces,
};

static void arm_generic_fdt_register_type(void)
{
    type_register_static(&arm_generic_fdt_machine_info);
}

type_init(arm_generic_fdt_register_type)
