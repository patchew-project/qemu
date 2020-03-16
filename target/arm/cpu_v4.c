/*
 * ARM generic helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"

/* CPU models. These are not needed for the AArch64 linux-user build. */
#if !defined(CONFIG_USER_ONLY) || !defined(TARGET_AARCH64)

static void ti925t_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    set_feature(&cpu->env, ARM_FEATURE_V4T);
    set_feature(&cpu->env, ARM_FEATURE_OMAPCP);
    cpu->midr = ARM_CPUID_TI925T;
    cpu->ctr = 0x5109149;
    cpu->reset_sctlr = 0x00000070;
}

static void sa1100_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "intel,sa1100";
    set_feature(&cpu->env, ARM_FEATURE_STRONGARM);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    cpu->midr = 0x4401A11B;
    cpu->reset_sctlr = 0x00000070;
}

static void sa1110_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    set_feature(&cpu->env, ARM_FEATURE_STRONGARM);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    cpu->midr = 0x6901B119;
    cpu->reset_sctlr = 0x00000070;
}

static const ARMCPUInfo arm_v4_cpus[] = {
    { .name = "ti925t",      .initfn = ti925t_initfn },
    { .name = "sa1100",      .initfn = sa1100_initfn },
    { .name = "sa1110",      .initfn = sa1110_initfn },
    { .name = NULL }
};

static void arm_v4_cpu_register_types(void)
{
    const ARMCPUInfo *info = arm_v4_cpus;

    while (info->name) {
        arm_cpu_register(info);
        info++;
    }
}

type_init(arm_v4_cpu_register_types)

#endif
