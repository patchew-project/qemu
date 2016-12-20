/*
 * QEMU KVM stub
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Author: Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "sysemu/kvm.h"

#ifndef CONFIG_USER_ONLY
#include "hw/pci/msi.h"
#endif

void kvm_flush_coalesced_mmio_buffer(void)
{
}

int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap)
{
    return -ENOSYS;
}

int kvm_insert_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

int kvm_remove_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

void kvm_remove_all_breakpoints(CPUState *cpu)
{
}

#ifndef _WIN32
int kvm_set_signal_mask(CPUState *cpu, const sigset_t *sigset)
{
    abort();
}
#endif

int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
    return 1;
}

int kvm_on_sigbus(int code, void *addr)
{
    return 1;
}

#ifndef CONFIG_USER_ONLY
int kvm_irqchip_add_msi_route(KVMState *s, int vector, PCIDevice *dev)
{
    return -ENOSYS;
}

void kvm_init_irq_routing(KVMState *s)
{
}

void kvm_irqchip_release_virq(KVMState *s, int virq)
{
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg,
                                 PCIDevice *dev)
{
    return -ENOSYS;
}

void kvm_irqchip_commit_routes(KVMState *s)
{
}

int kvm_irqchip_add_adapter_route(KVMState *s, AdapterInfo *adapter)
{
    return -ENOSYS;
}

int kvm_irqchip_add_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                       EventNotifier *rn, int virq)
{
    return -ENOSYS;
}

int kvm_irqchip_remove_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                          int virq)
{
    return -ENOSYS;
}

bool kvm_has_free_slot(MachineState *ms)
{
    return false;
}
#endif
