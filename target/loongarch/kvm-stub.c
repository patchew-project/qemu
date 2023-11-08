/*
 * QEMU KVM LoongArch specific function stubs
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "kvm_loongarch.h"

int kvm_loongarch_set_interrupt(LoongArchCPU *cpu, int irq, int level)
{
   g_assert_not_reached();
}
