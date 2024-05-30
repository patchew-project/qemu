/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch kvm interface
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "cpu.h"

#ifndef QEMU_KVM_LOONGARCH_H
#define QEMU_KVM_LOONGARCH_H

int  kvm_loongarch_set_interrupt(LoongArchCPU *cpu, int irq, int level);
void kvm_arch_reset_vcpu(CPULoongArchState *env);

#ifdef CONFIG_KVM
/*
 * kvm_feature_supported:
 *
 * Returns: true if KVM supports specified feature
 * and false otherwise.
 */
bool kvm_feature_supported(CPUState *cs, enum loongarch_features feature);
#else
static inline bool kvm_feature_supported(CPUState *cs,
                                         enum loongarch_features feature)
{
    return false;
}
#endif

#endif
