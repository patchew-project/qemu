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
#include "system/device_tree.h"
#include "system/kvm.h"
#include "exec/hwaddr.h"
#include "hw/arm/boot.h"
#include "hw/arm/bsa.h"
#include "hw/arm/phytium_e2000.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"
#include "target/arm/cpu-qom.h"

#define TYPE_PHYTIUM_E2000_MACHINE \
    MACHINE_TYPE_NAME("phytium-e2000-base")
OBJECT_DECLARE_TYPE(PhytiumE2000MachineState, PhytiumE2000MachineClass,
                    PHYTIUM_E2000_MACHINE)

#define TYPE_PHYTIUM_PI         MACHINE_TYPE_NAME("phytium-pi")
#define TYPE_PHYTIUM_E2000_COME MACHINE_TYPE_NAME("phytium-e2000-come")

struct PhytiumE2000MachineState {
    MachineState parent_obj;

    struct arm_boot_info bootinfo;
    PhytiumE2000SoCState soc;
    MemoryRegion ram_low;
    MemoryRegion ram_high;
};

struct PhytiumE2000MachineClass {
    MachineClass parent_class;

    const char *machine_name;
    const char *direct_boot_dtb;
    const char *pbr_boot_mode;
    const char *qspi_flash_model;
};

static BlockBackend *phytium_e2000_drive_blk(BlockInterfaceType type,
                                             int index)
{
    DriveInfo *dinfo = drive_get(type, 0, index);

    return dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
}

static void phytium_e2000_attach_sd_cards(PhytiumE2000MachineState *s)
{
    int i;

    for (i = 0; i < PHYTIUM_E2000_NUM_MCIS; i++) {
        phytium_e2000_soc_attach_sd_card(
            &s->soc, i, phytium_e2000_drive_blk(IF_SD, i));
    }
}

static void phytium_e2000_attach_qspi_flash(
    PhytiumE2000MachineState *s, const char *model)
{
    DeviceState *flash = qdev_new(model);
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);

    object_property_add_child(OBJECT(s), "qspi-flash", OBJECT(flash));
    if (dinfo) {
        qdev_prop_set_drive_err(flash, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }
    phytium_e2000_soc_attach_qspi_flash(&s->soc, flash);
}

static void phytium_e2000_reject_legacy_firmware(
    MachineState *ms, PhytiumE2000MachineClass *pemc)
{
    if (ms->firmware || drive_get(IF_PFLASH, 0, 0)) {
        error_report("%s: -bios and pflash firmware are not supported; "
                     "use an if=%s,index=0 image",
                     pemc->machine_name,
                     !strcmp(pemc->pbr_boot_mode,
                             PHYTIUM_E2000_PBR_BOOT_MODE_QSPI) ?
                         "mtd" : "sd");
        exit(1);
    }
}

static void phytium_e2000_create_ram(PhytiumE2000MachineState *s)
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

static void phytium_e2000_add_memory_node(void *fdt, hwaddr base,
                                          uint64_t size)
{
    uint32_t acells;
    uint32_t scells;
    g_autofree char *name = g_strdup_printf("/memory@%" HWADDR_PRIx, base);

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells",
                                   NULL, &error_fatal);
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells",
                                   NULL, &error_fatal);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 acells, base, scells, size);
}

static void phytium_e2000_modify_dtb(const struct arm_boot_info *info,
                                     void *fdt)
{
    uint64_t low_size =
        MIN(info->ram_size, phytium_e2000_memmap[PHYTIUM_E2000_RAM].size);
    uint64_t high_size = info->ram_size - low_size;
    g_auto(GStrv) memory_nodes = NULL;
    Error *err = NULL;
    int i;

    if (!high_size) {
        return;
    }

    /*
     * arm_load_dtb() normally describes RAM as one range beginning at
     * loader_start. E2000 RAM above 2 GiB is instead mapped at 0x2000000000,
     * beyond the PCIe aperture. Replace the generic range so Linux never
     * treats the intervening address-space hole as RAM.
     */
    memory_nodes = qemu_fdt_node_unit_path(fdt, "memory", &err);
    if (err) {
        error_report_err(err);
        exit(1);
    }
    for (i = 0; memory_nodes[i]; i++) {
        if (g_str_has_prefix(memory_nodes[i], "/memory")) {
            qemu_fdt_nop_node(fdt, memory_nodes[i]);
        }
    }

    phytium_e2000_add_memory_node(
        fdt, phytium_e2000_memmap[PHYTIUM_E2000_RAM].base, low_size);
    phytium_e2000_add_memory_node(
        fdt, phytium_e2000_memmap[PHYTIUM_E2000_RAM_HIGH].base, high_size);
}

static void phytium_e2000_init(MachineState *ms)
{
    PhytiumE2000MachineState *s = PHYTIUM_E2000_MACHINE(ms);
    PhytiumE2000MachineClass *pemc =
        PHYTIUM_E2000_MACHINE_GET_CLASS(ms);
    BlockBackend *boot_blk;
    bool firmware_loaded;

    /*
     * KVM cannot provide the heterogeneous FTC CPU configuration applied to
     * the Cortex-A72 TCG cores. The firmware path also requires EL3 and
     * Phytium-specific system registers that KVM cannot provide.
     */
    if (kvm_enabled()) {
        error_report("%s: KVM is not supported", pemc->machine_name);
        exit(1);
    }

    if (ms->kernel_filename && !ms->dtb) {
        error_report("%s: direct Linux boot requires the SDK %s via -dtb",
                     pemc->machine_name, pemc->direct_boot_dtb);
        exit(1);
    }

    if (ms->smp.cpus > PHYTIUM_E2000_NUM_CPUS) {
        error_report("%s supports at most %d CPUs", pemc->machine_name,
                     PHYTIUM_E2000_NUM_CPUS);
        exit(1);
    }

    if (ms->ram_size >
        phytium_e2000_memmap[PHYTIUM_E2000_RAM].size +
        phytium_e2000_memmap[PHYTIUM_E2000_RAM_HIGH].size) {
        error_report("%s supports at most 8 GiB RAM", pemc->machine_name);
        exit(1);
    }

    phytium_e2000_reject_legacy_firmware(ms, pemc);
    phytium_e2000_create_ram(s);
    if (!ms->kernel_filename &&
        !strcmp(pemc->pbr_boot_mode, PHYTIUM_E2000_PBR_BOOT_MODE_SD0)) {
        boot_blk = phytium_e2000_drive_blk(IF_SD, 0);
    } else if (!ms->kernel_filename &&
               !strcmp(pemc->pbr_boot_mode,
                       PHYTIUM_E2000_PBR_BOOT_MODE_QSPI)) {
        boot_blk = phytium_e2000_drive_blk(IF_MTD, 0);
    } else {
        boot_blk = NULL;
    }

    object_initialize_child(OBJECT(s), "soc", &s->soc,
                            TYPE_PHYTIUM_E2000_SOC);
    phytium_e2000_soc_configure(&s->soc, pemc->pbr_boot_mode,
                                boot_blk, ms->ram_size, ms->smp.cpus);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);
    phytium_e2000_attach_sd_cards(s);
    if (pemc->qspi_flash_model) {
        phytium_e2000_attach_qspi_flash(s, pemc->qspi_flash_model);
    }

    firmware_loaded = phytium_e2000_soc_firmware_loaded(&s->soc);
    s->bootinfo.ram_size = ms->ram_size;
    s->bootinfo.board_id = -1;
    s->bootinfo.loader_start =
        phytium_e2000_memmap[PHYTIUM_E2000_RAM].base;
    s->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    s->bootinfo.firmware_loaded = firmware_loaded;
    s->bootinfo.modify_dtb = phytium_e2000_modify_dtb;
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

static void phytium_e2000_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a72"),
        NULL,
    };

    mc->init = phytium_e2000_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a72");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = PHYTIUM_E2000_NUM_CPUS;
    mc->default_cpus = PHYTIUM_E2000_NUM_CPUS;
    /*
     * PBF/BL1 relocates to 0xf8c40000, which is outside a 1 GiB RAM window
     * starting at 0x80000000. Two GiB is the minimum useful firmware default.
     */
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "phytium-e2000.ram";
    mc->minimum_page_bits = 12;
    mc->no_cdrom = 1;
    mc->possible_cpu_arch_ids = phytium_e2000_possible_cpu_arch_ids;
}

static void phytium_pi_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PhytiumE2000MachineClass *pemc =
        PHYTIUM_E2000_MACHINE_CLASS(oc);

    mc->desc = "Phytium Pi board (Phytium E2000Q)";
    mc->block_default_type = IF_SD;
    pemc->machine_name = "phytium-pi";
    pemc->direct_boot_dtb = "phytiumpi_firefly.dtb";
    pemc->pbr_boot_mode = PHYTIUM_E2000_PBR_BOOT_MODE_SD0;
}

static void phytium_e2000_come_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PhytiumE2000MachineClass *pemc =
        PHYTIUM_E2000_MACHINE_CLASS(oc);

    mc->desc = "Phytium E2000Q COMe Development Board";
    pemc->machine_name = "phytium-e2000-come";
    pemc->direct_boot_dtb = "e2000q-come-board.dtb";
    pemc->pbr_boot_mode = PHYTIUM_E2000_PBR_BOOT_MODE_QSPI;
    pemc->qspi_flash_model = "gd25q128";
}

static const TypeInfo phytium_e2000_base_info = {
    .name = TYPE_PHYTIUM_E2000_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .class_init = phytium_e2000_class_init,
    .class_size = sizeof(PhytiumE2000MachineClass),
    .instance_size = sizeof(PhytiumE2000MachineState),
};

static const TypeInfo phytium_pi_info = {
    .name = TYPE_PHYTIUM_PI,
    .parent = TYPE_PHYTIUM_E2000_MACHINE,
    .class_init = phytium_pi_class_init,
};

static const TypeInfo phytium_e2000_come_info = {
    .name = TYPE_PHYTIUM_E2000_COME,
    .parent = TYPE_PHYTIUM_E2000_MACHINE,
    .class_init = phytium_e2000_come_class_init,
};

static void phytium_e2000_machine_init(void)
{
    type_register_static(&phytium_e2000_base_info);
    type_register_static(&phytium_pi_info);
    type_register_static(&phytium_e2000_come_info);
}

type_init(phytium_e2000_machine_init);
