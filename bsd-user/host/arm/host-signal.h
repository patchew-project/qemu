/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2021 Warner Losh
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ARM_HOST_SIGNAL_H
#define ARM_HOST_SIGNAL_H

#include <sys/ucontext.h>

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.__gregs[_REG_PC];
}

static inline void host_signal_set_pc(ucontext_t *uc, uintptr_t pc)
{
    uc->uc_mcontext.__gregs[_REG_PC] = pc;
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    /*
     * In the FSR, bit 11 is WnR. FreeBSD returns this as part of the
     * si_info.si_trapno which we don't have access to here.  We assume that uc
     * is part of a trapframe and reach around to get to the si_info that's in
     * the sigframe just before it, though this may be unwise.
     */
    siginfo_t *si;
    si = &((siginfo_t *)uc)[-1];
    uint32_t fsr = si->si_trapno;

    return extract32(fsr, 11, 1);
}

#endif
