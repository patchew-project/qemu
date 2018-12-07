/*
 * ARM SBSA Reference Platform emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Hongbo Zhang <hongbo.zhang@linaro.org>
 *
 * Based on hw/arm/virt.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/arm.h"
#include "hw/arm/virt.h"
#include "hw/devices.h"
#include "net/net.h"
#include "sysemu/device_tree.h"
#include "sysemu/numa.h"
#include "sysemu/sysemu.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/pci-host/gpex.h"
#include "hw/arm/sysbus-fdt.h"
#include "hw/arm/fdt.h"
#include "hw/intc/arm_gic.h"
#include "hw/intc/arm_gicv3_common.h"
#include "kvm_arm.h"
#include "hw/ide/internal.h"
#include "hw/ide/ahci_internal.h"
#include "qemu/units.h"

#define NUM_IRQS 256

#define RAMLIMIT_GB 8192
#define RAMLIMIT_BYTES (RAMLIMIT_GB * GiB)

static const MemMapEntry sbsa_ref_memmap[] = {
    /* 512M boot ROM */
    [VIRT_FLASH] =              {          0, 0x20000000 },
    /* 512M secure memery */
    [VIRT_SECURE_MEM] =         { 0x20000000, 0x20000000 },
    [VIRT_CPUPERIPHS] =         { 0x40000000, 0x00080000 },
    /* GIC distributor and CPU interface expansion spaces reserved */
    [VIRT_GIC_DIST] =           { 0x40000000, 0x00010000 },
    [VIRT_GIC_CPU] =            { 0x40040000, 0x00010000 },
    /* 64M redistributor space allows up to 512 CPUs */
    [VIRT_GIC_REDIST] =         { 0x40080000, 0x04000000 },
    /* Space here reserved for redistributor and vCPU/HYP expansion */
    [VIRT_UART] =               { 0x60000000, 0x00001000 },
    [VIRT_RTC] =                { 0x60010000, 0x00001000 },
    [VIRT_GPIO] =               { 0x60020000, 0x00001000 },
    [VIRT_SECURE_UART] =        { 0x60030000, 0x00001000 },
    [VIRT_SMMU] =               { 0x60040000, 0x00020000 },
    /* Space here reserved for more SMMUs */
    [VIRT_AHCI] =               { 0x60100000, 0x00010000 },
    /* Space here reserved for other devices */
    [VIRT_PCIE_PIO] =           { 0x7fff0000, 0x00010000 },
    /* 256M PCIE ECAM space */
    [VIRT_PCIE_ECAM] =          { 0x80000000, 0x10000000 },
    /* ~1TB for PCIE MMIO (4GB to 1024GB boundary) */
    [VIRT_PCIE_MMIO] =          { 0x100000000ULL, 0xFF00000000ULL },
    [VIRT_MEM] =                { 0x10000000000ULL, RAMLIMIT_BYTES },
};

static const int sbsa_ref_irqmap[] = {
    [VIRT_UART] = 1,
    [VIRT_RTC] = 2,
    [VIRT_PCIE] = 3, /* ... to 6 */
    [VIRT_GPIO] = 7,
    [VIRT_SECURE_UART] = 8,
    [VIRT_AHCI] = 9,
};

static void sbsa_ref_init(MachineState *machine)
{
    VirtMachineState *vms = VIRT_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *secure_sysmem = NULL;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    bool firmware_loaded = bios_name || drive_get(IF_PFLASH, 0, 0);
    const CPUArchIdList *possible_cpus;
    int n, sbsa_max_cpus;

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a57"))) {
        error_report("sbsa-ref: CPU type other than the built-in "
                     "cortex-a57 not supported");
        exit(1);
    }

    if (kvm_enabled()) {
        error_report("sbsa-ref: KVM is not supported at this machine");
        exit(1);
    }

    if (machine->kernel_filename && firmware_loaded) {
        error_report("sbsa-ref: No fw_cfg device on this machine, "
                     "so -kernel option is not supported when firmware loaded, "
                     "please load OS from hard disk instead");
        exit(1);
    }

    /* This machine has EL3 enabled, external firmware should supply PSCI
     * implementation, so the QEMU's internal PSCI is disabled.
     */
    vms->psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;

    sbsa_max_cpus = vms->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE;

    if (max_cpus > sbsa_max_cpus) {
        error_report("Number of SMP CPUs requested (%d) exceeds max CPUs "
                     "supported by machine 'sbsa-ref' (%d)",
                     max_cpus, sbsa_max_cpus);
        exit(1);
    }

    vms->smp_cpus = smp_cpus;

    if (machine->ram_size > vms->memmap[VIRT_MEM].size) {
        error_report("sbsa-ref: cannot model more than %dGB RAM", RAMLIMIT_GB);
        exit(1);
    }

    secure_sysmem = g_new(MemoryRegion, 1);
    memory_region_init(secure_sysmem, OBJECT(machine), "secure-memory",
                       UINT64_MAX);
    memory_region_add_subregion_overlap(secure_sysmem, 0, sysmem, -1);

    possible_cpus = mc->possible_cpu_arch_ids(machine);
    for (n = 0; n < possible_cpus->len; n++) {
        Object *cpuobj;
        CPUState *cs;

        if (n >= smp_cpus) {
            break;
        }

        cpuobj = object_new(possible_cpus->cpus[n].type);
        object_property_set_int(cpuobj, possible_cpus->cpus[n].arch_id,
                                "mp-affinity", NULL);

        cs = CPU(cpuobj);
        cs->cpu_index = n;

        numa_cpu_pre_plug(&possible_cpus->cpus[cs->cpu_index], DEVICE(cpuobj),
                          &error_fatal);

        if (object_property_find(cpuobj, "reset-cbar", NULL)) {
            object_property_set_int(cpuobj, vms->memmap[VIRT_CPUPERIPHS].base,
                                    "reset-cbar", &error_abort);
        }

        object_property_set_link(cpuobj, OBJECT(sysmem), "memory",
                                 &error_abort);

        object_property_set_link(cpuobj, OBJECT(secure_sysmem),
                                 "secure-memory", &error_abort);

        object_property_set_bool(cpuobj, true, "realized", &error_fatal);
        object_unref(cpuobj);
    }

    memory_region_allocate_system_memory(ram, NULL, "sbsa-ref.ram",
                                         machine->ram_size);
    memory_region_add_subregion(sysmem, vms->memmap[VIRT_MEM].base, ram);

    vms->bootinfo.ram_size = machine->ram_size;
    vms->bootinfo.kernel_filename = machine->kernel_filename;
    vms->bootinfo.nb_cpus = smp_cpus;
    vms->bootinfo.board_id = -1;
    vms->bootinfo.loader_start = vms->memmap[VIRT_MEM].base;
    vms->bootinfo.firmware_loaded = firmware_loaded;
    arm_load_kernel(ARM_CPU(first_cpu), &vms->bootinfo);
}

static uint64_t sbsa_ref_cpu_mp_affinity(VirtMachineState *vms, int idx)
{
    uint8_t clustersz = ARM_DEFAULT_CPUS_PER_CLUSTER;
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);

    if (!vmc->disallow_affinity_adjustment) {
        clustersz = GICV3_TARGETLIST_BITS;
    }
    return arm_cpu_mp_affinity(idx, clustersz);
}

static const CPUArchIdList *sbsa_ref_possible_cpu_arch_ids(MachineState *ms)
{
    VirtMachineState *vms = VIRT_MACHINE(ms);
    int n;

    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        ms->possible_cpus->cpus[n].type = ms->cpu_type;
        ms->possible_cpus->cpus[n].arch_id =
            sbsa_ref_cpu_mp_affinity(vms, n);
        ms->possible_cpus->cpus[n].props.has_thread_id = true;
        ms->possible_cpus->cpus[n].props.thread_id = n;
    }
    return ms->possible_cpus;
}

static CpuInstanceProperties
sbsa_ref_cpu_index_to_props(MachineState *ms, unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static int64_t
sbsa_ref_get_default_cpu_node_id(const MachineState *ms, int idx)
{
    return idx % nb_numa_nodes;
}

static void sbsa_ref_instance_init(Object *obj)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->memmap = sbsa_ref_memmap;
    vms->irqmap = sbsa_ref_irqmap;
}

static void sbsa_ref_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = sbsa_ref_init;
    mc->desc = "QEMU 'SBSA Reference' ARM Virtual Machine";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a57");
    mc->max_cpus = 512;
    mc->pci_allow_0_address = true;
    mc->minimum_page_bits = 12;
    mc->block_default_type = IF_IDE;
    mc->no_cdrom = 1;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = 4;
    mc->possible_cpu_arch_ids = sbsa_ref_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = sbsa_ref_cpu_index_to_props;
    mc->get_default_cpu_node_id = sbsa_ref_get_default_cpu_node_id;
}

static const TypeInfo sbsa_ref_info = {
    .name          = MACHINE_TYPE_NAME("sbsa-ref"),
    .parent        = TYPE_VIRT_MACHINE,
    .instance_init = sbsa_ref_instance_init,
    .class_init    = sbsa_ref_class_init,
};

static void sbsa_ref_machine_init(void)
{
    type_register_static(&sbsa_ref_info);
}

type_init(sbsa_ref_machine_init);
