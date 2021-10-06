/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (C) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef PPC_HOST_SIGNAL_H
#define PPC_HOST_SIGNAL_H

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.regs->nip;
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    return uc->uc_mcontext.regs->trap != 0x400
        && (uc->uc_mcontext.regs->dsisr & 0x02000000);
}

#endif
