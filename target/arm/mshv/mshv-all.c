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

#include "system/mshv.h"
#include "system/mshv_int.h"

int mshv_load_regs(CPUState *cpu)
{
    return 0;
}

int mshv_arch_put_registers(const CPUState *cpu)
{
    return 0;
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
