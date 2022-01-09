/*
 *  Emulation of BSD signals
 *
 *  Copyright (c) 2003 - 2008 Fabrice Bellard
 *  Copyright (c) 2013 Stacey Son
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
#include "trace.h"
#include "hw/core/tcg-cpu-ops.h"
#include "host-signal.h"

/*
 * Stubbed out routines until we merge signal support from bsd-user
 * fork.
 */

static struct target_sigaction sigact_table[TARGET_NSIG];
static void host_signal_handler(int host_sig, siginfo_t *info, void *puc);
static void target_to_host_sigset_internal(sigset_t *d,
        const target_sigset_t *s);


int host_to_target_signal(int sig)
{
    return sig;
}

int target_to_host_signal(int sig)
{
    return sig;
}

static inline void target_sigemptyset(target_sigset_t *set)
{
    memset(set, 0, sizeof(*set));
}

#include <signal.h>

int
qemu_sigorset(sigset_t *dest, const sigset_t *left, const sigset_t *right)
{
    sigset_t work;
    int i;

    sigemptyset(&work);
    for (i = 1; i < NSIG; ++i) {
        if (sigismember(left, i) || sigismember(right, i)) {
            sigaddset(&work, i);
        }
    }

    *dest = work;
    return 0;
}

static inline void target_sigaddset(target_sigset_t *set, int signum)
{
    signum--;
    uint32_t mask = (uint32_t)1 << (signum % TARGET_NSIG_BPW);
    set->__bits[signum / TARGET_NSIG_BPW] |= mask;
}

static inline int target_sigismember(const target_sigset_t *set, int signum)
{
    signum--;
    abi_ulong mask = (abi_ulong)1 << (signum % TARGET_NSIG_BPW);
    return (set->__bits[signum / TARGET_NSIG_BPW] & mask) != 0;
}

/* Adjust the signal context to rewind out of safe-syscall if we're in it */
static inline void rewind_if_in_safe_syscall(void *puc)
{
    ucontext_t *uc = (ucontext_t *)puc;
    uintptr_t pcreg = host_signal_pc(uc);

    if (pcreg > (uintptr_t)safe_syscall_start
        && pcreg < (uintptr_t)safe_syscall_end) {
        host_signal_set_pc(uc, (uintptr_t)safe_syscall_start);
    }
}

static void host_to_target_sigset_internal(target_sigset_t *d,
        const sigset_t *s)
{
    int i;

    target_sigemptyset(d);
    for (i = 1; i <= TARGET_NSIG; i++) {
        if (sigismember(s, i)) {
            target_sigaddset(d, host_to_target_signal(i));
        }
    }
}

void host_to_target_sigset(target_sigset_t *d, const sigset_t *s)
{
    target_sigset_t d1;
    int i;

    host_to_target_sigset_internal(&d1, s);
    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        d->__bits[i] = tswap32(d1.__bits[i]);
    }
}

static void target_to_host_sigset_internal(sigset_t *d,
        const target_sigset_t *s)
{
    int i;

    sigemptyset(d);
    for (i = 1; i <= TARGET_NSIG; i++) {
        if (target_sigismember(s, i)) {
            sigaddset(d, target_to_host_signal(i));
        }
    }
}

void target_to_host_sigset(sigset_t *d, const target_sigset_t *s)
{
    target_sigset_t s1;
    int i;

    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        s1.__bits[i] = tswap32(s->__bits[i]);
    }
    target_to_host_sigset_internal(d, &s1);
}

/* Siginfo conversion. */
static inline void host_to_target_siginfo_noswap(target_siginfo_t *tinfo,
        const siginfo_t *info)
{
    int sig, code;

    sig = host_to_target_signal(info->si_signo);
    /* XXX should have host_to_target_si_code() */
    code = tswap32(info->si_code);
    tinfo->si_signo = sig;
    tinfo->si_errno = info->si_errno;
    tinfo->si_code = info->si_code;
    tinfo->si_pid = info->si_pid;
    tinfo->si_uid = info->si_uid;
    tinfo->si_status = info->si_status;
    tinfo->si_addr = (abi_ulong)(unsigned long)info->si_addr;
    /* si_value is opaque to kernel */
    tinfo->si_value.sival_ptr =
        (abi_ulong)(unsigned long)info->si_value.sival_ptr;
    if (SIGILL == sig || SIGFPE == sig || SIGSEGV == sig || SIGBUS == sig ||
            SIGTRAP == sig) {
        tinfo->_reason._fault._trapno = info->_reason._fault._trapno;
    }
#ifdef SIGPOLL
    if (SIGPOLL == sig) {
        tinfo->_reason._poll._band = info->_reason._poll._band;
    }
#endif
    if (SI_TIMER == code) {
        int timerid;

        timerid = info->_reason._timer._timerid;
        tinfo->_reason._timer._timerid = timerid;
        tinfo->_reason._timer._overrun = info->_reason._timer._overrun;
    }
}

/* Returns 1 if given signal should dump core if not handled. */
static int core_dump_signal(int sig)
{
    switch (sig) {
    case TARGET_SIGABRT:
    case TARGET_SIGFPE:
    case TARGET_SIGILL:
    case TARGET_SIGQUIT:
    case TARGET_SIGSEGV:
    case TARGET_SIGTRAP:
    case TARGET_SIGBUS:
        return 1;
    default:
        return 0;
    }
}

/* Signal queue handling. */
static inline struct qemu_sigqueue *alloc_sigqueue(CPUArchState *env)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    struct qemu_sigqueue *q = ts->first_free;

    if (!q) {
        return NULL;
    }
    ts->first_free = q->next;
    return q;
}

static inline void free_sigqueue(CPUArchState *env, struct qemu_sigqueue *q)
{

    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    q->next = ts->first_free;
    ts->first_free = q;
}

/* Abort execution with signal. */
void QEMU_NORETURN force_sig(int target_sig)
{
    CPUArchState *env = thread_cpu->env_ptr;
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    int core_dumped = 0;
    int host_sig;
    struct sigaction act;

    host_sig = target_to_host_signal(target_sig);
    gdb_signalled(env, target_sig);

    /* Dump core if supported by target binary format */
    if (core_dump_signal(target_sig) && (ts->bprm->core_dump != NULL)) {
        stop_all_tasks();
        core_dumped =
            ((*ts->bprm->core_dump)(target_sig, env) == 0);
    }
    if (core_dumped) {
        struct rlimit nodump;

        /*
         * We already dumped the core of target process, we don't want
         * a coredump of qemu itself.
         */
         getrlimit(RLIMIT_CORE, &nodump);
         nodump.rlim_cur = 0;
         setrlimit(RLIMIT_CORE, &nodump);
         (void) fprintf(stderr, "qemu: uncaught target signal %d (%s) "
             "- %s\n", target_sig, strsignal(host_sig), "core dumped");
    }

    /*
     * The proper exit code for dying from an uncaught signal is
     * -<signal>.  The kernel doesn't allow exit() or _exit() to pass
     * a negative value.  To get the proper exit code we need to
     * actually die from an uncaught signal.  Here the default signal
     * handler is installed, we send ourself a signal and we wait for
     * it to arrive.
     */
    memset(&act, 0, sizeof(act));
    sigfillset(&act.sa_mask);
    act.sa_handler = SIG_DFL;
    sigaction(host_sig, &act, NULL);

    kill(getpid(), host_sig);

    /*
     * Make sure the signal isn't masked (just reuse the mask inside
     * of act).
     */
    sigdelset(&act.sa_mask, host_sig);
    sigsuspend(&act.sa_mask);

    /* unreachable */
    abort();
}

/*
 * Queue a signal so that it will be send to the virtual CPU as soon as
 * possible.
 */
void queue_signal(CPUArchState *env, int sig, target_siginfo_t *info)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    struct emulated_sigtable *k;
    struct qemu_sigqueue *q, **pq;

    k = &ts->sigtab[sig - 1];
    trace_user_queue_signal(env, sig); /* We called this in the caller? XXX */
    /*
     * XXX does the segv changes make this go away? -- I think so
     */
    if (sig == TARGET_SIGSEGV && sigismember(&ts->signal_mask, SIGSEGV)) {
        /*
         * Guest has blocked SIGSEGV but we got one anyway. Assume this is a
         * forced SIGSEGV (ie one the kernel handles via force_sig_info because
         * it got a real MMU fault). A blocked SIGSEGV in that situation is
         * treated as if using the default handler. This is not correct if some
         * other process has randomly sent us a SIGSEGV via kill(), but that is
         * not easy to distinguish at this point, so we assume it doesn't
         * happen.
         */
        force_sig(sig);
    }

    pq = &k->first;

    /*
     * FreeBSD signals are always queued.  Linux only queues real time signals.
     * XXX this code is not thread safe.  "What lock protects ts->sigtab?"
     */
    if (!k->pending) {
        /* first signal */
        q = &k->info;
    } else {
        q = alloc_sigqueue(env);
        if (!q) {
            return; /* XXX WHAT TO DO */
        }
        while (*pq != NULL) {
            pq = &(*pq)->next;
        }
    }
    *pq = q;
    q->info = *info;
    q->next = NULL;
    k->pending = 1;
    /* Signal that a new signal is pending. */
    ts->signal_pending = 1;
    return;
}

static int fatal_signal(int sig)
{

    switch (sig) {
    case TARGET_SIGCHLD:
    case TARGET_SIGURG:
    case TARGET_SIGWINCH:
    case TARGET_SIGINFO:
        /* Ignored by default. */
        return 0;
    case TARGET_SIGCONT:
    case TARGET_SIGSTOP:
    case TARGET_SIGTSTP:
    case TARGET_SIGTTIN:
    case TARGET_SIGTTOU:
        /* Job control signals.  */
        return 0;
    default:
        return 1;
    }
}

/*
 * Force a synchronously taken QEMU_SI_FAULT signal. For QEMU the
 * 'force' part is handled in process_pending_signals().
 */
void force_sig_fault(int sig, int code, abi_ulong addr)
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    target_siginfo_t info = {};

    info.si_signo = sig;
    info.si_errno = 0;
    info.si_code = code;
    info.si_addr = addr;
    queue_signal(env, sig, &info);
}

static void host_signal_handler(int host_sig, siginfo_t *info, void *puc)
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    int sig;
    target_siginfo_t tinfo;
    ucontext_t *uc = puc;
    uintptr_t pc = 0;
    bool sync_sig = false;

    /*
     * Non-spoofed SIGSEGV and SIGBUS are synchronous, and need special
     * handling wrt signal blocking and unwinding.
     */
    if ((host_sig == SIGSEGV || host_sig == SIGBUS) && info->si_code > 0) {
        MMUAccessType access_type;
        uintptr_t host_addr;
        abi_ptr guest_addr;
        bool is_write;

        host_addr = (uintptr_t)info->si_addr;

        /*
         * Convert forcefully to guest address space: addresses outside
         * reserved_va are still valid to report via SEGV_MAPERR.
         */
        guest_addr = h2g_nocheck(host_addr);

        pc = host_signal_pc(uc);
        is_write = host_signal_write(info, uc);
        access_type = adjust_signal_pc(&pc, is_write);

        if (host_sig == SIGSEGV) {
            bool maperr = true;

            if (info->si_code == SEGV_ACCERR && h2g_valid(host_addr)) {
                /* If this was a write to a TB protected page, restart. */
                if (is_write &&
                    handle_sigsegv_accerr_write(cpu, &uc->uc_sigmask,
                                                pc, guest_addr)) {
                    return;
                }

                /*
                 * With reserved_va, the whole address space is PROT_NONE,
                 * which means that we may get ACCERR when we want MAPERR.
                 */
                if (page_get_flags(guest_addr) & PAGE_VALID) {
                    maperr = false;
                } else {
                    info->si_code = SEGV_MAPERR;
                }
            }

            sigprocmask(SIG_SETMASK, &uc->uc_sigmask, NULL);
            cpu_loop_exit_sigsegv(cpu, guest_addr, access_type, maperr, pc);
        } else {
            sigprocmask(SIG_SETMASK, &uc->uc_sigmask, NULL);
            if (info->si_code == BUS_ADRALN) {
                cpu_loop_exit_sigbus(cpu, guest_addr, access_type, pc);
            }
        }

        sync_sig = true;
    }

    /* Get the target signal number. */
    sig = host_to_target_signal(host_sig);
    if (sig < 1 || sig > TARGET_NSIG) {
        return;
    }
    trace_user_host_signal(cpu, host_sig, sig);

    host_to_target_siginfo_noswap(&tinfo, info);

    queue_signal(env, sig, &tinfo);       /* XXX how to cope with failure? */
    /*
     * Linux does something else here -> the queue signal may be wrong, but
     * maybe not.  And then it does the rewind_if_in_safe_syscall
     */

    /*
     * For synchronous signals, unwind the cpu state to the faulting
     * insn and then exit back to the main loop so that the signal
     * is delivered immediately.
     XXXX Should this be in queue_signal?
     */
    if (sync_sig) {
        cpu->exception_index = EXCP_INTERRUPT;
        cpu_loop_exit_restore(cpu, pc);
    }

    rewind_if_in_safe_syscall(puc);

    /*
     * Block host signals until target signal handler entered. We
     * can't block SIGSEGV or SIGBUS while we're executing guest
     * code in case the guest code provokes one in the window between
     * now and it getting out to the main loop. Signals will be
     * unblocked again in process_pending_signals().
     */
    sigfillset(&uc->uc_sigmask);
    sigdelset(&uc->uc_sigmask, SIGSEGV);
    sigdelset(&uc->uc_sigmask, SIGBUS);

    /* Interrupt the virtual CPU as soon as possible. */
    cpu_exit(thread_cpu);
}

void signal_init(void)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
    struct sigaction act;
    struct sigaction oact;
    int i;
    int host_sig;

    /* Set the signal mask from the host mask. */
    sigprocmask(0, 0, &ts->signal_mask);

    /*
     * Set all host signal handlers. ALL signals are blocked during the
     * handlers to serialize them.
     */
    memset(sigact_table, 0, sizeof(sigact_table));

    sigfillset(&act.sa_mask);
    act.sa_sigaction = host_signal_handler;
    act.sa_flags = SA_SIGINFO;

    for (i = 1; i <= TARGET_NSIG; i++) {
        host_sig = target_to_host_signal(i);
        sigaction(host_sig, NULL, &oact);
        if (oact.sa_sigaction == (void *)SIG_IGN) {
            sigact_table[i - 1]._sa_handler = TARGET_SIG_IGN;
        } else if (oact.sa_sigaction == (void *)SIG_DFL) {
            sigact_table[i - 1]._sa_handler = TARGET_SIG_DFL;
        }
        /*
         * If there's already a handler installed then something has
         * gone horribly wrong, so don't even try to handle that case.
         * Install some handlers for our own use.  We need at least
         * SIGSEGV and SIGBUS, to detect exceptions.  We can not just
         * trap all signals because it affects syscall interrupt
         * behavior.  But do trap all default-fatal signals.
         */
        if (fatal_signal(i)) {
            sigaction(host_sig, &act, NULL);
        }
    }
}

void process_pending_signals(CPUArchState *cpu_env)
{
}

void cpu_loop_exit_sigsegv(CPUState *cpu, target_ulong addr,
                           MMUAccessType access_type, bool maperr, uintptr_t ra)
{
    const struct TCGCPUOps *tcg_ops = CPU_GET_CLASS(cpu)->tcg_ops;

    if (tcg_ops->record_sigsegv) {
        tcg_ops->record_sigsegv(cpu, addr, access_type, maperr, ra);
    }

    force_sig_fault(TARGET_SIGSEGV,
                    maperr ? TARGET_SEGV_MAPERR : TARGET_SEGV_ACCERR,
                    addr);
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}

void cpu_loop_exit_sigbus(CPUState *cpu, target_ulong addr,
                          MMUAccessType access_type, uintptr_t ra)
{
    const struct TCGCPUOps *tcg_ops = CPU_GET_CLASS(cpu)->tcg_ops;

    if (tcg_ops->record_sigbus) {
        tcg_ops->record_sigbus(cpu, addr, access_type, ra);
    }

    force_sig_fault(TARGET_SIGBUS, TARGET_BUS_ADRALN, addr);
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}
