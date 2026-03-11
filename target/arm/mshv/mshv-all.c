/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2026
 *
 * Authors: Aastha Rawat          <aastharawat@linux.microsoft.com>
 *          Anirudh Rayabharam    <anirudh@anirudhrb.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include "qemu/error-report.h"
#include "qemu/memalign.h"

#include "system/cpus.h"
#include "target/arm/cpu.h"

#include "system/mshv.h"
#include "system/mshv_int.h"
#include "hw/hyperv/hvgdk_mini.h"

static enum hv_register_name STANDARD_REGISTER_NAMES[32] = {
    HV_ARM64_REGISTER_X0,
    HV_ARM64_REGISTER_X1,
    HV_ARM64_REGISTER_X2,
    HV_ARM64_REGISTER_X3,
    HV_ARM64_REGISTER_X4,
    HV_ARM64_REGISTER_X5,
    HV_ARM64_REGISTER_X6,
    HV_ARM64_REGISTER_X7,
    HV_ARM64_REGISTER_X8,
    HV_ARM64_REGISTER_X9,
    HV_ARM64_REGISTER_X10,
    HV_ARM64_REGISTER_X11,
    HV_ARM64_REGISTER_X12,
    HV_ARM64_REGISTER_X13,
    HV_ARM64_REGISTER_X14,
    HV_ARM64_REGISTER_X15,
    HV_ARM64_REGISTER_X16,
    HV_ARM64_REGISTER_X17,
    HV_ARM64_REGISTER_X18,
    HV_ARM64_REGISTER_X19,
    HV_ARM64_REGISTER_X20,
    HV_ARM64_REGISTER_X21,
    HV_ARM64_REGISTER_X22,
    HV_ARM64_REGISTER_X23,
    HV_ARM64_REGISTER_X24,
    HV_ARM64_REGISTER_X25,
    HV_ARM64_REGISTER_X26,
    HV_ARM64_REGISTER_X27,
    HV_ARM64_REGISTER_X28,
    HV_ARM64_REGISTER_FP,
    HV_ARM64_REGISTER_LR,
    HV_ARM64_REGISTER_PC,
};

static int set_standard_regs(const CPUState *cpu)
{
    size_t n_regs = ARRAY_SIZE(STANDARD_REGISTER_NAMES);
    struct hv_register_assoc *assocs;
    int ret;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    assocs = g_new0(hv_register_assoc, n_regs);

    for (size_t i = 0; i < n_regs - 1; i++) {
        assocs[i].name = STANDARD_REGISTER_NAMES[i];
        assocs[i].value.reg64 = env->xregs[i];
    }

    /* Last register is the program counter */
    assocs[n_regs - 1].name = STANDARD_REGISTER_NAMES[n_regs - 1];
    assocs[n_regs - 1].value.reg64 = env->pc;

    ret = mshv_set_generic_regs(cpu, assocs, n_regs);
    if (ret < 0) {
        error_report("failed to set standard registers");
        g_free(assocs);
        return -1;
    }

    g_free(assocs);

    return 0;
}

static void populate_standard_regs(const hv_register_assoc *assocs,
                                   CPUARMState *env)
{
    size_t n_regs = ARRAY_SIZE(STANDARD_REGISTER_NAMES);

    for (size_t i = 0; i < n_regs - 1; i++) {
        env->xregs[i] = assocs[i].value.reg64;
    }

    /* Last register is the program counter */
    env->pc = assocs[n_regs - 1].value.reg64;
}

int mshv_load_regs(CPUState *cpu)
{
    int ret;

    ret = mshv_get_standard_regs(cpu);
    if (ret < 0) {
        error_report("Failed to load standard registers");
        return -1;
    }

    return 0;
}

int mshv_get_standard_regs(CPUState *cpu)
{
    size_t n_regs = ARRAY_SIZE(STANDARD_REGISTER_NAMES);
    struct hv_register_assoc *assocs;
    int ret;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    assocs = g_new0(hv_register_assoc, n_regs);
    for (size_t i = 0; i < n_regs; i++) {
        assocs[i].name = STANDARD_REGISTER_NAMES[i];
    }
    ret = mshv_get_generic_regs(cpu, assocs, n_regs);
    if (ret < 0) {
        error_report("failed to get standard registers");
        g_free(assocs);
        return -1;
    }

    populate_standard_regs(assocs, env);

    g_free(assocs);
    return 0;
}

int mshv_arch_put_registers(const CPUState *cpu)
{
    int ret;

    ret = set_standard_regs(cpu);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit)
{
    return 0;
}

void mshv_arch_init_vcpu(CPUState *cpu)
{
    AccelCPUState *state = cpu->accel;

    mshv_setup_hvcall_args(state);
}

void mshv_arch_destroy_vcpu(CPUState *cpu)
{
    AccelCPUState *state = cpu->accel;

    if (state->hvcall_args.base) {
        qemu_vfree(state->hvcall_args.base);
    }

    state->hvcall_args = (MshvHvCallArgs){0};
}

void mshv_init_mmio_emu(void)
{

}

void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features)
{

}

int mshv_arch_post_init_vm(int vm_fd)
{
    return 0;
}
