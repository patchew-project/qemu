/*
 * QEMU KVM Hyper-V support
 *
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *  Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef TARGET_I386_HYPERV_H
#define TARGET_I386_HYPERV_H

#include "cpu.h"
#include "sysemu/kvm.h"
#include "hw/hyperv/hyperv.h"

#ifdef CONFIG_KVM
int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit);
int kvm_hv_handle_wrmsr(X86CPU *cpu, uint32_t msr, uint64_t data);

#endif

void hyperv_x86_hcall_page_update(X86CPU *cpu);

int hyperv_x86_synic_add(X86CPU *cpu);
void hyperv_x86_synic_reset(X86CPU *cpu);
void hyperv_x86_synic_update(X86CPU *cpu);

#endif
