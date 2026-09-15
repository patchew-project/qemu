/*
 * Program counter of an interrupted riscv64 thread, from its signal frame.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV64_HOST_SIGNAL_PC_H
#define RISCV64_HOST_SIGNAL_PC_H

#ifdef CONFIG_LINUX
#define HAVE_HOST_SIGNAL_PC 1

static inline uintptr_t host_signal_pc(const ucontext_t *uc)
{
    return uc->uc_mcontext.__gregs[REG_PC];
}
#else
#define HAVE_HOST_SIGNAL_PC 0
#endif

#endif
