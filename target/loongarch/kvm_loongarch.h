/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch kvm interface
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#ifndef QEMU_KVM_LOONGARCH_H
#define QEMU_KVM_LOONGARCH_H

void kvm_loongarch_reset_vcpu(LoongArchCPU *cpu);
int  kvm_loongarch_set_interrupt(LoongArchCPU *cpu, int irq, int level);

#endif
