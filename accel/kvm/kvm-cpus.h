/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef KVM_CPUS_H
#define KVM_CPUS_H

#include "sysemu/cpus.h"

int kvm_init_vcpu(CPUState *cpu, Error **errp);
int kvm_cpu_exec(CPUState *cpu);
void kvm_destroy_vcpu(CPUState *cpu);
void kvm_cpu_synchronize_post_reset(CPUState *cpu);
void kvm_cpu_synchronize_post_init(CPUState *cpu);
void kvm_cpu_synchronize_pre_loadvm(CPUState *cpu);
bool kvm_supports_guest_debug(void);
int kvm_insert_breakpoint(CPUState *cpu, int type, vaddr addr, vaddr len);
int kvm_remove_breakpoint(CPUState *cpu, int type, vaddr addr, vaddr len);
void kvm_remove_all_breakpoints(CPUState *cpu);
/**
 * kvm_create_vcpu - Gets a parked KVM vCPU or creates a KVM vCPU
 * @cpu: QOM CPUState object for which KVM vCPU has to be fetched/created.
 *
 * @returns: 0 when success, errno (<0) when failed.
 */
int kvm_create_vcpu(CPUState *cpu);

/**
 * kvm_park_vcpu - Park QEMU KVM vCPU context
 * @cpu: QOM CPUState object for which QEMU KVM vCPU context has to be parked.
 *
 * @returns: none
 */
void kvm_park_vcpu(CPUState *cpu);
#endif /* KVM_CPUS_H */
