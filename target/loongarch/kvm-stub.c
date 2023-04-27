/*
 * QEMU KVM LoongArch specific function stubs
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */
#include "cpu.h"

void kvm_loongarch_set_interrupt(LoongArchCPU *cpu, int irq, int level)
{
   g_assert_not_reached();
}
