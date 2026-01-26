// SPDX-License-Identifier: GPL-2.0-or-later
/*
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
#include "hw/core/loader.h"
#include "hw/core/hw-error.h"
#include "qapi/error.h"
#include "hw/block/flash.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "system/system.h"
#include "system/qtest.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"

#include <libfdt.h>
#include "hw/core/fdt_generic_util.h"

#define GENERAL_MACHINE_NAME "arm-generic-fdt"

#define QTEST_RUNNING (qtest_enabled() && qtest_driver())

#define SMP_BOOT_ADDR 0xfffffff0
/* Meaningless, but keeps arm boot happy */
#define SMP_BOOTREG_ADDR 0xfffffffc

static struct arm_boot_info arm_generic_fdt_binfo = {};

typedef struct {
    ram_addr_t ram_kernel_base;
    ram_addr_t ram_kernel_size;
} memory_info;

static memory_info init_memory(void *fdt, ram_addr_t ram_size)
{
    FDTMachineInfo *fdti;
    char node_path[DT_PATH_LENGTH];
    MemoryRegion *mem_area;
    memory_info kernel_info;
    Error *errp = NULL;

    /* Find a memory node or add new one if needed */
    while (qemu_devtree_get_node_by_name(fdt, node_path, "memory")) {
        qemu_fdt_add_subnode(fdt, "/memory@0");
        qemu_fdt_setprop_cells(fdt, "/memory@0", "reg", 0, ram_size);
    }

    if (!qemu_fdt_getprop(fdt, "/memory", "compatible", NULL, 0, NULL)) {
        qemu_fdt_setprop_string(fdt, "/memory", "compatible",
                                "qemu:memory-region");
        qemu_fdt_setprop_cells(fdt, "/memory", "qemu,ram", 1);
    }

    /* Instantiate peripherals from the FDT.  */
    fdti = fdt_generic_create_machine(fdt, NULL);

    mem_area = MEMORY_REGION(object_resolve_path(node_path, NULL));

    /*
     * Look for the optional kernel-base prop. If not found fallback to
     * start of memory.
     */
    kernel_info.ram_kernel_base = qemu_fdt_getprop_sized_cell(fdt, "/",
                                              "kernel-base", 0, 2, &errp);
    if (errp) {
        kernel_info.ram_kernel_base = object_property_get_int(OBJECT(mem_area),
                                                              "addr", NULL);
    }

    kernel_info.ram_kernel_size = object_property_get_int(OBJECT(mem_area),
                                                          "size", NULL);

    if (kernel_info.ram_kernel_size == -1) {
        kernel_info.ram_kernel_size = ram_size;
    }

    fdt_init_destroy_fdti(fdti);

    return kernel_info;
}

static void arm_generic_fdt_init(MachineState *machine)
{
    void *fdt = NULL, *sw_fdt = NULL;
    int fdt_size, sw_fdt_size;
    const char *dtb_arg, *hw_dtb_arg;
    memory_info kernel_info;

    dtb_arg = machine->dtb;
    hw_dtb_arg = machine->hw_dtb;
    if (!dtb_arg && !hw_dtb_arg) {
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

    /* Software dtb is always the -dtb arg */
    if (dtb_arg) {
        sw_fdt = load_device_tree(dtb_arg, &sw_fdt_size);
        if (!sw_fdt) {
            error_report("Error: Unable to load Device Tree %s", dtb_arg);
            exit(1);
        }
    }

    /* If the user provided a -hw-dtb, use it as the hw description.  */
    if (hw_dtb_arg) {
        fdt = load_device_tree(hw_dtb_arg, &fdt_size);
        if (!fdt) {
            error_report("Error: Unable to load Device Tree %s", hw_dtb_arg);
            exit(1);
        }
    } else if (sw_fdt) {
        fdt = sw_fdt;
        fdt_size = sw_fdt_size;
    }

    kernel_info = init_memory(fdt, machine->ram_size);

    arm_generic_fdt_binfo.ram_size = kernel_info.ram_kernel_size;
    arm_generic_fdt_binfo.kernel_filename = machine->kernel_filename;
    arm_generic_fdt_binfo.kernel_cmdline = machine->kernel_cmdline;
    arm_generic_fdt_binfo.initrd_filename = machine->initrd_filename;
    arm_generic_fdt_binfo.smp_loader_start = SMP_BOOT_ADDR;
    arm_generic_fdt_binfo.smp_bootreg_addr = SMP_BOOTREG_ADDR;
    arm_generic_fdt_binfo.board_id = 0xd32;
    arm_generic_fdt_binfo.loader_start = kernel_info.ram_kernel_base;
    arm_generic_fdt_binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;

    if (machine->kernel_filename) {
        arm_load_kernel(ARM_CPU(first_cpu), machine, &arm_generic_fdt_binfo);
    }

    return;
}

static void arm_generic_fdt_machine_init(MachineClass *mc)
{
    mc->desc = "ARM device tree driven machine model";
    mc->init = arm_generic_fdt_init;
    mc->max_cpus = 64;
    mc->default_cpus = 64;
}

DEFINE_MACHINE_ARM("arm-generic-fdt", arm_generic_fdt_machine_init)
