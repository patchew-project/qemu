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
#include "hw/arm/bsa.h"
#include "hw/arm/virt.h"

#include "system/cpus.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"
#include "target/arm/mshv_arm.h"

#include "system/mshv.h"
#include "system/mshv_int.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "hw/hyperv/hvhdk_mini.h"

typedef struct ARMHostCPUFeatures {
    ARMISARegisters isar;
    uint64_t features;
    uint64_t midr;
    uint32_t reset_sctlr;
    const char *dtb_compatible;
} ARMHostCPUFeatures;

static ARMHostCPUFeatures arm_host_cpu_features;

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

void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features)
{

}

int mshv_arch_post_init_vm(int vm_fd)
{
    return 0;
}

static uint32_t mshv_arm_get_ipa_bit_size(int mshv_fd)
{
    int ret;
    struct hv_input_get_partition_property in = {0};
    struct hv_output_get_partition_property out = {0};
    struct mshv_root_hvcall args = {0};

    in.property_code = HV_PARTITION_PROPERTY_PHYSICAL_ADDRESS_WIDTH;

    args.code = HVCALL_GET_PARTITION_PROPERTY;
    args.in_sz = sizeof(in);
    args.in_ptr = (uint64_t)&in;
    args.out_sz = sizeof(out);
    args.out_ptr = (uint64_t)&out;

    ret = mshv_hvcall(mshv_fd, &args);

    if (ret < 0) {
        error_report("Failed to get IPA size");
        exit(1);
    }

    return out.property_value;
}

static void clamp_id_aa64mmfr0_parange_to_ipa_size(int mshv_fd, ARMISARegisters *isar)
{
    uint32_t ipa_size = mshv_arm_get_ipa_bit_size(mshv_fd);
    uint64_t id_aa64mmfr0;

    /* Clamp down the PARange to the IPA size the kernel supports. */
    uint8_t index = round_down_to_parange_index(ipa_size);
    id_aa64mmfr0 = GET_IDREG(isar, ID_AA64MMFR0);
    id_aa64mmfr0 = (id_aa64mmfr0 & ~R_ID_AA64MMFR0_PARANGE_MASK) | index;
    SET_IDREG(isar, ID_AA64MMFR0, id_aa64mmfr0);
}

static int mshv_get_partition_regs(int vm_fd, hv_register_name *names,
                             hv_register_value *values, size_t n_regs)
{
    int ret = 0;
    size_t in_sz, names_sz, values_sz;
    void *in_buffer = qemu_memalign(HV_HYP_PAGE_SIZE, HV_HYP_PAGE_SIZE);
    void *out_buffer = qemu_memalign(HV_HYP_PAGE_SIZE, HV_HYP_PAGE_SIZE);
    hv_input_get_vp_registers *in = in_buffer;

    struct mshv_root_hvcall args = {0};

    names_sz = n_regs * sizeof(hv_register_name);
    in_sz = sizeof(hv_input_get_vp_registers) + names_sz;

    memset(in, 0, HV_HYP_PAGE_SIZE);

    in->vp_index = HV_ANY_VP;
    in->input_vtl.target_vtl = HV_VTL_ALL;
    in->input_vtl.use_target_vtl = 1;

    for (int i = 0; i < n_regs; i++) {
        in->names[i] = names[i];
    }

    values_sz = n_regs * sizeof(hv_register_value);

    args.code = HVCALL_GET_VP_REGISTERS;
    args.in_sz = in_sz;
    args.in_ptr = (uintptr_t)(in_buffer);
    args.out_sz = values_sz;
    args.out_ptr = (uintptr_t)(out_buffer);
    args.reps =  (uint16_t) n_regs;

    ret = mshv_hvcall(vm_fd, &args);

    if (ret == 0) {
        memcpy(values, out_buffer, values_sz);
    }

    qemu_vfree(in_buffer);
    qemu_vfree(out_buffer);

    return ret;
}

static bool mshv_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf)
{
    int mshv_fd = mshv_state->fd;
    int vm_fd = mshv_state->vm;
    int i, ret;
    bool success = true;
    uint64_t pfr0, pfr1;
    gchar *contents = NULL;

    const struct {
        hv_register_name name;
        int isar_idx;
    } regs[] = {
        { HV_ARM64_REGISTER_ID_AA64_PFR0_EL1,  ID_AA64PFR0_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_PFR1_EL1,  ID_AA64PFR1_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_ISAR0_EL1, ID_AA64ISAR0_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_ISAR1_EL1, ID_AA64ISAR1_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_ISAR2_EL1, ID_AA64ISAR2_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_MMFR0_EL1, ID_AA64MMFR0_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_MMFR1_EL1, ID_AA64MMFR1_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_MMFR2_EL1, ID_AA64MMFR2_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_DFR0_EL1,  ID_AA64DFR0_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_DFR1_EL1,  ID_AA64DFR1_EL1_IDX },
    };

    size_t n_regs = ARRAY_SIZE(regs);
    hv_register_name *reg_names = g_new(hv_register_name, n_regs);
    hv_register_value *reg_values = g_new(hv_register_value, n_regs);

    for (i = 0; i < n_regs; i++) {
        reg_names[i] = regs[i].name;
    }

    ret = mshv_get_partition_regs(vm_fd, reg_names, reg_values, n_regs);

    if (ret < 0) {
        error_report("Failed to get host ID registers");
        success = false;
        goto out;
    }

    for (i = 0; i < n_regs; i++) {
        ahcf->isar.idregs[regs[i].isar_idx] = reg_values[i].reg64;
    }

    /* Read MIDR_EL1 from sysfs */
    if (g_file_get_contents("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1",
                            &contents, NULL, NULL)) {
        ahcf->midr = g_ascii_strtoull(contents, NULL, 0);
    } else {
        error_report("Failed to read MIDR_EL1 from sysfs");
        success = false;
        goto out;
    }

    ahcf->dtb_compatible = "arm,armv8";
    ahcf->features = (1ULL << ARM_FEATURE_V8) |
                     (1ULL << ARM_FEATURE_AARCH64) |
                     (1ULL << ARM_FEATURE_PMU) |
                     (1ULL << ARM_FEATURE_GENERIC_TIMER) |
                     (1ULL << ARM_FEATURE_NEON);

    clamp_id_aa64mmfr0_parange_to_ipa_size(mshv_fd, &ahcf->isar);

    /* SVE (Scalable Vector Extension) and SME (Scalable Matrix Extension)
     * require specific context switch logic in the accelerator.
     * Mask them out for now to ensure stability.
     */
    /* Mask SVE in PFR0 */
    pfr0 = GET_IDREG(&ahcf->isar, ID_AA64PFR0);
    pfr0 &= ~R_ID_AA64PFR0_SVE_MASK;
    SET_IDREG(&ahcf->isar, ID_AA64PFR0, pfr0);

    /* Mask SME in PFR1 */
    pfr1 = GET_IDREG(&ahcf->isar, ID_AA64PFR1);
    pfr1 &= ~R_ID_AA64PFR1_SME_MASK;
    SET_IDREG(&ahcf->isar, ID_AA64PFR1, pfr1);

out:
    g_free(contents);
    g_free(reg_names);
    g_free(reg_values);
    return success;
}

void mshv_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    if (!arm_host_cpu_features.dtb_compatible) {
        if (!mshv_enabled() ||
            !mshv_arm_get_host_cpu_features(&arm_host_cpu_features)) {
            /* We can't report this error yet, so flag that we need to
             * in arm_cpu_realizefn().
             */
            cpu->host_cpu_probe_failed = true;
            return;
        }
    }

    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar = arm_host_cpu_features.isar;
    cpu->env.features = arm_host_cpu_features.features;
    cpu->midr = arm_host_cpu_features.midr;
    cpu->reset_sctlr = arm_host_cpu_features.reset_sctlr;
}

int mshv_arch_accel_init(AccelState *as, MachineState *ms, int mshv_fd)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    int pa_range;
    uint32_t ipa_size;

    if (mc->get_physical_address_range) {
        ipa_size = mshv_arm_get_ipa_bit_size(mshv_fd);
        pa_range = mc->get_physical_address_range(ms, ipa_size, ipa_size);
        if (pa_range < 0) {
            return -EINVAL;
        }
    }

    return 0;
}
