/*
 *  Emulation of Linux signals
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu.h"
#include "signal-common.h"
#include "linux-user/trace.h"

#define __SUNOS_MAXWIN   31

/* This is what SunOS does, so shall I. */
struct target_sigcontext {
    abi_ulong sigc_onstack;      /* state to restore */

    abi_ulong sigc_mask;         /* sigmask to restore */
    abi_ulong sigc_sp;           /* stack pointer */
    abi_ulong sigc_pc;           /* program counter */
    abi_ulong sigc_npc;          /* next program counter */
    abi_ulong sigc_psr;          /* for condition codes etc */
    abi_ulong sigc_g1;           /* User uses these two registers */
    abi_ulong sigc_o0;           /* within the trampoline code. */

    /* Now comes information regarding the users window set
         * at the time of the signal.
         */
    abi_ulong sigc_oswins;       /* outstanding windows */

    /* stack ptrs for each regwin buf */
    char *sigc_spbuf[__SUNOS_MAXWIN];

    /* Windows to restore after signal */
    struct {
        abi_ulong locals[8];
        abi_ulong ins[8];
    } sigc_wbuf[__SUNOS_MAXWIN];
};
/* A Sparc stack frame */
struct sparc_stackf {
    abi_ulong locals[8];
    abi_ulong ins[8];
    /* It's simpler to treat fp and callers_pc as elements of ins[]
         * since we never need to access them ourselves.
         */
    char *structptr;
    abi_ulong xargs[6];
    abi_ulong xxargs[1];
};

typedef struct {
    struct {
        abi_ulong psr;
        abi_ulong pc;
        abi_ulong npc;
        abi_ulong y;
        abi_ulong u_regs[16]; /* globals and ins */
    }               si_regs;
    int             si_mask;
} __siginfo_t;

typedef struct {
    abi_ulong  si_float_regs[32];
    unsigned   long si_fsr;
    unsigned   long si_fpqdepth;
    struct {
        unsigned long *insn_addr;
        unsigned long insn;
    } si_fpqueue [16];
} qemu_siginfo_fpu_t;


struct target_signal_frame {
    struct sparc_stackf ss;
    __siginfo_t         info;
    abi_ulong           fpu_save;
    uint32_t            insns[2] QEMU_ALIGNED(8);
    abi_ulong           extramask[TARGET_NSIG_WORDS - 1];
    abi_ulong           extra_size; /* Should be 0 */
    qemu_siginfo_fpu_t fpu_state;
};
struct target_rt_signal_frame {
    struct sparc_stackf ss;
    siginfo_t           info;
    abi_ulong           regs[20];
    sigset_t            mask;
    abi_ulong           fpu_save;
    uint32_t            insns[2];
    stack_t             stack;
    unsigned int        extra_size; /* Should be 0 */
    qemu_siginfo_fpu_t  fpu_state;
};

static inline abi_ulong get_sigframe(struct target_sigaction *sa, 
                                     CPUSPARCState *env,
                                     unsigned long framesize)
{
    abi_ulong sp = get_sp_from_cpustate(env);

    /*
     * If we are on the alternate signal stack and would overflow it, don't.
     * Return an always-bogus address instead so we will die with SIGSEGV.
         */
    if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize))) {
            return -1;
    }

    /* This is the X/Open sanctioned signal stack switching.  */
    sp = target_sigsp(sp, sa) - framesize;

    /* Always align the stack frame.  This handles two cases.  First,
     * sigaltstack need not be mindful of platform specific stack
     * alignment.  Second, if we took this signal because the stack
     * is not aligned properly, we'd like to take the signal cleanly
     * and report that.
     */
    sp &= ~15UL;

    return sp;
}

static int
setup___siginfo(__siginfo_t *si, CPUSPARCState *env, abi_ulong mask)
{
    int err = 0, i;

    __put_user(env->psr, &si->si_regs.psr);
    __put_user(env->pc, &si->si_regs.pc);
    __put_user(env->npc, &si->si_regs.npc);
    __put_user(env->y, &si->si_regs.y);
    for (i=0; i < 8; i++) {
        __put_user(env->gregs[i], &si->si_regs.u_regs[i]);
    }
    for (i=0; i < 8; i++) {
        __put_user(env->regwptr[WREG_O0 + i], &si->si_regs.u_regs[i + 8]);
    }
    __put_user(mask, &si->si_mask);
    return err;
}

#define NF_ALIGNEDSZ  (((sizeof(struct target_signal_frame) + 7) & (~7)))

void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUSPARCState *env)
{
    abi_ulong sf_addr;
    struct target_signal_frame *sf;
    int sigframe_size, err, i;

    /* 1. Make sure everything is clean */
    //synchronize_user_stack();

    sigframe_size = NF_ALIGNEDSZ;
    sf_addr = get_sigframe(ka, env, sigframe_size);
    trace_user_setup_frame(env, sf_addr);

    sf = lock_user(VERIFY_WRITE, sf_addr,
                   sizeof(struct target_signal_frame), 0);
    if (!sf) {
        goto sigsegv;
    }
#if 0
    if (invalid_frame_pointer(sf, sigframe_size))
        goto sigill_and_return;
#endif
    /* 2. Save the current process state */
    err = setup___siginfo(&sf->info, env, set->sig[0]);
    __put_user(0, &sf->extra_size);

    //save_fpu_state(regs, &sf->fpu_state);
    //__put_user(&sf->fpu_state, &sf->fpu_save);

    __put_user(set->sig[0], &sf->info.si_mask);
    for (i = 0; i < TARGET_NSIG_WORDS - 1; i++) {
        __put_user(set->sig[i + 1], &sf->extramask[i]);
    }

    for (i = 0; i < 8; i++) {
        __put_user(env->regwptr[i + WREG_L0], &sf->ss.locals[i]);
    }
    for (i = 0; i < 8; i++) {
        __put_user(env->regwptr[i + WREG_I0], &sf->ss.ins[i]);
    }
    if (err)
        goto sigsegv;

    /* 3. signal handler back-trampoline and parameters */
    env->regwptr[WREG_SP] = sf_addr;
    env->regwptr[WREG_O0] = sig;
    env->regwptr[WREG_O1] = sf_addr +
            offsetof(struct target_signal_frame, info);
    env->regwptr[WREG_O2] = sf_addr +
            offsetof(struct target_signal_frame, info);

    /* 4. signal handler */
    env->pc = ka->_sa_handler;
    env->npc = (env->pc + 4);
    /* 5. return to kernel instructions */
    if (ka->ka_restorer) {
        env->regwptr[WREG_O7] = ka->ka_restorer;
    } else {
        uint32_t val32;

        env->regwptr[WREG_O7] = sf_addr +
                offsetof(struct target_signal_frame, insns) - 2 * 4;

        /* mov __NR_sigreturn, %g1 */
        val32 = 0x821020d8;
        __put_user(val32, &sf->insns[0]);

        /* t 0x10 */
        val32 = 0x91d02010;
        __put_user(val32, &sf->insns[1]);
    }
    unlock_user(sf, sf_addr, sizeof(struct target_signal_frame));
    return;
#if 0
sigill_and_return:
    force_sig(TARGET_SIGILL);
#endif
sigsegv:
    unlock_user(sf, sf_addr, sizeof(struct target_signal_frame));
    force_sigsegv(sig);
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUSPARCState *env)
{
    qemu_log_mask(LOG_UNIMP, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUSPARCState *env)
{
    abi_ulong sf_addr;
    struct target_signal_frame *sf;
    abi_ulong up_psr, pc, npc;
    target_sigset_t set;
    sigset_t host_set;
    int i;

    sf_addr = env->regwptr[WREG_SP];
    trace_user_do_sigreturn(env, sf_addr);
    if (!lock_user_struct(VERIFY_READ, sf, sf_addr, 1)) {
        goto segv_and_exit;
    }

    /* 1. Make sure we are not getting garbage from the user */

    if (sf_addr & 3)
        goto segv_and_exit;

    __get_user(pc,  &sf->info.si_regs.pc);
    __get_user(npc, &sf->info.si_regs.npc);

    if ((pc | npc) & 3) {
        goto segv_and_exit;
    }

    /* 2. Restore the state */
    __get_user(up_psr, &sf->info.si_regs.psr);

    /* User can only change condition codes and FPU enabling in %psr. */
    env->psr = (up_psr & (PSR_ICC /* | PSR_EF */))
            | (env->psr & ~(PSR_ICC /* | PSR_EF */));

    env->pc = pc;
    env->npc = npc;
    __get_user(env->y, &sf->info.si_regs.y);
    for (i=0; i < 8; i++) {
        __get_user(env->gregs[i], &sf->info.si_regs.u_regs[i]);
    }
    for (i=0; i < 8; i++) {
        __get_user(env->regwptr[i + WREG_O0], &sf->info.si_regs.u_regs[i + 8]);
    }

    /* FIXME: implement FPU save/restore:
     * __get_user(fpu_save, &sf->fpu_save);
     * if (fpu_save) {
     *     if (restore_fpu_state(env, fpu_save)) {
     *         goto segv_and_exit;
     *     }
     * }
     */

    /* This is pretty much atomic, no amount locking would prevent
         * the races which exist anyways.
         */
    __get_user(set.sig[0], &sf->info.si_mask);
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(set.sig[i], &sf->extramask[i - 1]);
    }

    target_to_host_sigset_internal(&host_set, &set);
    set_sigmask(&host_set);

    unlock_user_struct(sf, sf_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

segv_and_exit:
    unlock_user_struct(sf, sf_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}

long do_rt_sigreturn(CPUSPARCState *env)
{
    trace_user_do_rt_sigreturn(env, 0);
    qemu_log_mask(LOG_UNIMP, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}
