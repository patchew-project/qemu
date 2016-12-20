/*
 * QEMU KVM stub
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Authors:
 *  Christoffer Dall  <christoffer.dall@linaro.org>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/kvm.h"

KVMState *kvm_state;
bool kvm_kernel_irqchip;
bool kvm_async_interrupts_allowed;
bool kvm_eventfds_allowed;
bool kvm_irqfds_allowed;
bool kvm_resamplefds_allowed;
bool kvm_msi_via_irqfd_allowed;
bool kvm_gsi_routing_allowed;
bool kvm_gsi_direct_mapping;
bool kvm_allowed;
bool kvm_readonly_mem_allowed;
bool kvm_ioeventfd_any_length_allowed;
bool kvm_msi_use_devid;

int kvm_destroy_vcpu(CPUState *cpu)
{
    return -ENOSYS;
}

int kvm_init_vcpu(CPUState *cpu)
{
    return -ENOSYS;
}

void kvm_cpu_synchronize_state(CPUState *cpu)
{
}

void kvm_cpu_synchronize_post_reset(CPUState *cpu)
{
}

void kvm_cpu_synchronize_post_init(CPUState *cpu)
{
}

int kvm_cpu_exec(CPUState *cpu)
{
    abort();
}

int kvm_has_sync_mmu(void)
{
    return 0;
}

int kvm_has_many_ioeventfds(void)
{
    return 0;
}

int kvm_arch_irqchip_create(MachineState *ms, KVMState *s)
{
    return 0;
}
