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
#include "hw/arm/virt.h"
#include "system/mshv.h"
#include "system/mshv_int.h"

static int load_regs(CPUState *cpu)
{
    return 0;
}

static int store_regs(const CPUState *cpu)
{
    return 0;
}

int mshv_arch_load_vcpu_state(CPUState *cpu)
{
    return load_regs(cpu);
}

int mshv_arch_store_vcpu_state(const CPUState *cpu)
{
    return store_regs(cpu);
}

int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit)
{
    return 0;
}

void mshv_arch_init_vcpu(CPUState *cpu)
{

}

void mshv_arch_destroy_vcpu(CPUState *cpu)
{

}

int mshv_arch_accel_init(AccelState *as, MachineState *ms, int mshv_fd)
{
    return 0;
}

void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features)
{

}

void mshv_arch_disable_partition_proc_features(
    union hv_partition_processor_features *disabled_features)
{
    /* No processor features to disable on ARM */
}

int mshv_arch_post_init_vm(int vm_fd)
{
    return 0;
}
