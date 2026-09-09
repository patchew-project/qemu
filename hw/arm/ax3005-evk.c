/*
 * Axiado Ax3005 Evaluation Kit Emulation
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <libfdt.h>
#include "hw/arm/axiado-boards.h"
#include "hw/arm/boot.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qom/object.h"
#include "system/device_tree.h"

#define RAMROFS_BASE     0x90000000
#define RAMROFS_SIZE     (100 * MiB)

/*
 * Reserve the ramrofs window so that Linux keeps it out of the linear map and
 * the phram driver can hand it to the guest as a block device.  Any ramrofs
 * reservation the supplied DTB carries is replaced, so that the reservation
 * cannot drift away from RAMROFS_BASE.
 */
static void ax3005_modify_dtb(const struct arm_boot_info *info, void *fdt)
{
    g_autofree char *nodename = NULL;
    uint64_t reg[2];
    char **node_path;
    Error *err = NULL;
    int offset;
    int i;

    node_path = qemu_fdt_node_unit_path(fdt, "ramrofs", &err);
    if (err) {
        error_report_err(err);
        exit(1);
    }
    for (i = 0; node_path[i]; i++) {
        qemu_fdt_nop_node(fdt, node_path[i]);
    }
    g_strfreev(node_path);

    offset = fdt_path_offset(fdt, "/reserved-memory");
    if (offset < 0) {
        qemu_fdt_add_subnode(fdt, "/reserved-memory");
        qemu_fdt_setprop_cell(fdt, "/reserved-memory", "#address-cells", 2);
        qemu_fdt_setprop_cell(fdt, "/reserved-memory", "#size-cells", 2);
        qemu_fdt_setprop(fdt, "/reserved-memory", "ranges", NULL, 0);
    } else if (fdt_address_cells(fdt, offset) != 2 ||
               fdt_size_cells(fdt, offset) != 2) {
        error_report("/reserved-memory must use two address and size cells");
        exit(1);
    }

    nodename = g_strdup_printf("/reserved-memory/ramrofs@%" HWADDR_PRIx,
                               (hwaddr)RAMROFS_BASE);
    reg[0] = cpu_to_be64(RAMROFS_BASE);
    reg[1] = cpu_to_be64(RAMROFS_SIZE);

    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop(fdt, nodename, "reg", reg, sizeof(reg));
    qemu_fdt_setprop(fdt, nodename, "no-map", NULL, 0);
}

static struct arm_boot_info ax3005_binfo = {
    .loader_start = AX3005_DRAM_BASE,
    .board_id = -1,
    .modify_dtb = ax3005_modify_dtb,
};

static void ax3005_evb_machine_init(MachineState *machine)
{
    Ax3005SoCState *s;

    if (machine->ram_size != AX3005_DRAM_SIZE) {
        g_autofree char *size_str = size_to_str(AX3005_DRAM_SIZE);

        error_report("Invalid RAM size, should be %s", size_str);
        exit(1);
    }

    s = AX3005_SOC(object_new_with_props(TYPE_AX3005_SOC, OBJECT(machine),
                                         "soc", &error_fatal, NULL));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);

    ax3005_binfo.ram_size = machine->ram_size;
    arm_load_kernel(&s->cpu[0], machine, &ax3005_binfo);
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
    mc->default_ram_size = AX3005_DRAM_SIZE;
}

static const TypeInfo ax3005_evk_types[] = {
    {
        .name          = TYPE_AX3005_MACHINE,
        .parent        = TYPE_MACHINE,
        .instance_size = sizeof(Ax3005MachineState),
        .class_init    = ax3005_evb_class_init,
    }
};

DEFINE_TYPES(ax3005_evk_types)
