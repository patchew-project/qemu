/*
 * QEMU KVM LoongArch specific function stubs
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "cpu.h"

void kvm_loongarch_reset_vcpu(LoongArchCPU *cpu)
{
    abort();
}

void kvm_loongarch_set_interrupt(LoongArchCPU *cpu, int irq, int level)
{
    abort();
}
