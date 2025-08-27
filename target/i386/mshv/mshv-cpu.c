/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou   <ziqiaozhou@microsoft.com>
 *          Magnus Kulke  <magnuskulke@microsoft.com>
 *          Jinank Jain   <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/typedefs.h"

#include "system/mshv.h"
#include "system/address-spaces.h"
#include "linux/mshv.h"
#include "hw/hyperv/hvhdk_mini.h"
#include "hw/hyperv/hvgdk.h"

#include "cpu.h"
#include "emulate/x86_decode.h"
#include "emulate/x86_emu.h"
#include "emulate/x86_flags.h"

#include "trace-accel_mshv.h"
#include "trace.h"

#include <sys/ioctl.h>

static enum hv_register_name STANDARD_REGISTER_NAMES[18] = {
    HV_X64_REGISTER_RAX,
    HV_X64_REGISTER_RBX,
    HV_X64_REGISTER_RCX,
    HV_X64_REGISTER_RDX,
    HV_X64_REGISTER_RSI,
    HV_X64_REGISTER_RDI,
    HV_X64_REGISTER_RSP,
    HV_X64_REGISTER_RBP,
    HV_X64_REGISTER_R8,
    HV_X64_REGISTER_R9,
    HV_X64_REGISTER_R10,
    HV_X64_REGISTER_R11,
    HV_X64_REGISTER_R12,
    HV_X64_REGISTER_R13,
    HV_X64_REGISTER_R14,
    HV_X64_REGISTER_R15,
    HV_X64_REGISTER_RIP,
    HV_X64_REGISTER_RFLAGS,
};

int mshv_set_generic_regs(int cpu_fd, hv_register_assoc *assocs, size_t n_regs)
{
    struct mshv_vp_registers input = {
        .count = n_regs,
        .regs = assocs,
    };

    return ioctl(cpu_fd, MSHV_SET_VP_REGISTERS, &input);
}

static int set_standard_regs(const CPUState *cpu)
{
    X86CPU *x86cpu = X86_CPU(cpu);
    CPUX86State *env = &x86cpu->env;
    hv_register_assoc assocs[ARRAY_SIZE(STANDARD_REGISTER_NAMES)];
    int ret;
    int cpu_fd = mshv_vcpufd(cpu);
    size_t n_regs = ARRAY_SIZE(STANDARD_REGISTER_NAMES);

    /* set names */
    for (size_t i = 0; i < ARRAY_SIZE(STANDARD_REGISTER_NAMES); i++) {
        assocs[i].name = STANDARD_REGISTER_NAMES[i];
    }
    assocs[0].value.reg64 = env->regs[R_EAX];
    assocs[1].value.reg64 = env->regs[R_EBX];
    assocs[2].value.reg64 = env->regs[R_ECX];
    assocs[3].value.reg64 = env->regs[R_EDX];
    assocs[4].value.reg64 = env->regs[R_ESI];
    assocs[5].value.reg64 = env->regs[R_EDI];
    assocs[6].value.reg64 = env->regs[R_ESP];
    assocs[7].value.reg64 = env->regs[R_EBP];
    assocs[8].value.reg64 = env->regs[R_R8];
    assocs[9].value.reg64 = env->regs[R_R9];
    assocs[10].value.reg64 = env->regs[R_R10];
    assocs[11].value.reg64 = env->regs[R_R11];
    assocs[12].value.reg64 = env->regs[R_R12];
    assocs[13].value.reg64 = env->regs[R_R13];
    assocs[14].value.reg64 = env->regs[R_R14];
    assocs[15].value.reg64 = env->regs[R_R15];
    assocs[16].value.reg64 = env->eip;
    lflags_to_rflags(env);
    assocs[17].value.reg64 = env->eflags;

    ret = mshv_set_generic_regs(cpu_fd, assocs, n_regs);
    if (ret < 0) {
        error_report("failed to set standard registers");
        return -errno;
    }
    return 0;
}

int mshv_store_regs(CPUState *cpu)
{
    int ret;

    ret = set_standard_regs(cpu);
    if (ret < 0) {
        error_report("Failed to store standard registers");
        return -1;
    }

    return 0;
}


int mshv_load_regs(CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

int mshv_arch_put_registers(const CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features)
{
    features->access_guest_idle_reg = 1;
}

int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit)
{
    error_report("unimplemented");
    abort();
}

void mshv_remove_vcpu(int vm_fd, int cpu_fd)
{
    close(cpu_fd);
}


int mshv_create_vcpu(int vm_fd, uint8_t vp_index, int *cpu_fd)
{
    int ret;
    struct mshv_create_vp vp_arg = {
        .vp_index = vp_index,
    };
    ret = ioctl(vm_fd, MSHV_CREATE_VP, &vp_arg);
    if (ret < 0) {
        error_report("failed to create mshv vcpu: %s", strerror(errno));
        return -1;
    }

    *cpu_fd = ret;

    return 0;
}

void mshv_init_cpu_logic(void)
{
}

void mshv_arch_init_vcpu(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    env->emu_mmio_buf = g_new(char, 4096);
}

void mshv_arch_destroy_vcpu(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    g_free(env->emu_mmio_buf);
    env->emu_mmio_buf = NULL;
}

/*
 * Default Microsoft Hypervisor behavior for unimplemented MSR is to send a
 * fault to the guest if it tries to access it. It is possible to override
 * this behavior with a more suitable option i.e., ignore writes from the guest
 * and return zero in attempt to read unimplemented.
 */
static int set_unimplemented_msr_action(int vm_fd)
{
    struct hv_input_set_partition_property in = {0};
    struct mshv_root_hvcall args = {0};

    in.property_code  = HV_PARTITION_PROPERTY_UNIMPLEMENTED_MSR_ACTION;
    in.property_value = HV_UNIMPLEMENTED_MSR_ACTION_IGNORE_WRITE_READ_ZERO;

    args.code   = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz  = sizeof(in);
    args.in_ptr = (uint64_t)&in;

    trace_mshv_hvcall_args("unimplemented_msr_action", args.code, args.in_sz);

    int ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set unimplemented MSR action");
        return -1;
    }
    return 0;
}

int mshv_arch_post_init_vm(int vm_fd)
{
    int ret;

    ret = set_unimplemented_msr_action(vm_fd);
    if (ret < 0) {
        error_report("Failed to set unimplemented MSR action");
    }

    return ret;
}
