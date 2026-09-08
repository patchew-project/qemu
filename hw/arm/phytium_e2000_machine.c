/*
 * Phytium E2000 board models
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/block-backend-global-state.h"
#include "system/kvm.h"
#include "exec/hwaddr.h"
#include "hw/arm/boot.h"
#include "hw/arm/bsa.h"
#include "hw/arm/phytium_e2000.h"
#include "hw/core/boards.h"
#include "qom/object.h"
#include "target/arm/cpu-qom.h"

#define TYPE_PHYTIUM_PI MACHINE_TYPE_NAME("phytium-pi")
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumPiMachineState, PHYTIUM_PI)

struct PhytiumPiMachineState {
    MachineState parent_obj;

    struct arm_boot_info bootinfo;
    PhytiumE2000SoCState soc;
    MemoryRegion ram_low;
    MemoryRegion ram_high;
};

static BlockBackend *phytium_e2000_sd_blk(int index)
{
    DriveInfo *dinfo = drive_get(IF_SD, 0, index);

    return dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
}

static void phytium_e2000_attach_sd_cards(PhytiumPiMachineState *s)
{
    int i;

    for (i = 0; i < PHYTIUM_E2000_NUM_MCIS; i++) {
        phytium_e2000_soc_attach_sd_card(
            &s->soc, i, phytium_e2000_sd_blk(i));
    }
}

static void phytium_e2000_create_ram(PhytiumPiMachineState *s)
{
    MachineState *ms = MACHINE(s);
    uint64_t low_size =
        MIN(ms->ram_size, phytium_e2000_memmap[PHYTIUM_E2000_RAM].size);
    uint64_t high_size = ms->ram_size - low_size;

    /*
     * Keep the machine RAMBlock contiguous for migration, but expose it in
     * the two physical windows implemented by E2000. The high alias resumes
     * at low_size, so the intervening PCIe hole consumes no guest RAM.
     */
    memory_region_init_alias(&s->ram_low, OBJECT(s),
        "phytium-e2000.ram-low", ms->ram, 0, low_size);
    memory_region_add_subregion(get_system_memory(),
        phytium_e2000_memmap[PHYTIUM_E2000_RAM].base, &s->ram_low);

    if (!high_size) {
        return;
    }

    memory_region_init_alias(&s->ram_high, OBJECT(s),
        "phytium-e2000.ram-high", ms->ram, low_size, high_size);
    memory_region_add_subregion(get_system_memory(),
        phytium_e2000_memmap[PHYTIUM_E2000_RAM_HIGH].base, &s->ram_high);
}

static void phytium_pi_init(MachineState *ms)
{
    PhytiumPiMachineState *s = PHYTIUM_PI(ms);

    /*
     * KVM cannot provide the heterogeneous FTC CPU configuration applied to
     * the Cortex-A72 TCG cores. The firmware path also requires EL3 and
     * Phytium-specific system registers that KVM cannot provide.
     */
    if (kvm_enabled()) {
        error_report("phytium-pi: KVM is not supported");
        exit(1);
    }

    if (ms->smp.cpus > PHYTIUM_E2000_NUM_CPUS) {
        error_report("phytium-pi supports at most %d CPUs",
                     PHYTIUM_E2000_NUM_CPUS);
        exit(1);
    }

    if (ms->ram_size >
        phytium_e2000_memmap[PHYTIUM_E2000_RAM].size +
        phytium_e2000_memmap[PHYTIUM_E2000_RAM_HIGH].size) {
        error_report("phytium-pi supports at most 8 GiB RAM");
        exit(1);
    }

    phytium_e2000_create_ram(s);

    object_initialize_child(OBJECT(s), "soc", &s->soc,
                            TYPE_PHYTIUM_E2000_SOC);
    phytium_e2000_soc_configure(&s->soc, ms->smp.cpus);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);
    phytium_e2000_attach_sd_cards(s);

    s->bootinfo.ram_size = ms->ram_size;
    s->bootinfo.board_id = -1;
    s->bootinfo.loader_start =
        phytium_e2000_memmap[PHYTIUM_E2000_RAM].base;
    s->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    s->bootinfo.firmware_loaded = false;
    arm_load_kernel(phytium_e2000_soc_cpu(&s->soc, 0), ms, &s->bootinfo);
}

static const CPUArchIdList *phytium_e2000_possible_cpu_arch_ids(
    MachineState *ms)
{
    int i;

    if (ms->possible_cpus) {
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * ms->smp.max_cpus);
    ms->possible_cpus->len = ms->smp.max_cpus;

    for (i = 0; i < ms->possible_cpus->len; i++) {
        CPUArchId *cpu_slot = &ms->possible_cpus->cpus[i];

        cpu_slot->type = ARM_CPU_TYPE_NAME("cortex-a72");
        phytium_e2000_soc_cpu_topology(i, &cpu_slot->arch_id,
                                       &cpu_slot->props.cluster_id,
                                       &cpu_slot->props.core_id);
        cpu_slot->props.has_cluster_id = true;
        cpu_slot->props.has_core_id = true;
        cpu_slot->props.has_thread_id = true;
        cpu_slot->props.thread_id = 0;
    }

    return ms->possible_cpus;
}

static void phytium_pi_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a72"),
        NULL,
    };

    mc->init = phytium_pi_init;
    mc->desc = "Phytium Pi board (Phytium E2000Q)";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a72");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = PHYTIUM_E2000_NUM_CPUS;
    mc->default_cpus = PHYTIUM_E2000_NUM_CPUS;
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "phytium-e2000.ram";
    mc->minimum_page_bits = 12;
    mc->block_default_type = IF_SD;
    mc->no_cdrom = 1;
    mc->possible_cpu_arch_ids = phytium_e2000_possible_cpu_arch_ids;
}

static const TypeInfo phytium_pi_info = {
    .name = TYPE_PHYTIUM_PI,
    .parent = TYPE_MACHINE,
    .class_init = phytium_pi_class_init,
    .instance_size = sizeof(PhytiumPiMachineState),
};

static void phytium_pi_machine_init(void)
{
    type_register_static(&phytium_pi_info);
}

type_init(phytium_pi_machine_init);
