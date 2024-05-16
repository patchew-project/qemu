/*
 * QEMU Gunyah hypervisor support
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/gunyah.h"
#include "sysemu/gunyah_int.h"
#include "linux-headers/linux/gunyah.h"
#include "exec/memory.h"
#include "sysemu/device_tree.h"
#include "hw/arm/fdt.h"

/*
 * Specify location of device-tree in guest address space.
 *
 * @dtb_start - Guest physical address where VM's device-tree is found
 * @dtb_size - Size of device-tree (and any free space after it).
 *
 * RM or Resource Manager VM is a trusted and privileged VM that works in
 * collaboration with Gunyah hypevisor to setup resources for a VM before it can
 * begin execution. One of its functions includes inspection/modification of a
 * VM's device-tree before VM begins its execution. Modification can
 * include specification of runtime resources allocated by hypervisor,
 * details of which needs to be visible to VM.  VM's device-tree is modified
 * "inline" making use of "free" space that could exist at the end of device
 * tree.
 */
int gunyah_arm_set_dtb(uint64_t dtb_start, uint64_t dtb_size)
{
    int ret;
    struct gh_vm_dtb_config dtb;

    dtb.guest_phys_addr = dtb_start;
    dtb.size = dtb_size;

    ret = gunyah_vm_ioctl(GH_VM_SET_DTB_CONFIG, &dtb);
    if (ret != 0) {
        error_report("GH_VM_SET_DTB_CONFIG failed: %s", strerror(errno));
        exit(1);
    }

    return 0;
}

void gunyah_arm_fdt_customize(void *fdt, uint64_t mem_base,
            uint32_t gic_phandle)
{
    char *nodename;
    int i;
    GUNYAHState *state = get_gunyah_state();

    qemu_fdt_add_subnode(fdt, "/gunyah-vm-config");
    qemu_fdt_setprop_string(fdt, "/gunyah-vm-config",
                                "image-name", "qemu-vm");
    qemu_fdt_setprop_string(fdt, "/gunyah-vm-config", "os-type", "linux");

    nodename = g_strdup_printf("/gunyah-vm-config/memory");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, nodename, "#size-cells", 2);
    qemu_fdt_setprop_u64(fdt, nodename, "base-address", mem_base);

    g_free(nodename);

    nodename = g_strdup_printf("/gunyah-vm-config/interrupts");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "config", gic_phandle);
    g_free(nodename);

    nodename = g_strdup_printf("/gunyah-vm-config/vcpus");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "affinity", "proxy");
    g_free(nodename);

    nodename = g_strdup_printf("/gunyah-vm-config/vdevices");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "generate", "/hypervisor");
    g_free(nodename);

    for (i = 0; i < state->nr_slots; ++i) {
        if (!state->slots[i].start || state->slots[i].lend ||
                state->slots[i].start == mem_base) {
            continue;
        }

        nodename = g_strdup_printf("/gunyah-vm-config/vdevices/shm-%x", i);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "vdevice-type", "shm");
        qemu_fdt_setprop_string(fdt, nodename, "push-compatible", "dma");
        qemu_fdt_setprop(fdt, nodename, "peer-default", NULL, 0);
        qemu_fdt_setprop_u64(fdt, nodename, "dma_base", 0);
        g_free(nodename);

        nodename = g_strdup_printf("/gunyah-vm-config/vdevices/shm-%x/memory",
                                                                        i);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_cell(fdt, nodename, "label", i);
        qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 2);
        qemu_fdt_setprop_u64(fdt, nodename, "base", state->slots[i].start);
        g_free(nodename);
    }

    for (i = 0; i < state->nr_irqs; ++i) {
        nodename = g_strdup_printf("/gunyah-vm-config/vdevices/bell-%x", i);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "vdevice-type", "doorbell");
        char *p = g_strdup_printf("/hypervisor/bell-%x", i);
        qemu_fdt_setprop_string(fdt, nodename, "generate", p);
        g_free(p);
        qemu_fdt_setprop_cell(fdt, nodename, "label", i);
        qemu_fdt_setprop(fdt, nodename, "peer-default", NULL, 0);
        qemu_fdt_setprop(fdt, nodename, "source-can-clear", NULL, 0);

        qemu_fdt_setprop_cells(fdt, nodename, "interrupts",
                GIC_FDT_IRQ_TYPE_SPI, i, GIC_FDT_IRQ_FLAGS_LEVEL_HI);

        g_free(nodename);
    }
}
