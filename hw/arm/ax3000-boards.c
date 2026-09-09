/*
 * Axiado Ax3000 Boards
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/arm/axiado-boards.h"
#include "hw/arm/boot.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qom/object.h"
#include "system/device_tree.h"

/*
 * arm_load_dtb() drops every /memory node found in the DTB and replaces them
 * with a single node spanning [loader_start, loader_start + ram_size).  The
 * AX3000 DRAM is split into two banks with the MMIO region in between, so the
 * generated node would describe unbacked addresses as RAM.  Describe the banks
 * ourselves instead.
 */
static void ax3000_modify_dtb(const struct arm_boot_info *info, void *fdt)
{
    g_autofree char *nodename = NULL;
    uint64_t reg[AX3000_NUM_BANKS * 2];
    char **node_path;
    Error *err = NULL;
    int i;

    node_path = qemu_fdt_node_unit_path(fdt, "memory", &err);
    if (err) {
        error_report_err(err);
        exit(1);
    }
    for (i = 0; node_path[i]; i++) {
        if (g_str_has_prefix(node_path[i], "/memory")) {
            qemu_fdt_nop_node(fdt, node_path[i]);
        }
    }
    g_strfreev(node_path);

    const struct {
        hwaddr addr;
        size_t size;
    } dram_table[] = {
        { AX3000_DRAM0_BASE, AX3000_DRAM0_SIZE},
        { AX3000_DRAM1_BASE, AX3000_DRAM1_SIZE}
    };

    for (i = 0; i < AX3000_NUM_BANKS; i++) {
        reg[i * 2] = cpu_to_be64(dram_table[i].addr);
        reg[i * 2 + 1] = cpu_to_be64(dram_table[i].size);
    }

    nodename = g_strdup_printf("/memory@%" HWADDR_PRIx, dram_table[0].addr);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    qemu_fdt_setprop(fdt, nodename, "reg", reg, sizeof(reg));
}

static struct arm_boot_info ax3000_binfo = {
    .loader_start = AX3000_DRAM0_BASE,
    .board_id = -1,
    .modify_dtb = ax3000_modify_dtb,
};

static void ax3000_machine_init(MachineState *machine)
{
    Ax3000SoCState *s;

    if (machine->ram_size != AX3000_DRAM_SIZE) {
        g_autofree char *size_str = size_to_str(AX3000_DRAM_SIZE);

        error_report("Invalid RAM size, should be %s", size_str);
        exit(1);
    }

    s = AX3000_SOC(object_new_with_props(TYPE_AX3000_SOC, OBJECT(machine),
                                         "soc", &error_fatal, NULL));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);

    ax3000_binfo.ram_size = machine->ram_size;
    arm_load_kernel(&s->cpu[0], machine, &ax3000_binfo);
}

static void ax3000_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = ax3000_machine_init;
    mc->default_cpus = AX3000_NUM_CPUS;
    mc->min_cpus = AX3000_NUM_CPUS;
    mc->max_cpus = AX3000_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->default_ram_size = AX3000_DRAM_SIZE;
}

static const TypeInfo ax3000_machine_types[] = {
    {
        .name          = TYPE_AX3000_MACHINE,
        .parent        = TYPE_MACHINE,
        .instance_size = sizeof(Ax3000MachineState),
        .class_size    = sizeof(Ax3000MachineClass),
        .class_init    = ax3000_machine_class_init,
        .abstract      = true,
    }
};

DEFINE_TYPES(ax3000_machine_types)
