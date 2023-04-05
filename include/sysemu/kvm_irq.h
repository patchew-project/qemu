/*
 * QEMU KVM IRQ helpers
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in hardware (non-KVM-specific) code */

#ifndef QEMU_KVM_IRQ_H
#define QEMU_KVM_IRQ_H

#include "qemu/notify.h"
#include "sysemu/kvm.h"

struct kvm_lapic_state;

int kvm_has_gsi_routing(void);
int kvm_has_intx_set_mask(void);

/**
 * kvm_arm_supports_user_irq
 *
 * Not all KVM implementations support notifications for kernel generated
 * interrupt events to user space. This function indicates whether the current
 * KVM implementation does support them.
 *
 * Returns: true if KVM supports using kernel generated IRQs from user space
 */
bool kvm_arm_supports_user_irq(void);

void kvm_irqchip_add_change_notifier(Notifier *n);
void kvm_irqchip_remove_change_notifier(Notifier *n);
void kvm_irqchip_change_notify(void);

/**
 * kvm_irqchip_add_msi_route - Add MSI route for specific vector
 * @c:      KVMRouteChange instance.
 * @vector: which vector to add. This can be either MSI/MSIX
 *          vector. The function will automatically detect whether
 *          MSI/MSIX is enabled, and fetch corresponding MSI
 *          message.
 * @dev:    Owner PCI device to add the route. If @dev is specified
 *          as @NULL, an empty MSI message will be inited.
 * @return: virq (>=0) when success, errno (<0) when failed.
 */
int kvm_irqchip_add_msi_route(KVMRouteChange *c, int vector, PCIDevice *dev);
int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg,
                                 PCIDevice *dev);
void kvm_irqchip_commit_routes(KVMState *s);

static inline KVMRouteChange kvm_irqchip_begin_route_changes(KVMState *s)
{
    return (KVMRouteChange) { .s = s, .changes = 0 };
}

static inline void kvm_irqchip_commit_route_changes(KVMRouteChange *c)
{
    if (c->changes) {
        kvm_irqchip_commit_routes(c->s);
        c->changes = 0;
    }
}

void kvm_irqchip_release_virq(KVMState *s, int virq);

int kvm_irqchip_add_adapter_route(KVMState *s, AdapterInfo *adapter);
int kvm_irqchip_add_hv_sint_route(KVMState *s, uint32_t vcpu, uint32_t sint);

int kvm_irqchip_add_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                       EventNotifier *rn, int virq);
int kvm_irqchip_remove_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                          int virq);
int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n,
                                   EventNotifier *rn, qemu_irq irq);
int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n,
                                      qemu_irq irq);
void kvm_irqchip_set_qemuirq_gsi(KVMState *s, qemu_irq irq, int gsi);
void kvm_pc_setup_irq_routing(bool pci_enabled);
void kvm_init_irq_routing(KVMState *s);

bool kvm_kernel_irqchip_allowed(void);
bool kvm_kernel_irqchip_required(void);
bool kvm_kernel_irqchip_split(void);


int kvm_set_irq(KVMState *s, int irq, int level);
int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg);

void kvm_irqchip_add_irq_route(KVMState *s, int gsi, int irqchip, int pin);

void kvm_get_apic_state(DeviceState *d, struct kvm_lapic_state *kapic);

#endif
