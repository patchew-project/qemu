/*
 * NXP i.MX 95 19x19 Evaluation Kit (LPDDR5) - QEMU machine
 *
 * Modeled on hw/arm/imx8mp-evk.c by Bernhard Beschow
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Instantiates the SoC, attaches DDR, and hands control to
 * arm_load_kernel() so -kernel works.
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/arm/boot.h"
#include "hw/arm/fsl-imx95.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "system/kvm.h"
#include "system/qtest.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

static void imx95_evk_init(MachineState *machine)
{
    static struct arm_boot_info boot_info;
    FslImx95State *s;

    if (kvm_enabled()) {
        error_report("The imx95-19x19-evk machine requires TCG: it emulates "
                     "Cortex-M33 and Cortex-M7 cores that KVM cannot run");
        exit(1);
    }

    if (machine->ram_size > FSL_IMX95_RAM_SIZE_MAX) {
        error_report("RAM size " RAM_ADDR_FMT
                     " above max supported (0x%" PRIx64 ")",
                     machine->ram_size, (uint64_t)FSL_IMX95_RAM_SIZE_MAX);
        exit(1);
    }

    /*
     * The board presets the DTB at RAM_START + 128 MiB and the initrd at
     * RAM_START + 256 MiB (see below), so require enough RAM to hold them.
     */
    if (machine->ram_size < FSL_IMX95_RAM_SIZE_MIN) {
        error_report("RAM size " RAM_ADDR_FMT
                     " below min supported (0x%" PRIx64 ")",
                     machine->ram_size, (uint64_t)FSL_IMX95_RAM_SIZE_MIN);
        exit(1);
    }

    boot_info = (struct arm_boot_info) {
        .loader_start = FSL_IMX95_RAM_START,
        .board_id     = -1,
        .ram_size     = machine->ram_size,
        .psci_conduit = QEMU_PSCI_CONDUIT_SMC,
        /*
         * The default arm_load_kernel() heuristic lands the initrd (and the
         * DTB right after it) at loader_start + 128 MiB == 0x88000000. That
         * is exactly where the NXP BSP device tree places the Cortex-M7
         * remoteproc carveout (vdev vrings + rsc-table @0x88220000). The
         * collision makes Linux reserve those pages for the initrd/FDT, so
         * the later reserved-memory no-map pass fails (-EBUSY) and imx-rproc
         * cannot ioremap the rsc-table (probe fails with -ENOMEM). Force the
         * initrd/DTB up to 0x90000000, in the free gap above the M7 carveout
         * and below the GPU/VPU carveouts at 0xa0000000.
         */
        .initrd_start = FSL_IMX95_RAM_START + 256 * MiB,
    };

    s = FSL_IMX95(object_new(TYPE_FSL_IMX95));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(s));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);

    memory_region_add_subregion(get_system_memory(), FSL_IMX95_RAM_START,
                                machine->ram);

    if (!qtest_enabled()) {
        arm_load_kernel(&s->cpu[0], machine, &boot_info);

        /*
         * arm_load_kernel() registers its boot reset hook on every CPU in
         * the system and treats all non-boot cores as A-profile PSCI
         * secondaries. That is correct for the five A55 secondaries, but
         * the Cortex-M33 SM core and the Cortex-M7 RT core are not part of
         * the A55 boot flow - each boots from its own ITCM vector table.
         * Detach them from the A-profile boot machinery so their reset just
         * runs the normal M-profile vector-table reset. Whether each then
         * actually runs is decided by the SoC's M33/M7 reset hooks (only if
         * firmware was loaded into the respective ITCM).
         */
        if (s->m33.cpu) {
            s->m33.cpu->env.boot_info = NULL;
        }
        if (s->m7.cpu) {
            s->m7.cpu->env.boot_info = NULL;
        }
    }
}

static const char *imx95_evk_get_default_cpu_type(const MachineState *ms)
{
    return ARM_CPU_TYPE_NAME("cortex-a55");
}

static const char * const imx95_evk_valid_cpu_types[] = {
    ARM_CPU_TYPE_NAME("cortex-a55"),
    NULL
};

static void imx95_19x19_evk_machine_init(MachineClass *mc)
{
    mc->desc                  = "NXP i.MX 95 19x19 EVK (LPDDR5)";
    mc->init                  = imx95_evk_init;
    /*
     * Total vCPUs = 6 A55 + 1 Cortex-M33 System Manager core + 1 Cortex-M7
     * real-time core that the SoC always instantiates. TCG sizes its
     * per-CPU context table from the resolved smp.max_cpus, which defaults
     * to smp.cpus when -smp is not given - so both the default and the
     * max must include the M33 and the M7, otherwise the 8th CPU's
     * tcg_register_thread() asserts. The A55 cluster size is fixed in
     * the SoC regardless of -smp; this count is really "A55 cluster +
     * SM core + RT core".
     */
    mc->default_cpus          = FSL_IMX95_NUM_A55_CPUS + 2;
    mc->max_cpus              = FSL_IMX95_NUM_A55_CPUS + 2;
    mc->default_ram_id        = "imx95-19x19-evk.ram";
    mc->default_ram_size      = 8 * GiB;   /* 19x19 EVK has 8 GiB LPDDR5 */
    mc->get_default_cpu_type  = imx95_evk_get_default_cpu_type;
    mc->valid_cpu_types       = imx95_evk_valid_cpu_types;
}

DEFINE_MACHINE_AARCH64("imx95-19x19-evk", imx95_19x19_evk_machine_init)
