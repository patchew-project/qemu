/*
 * Program counter of an interrupted ppc64 thread, from its signal frame.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC64_HOST_SIGNAL_PC_H
#define PPC64_HOST_SIGNAL_PC_H

#ifdef CONFIG_LINUX
#include <asm/ptrace.h>

#define HAVE_HOST_SIGNAL_PC 1

static inline uintptr_t host_signal_pc(const ucontext_t *uc)
{
    return uc->uc_mcontext.gp_regs[PT_NIP];
}
#else
#define HAVE_HOST_SIGNAL_PC 0
#endif

#endif
