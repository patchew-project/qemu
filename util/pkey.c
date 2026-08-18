/*
 * QEMU guest memory protection key helpers
 *
 * Copyright (c) 2026 Google LLC
 *
 * Author:
 *  Jacky Li <jackyli@google.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/cpu-common.h"
#include "qemu/error-report.h"

#if defined(HOST_X86_64) && defined(CONFIG_LINUX)
#include <asm/unistd.h>
#include <cpuid.h>
#include <immintrin.h>
#include <linux/kvm.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#include "qemu/atomic.h"

#ifndef PR_SET_SPECULATION_CTRL
#define PR_SET_SPECULATION_CTRL 53
#endif
#ifndef PR_SPEC_INDIRECT_BRANCH
#define PR_SPEC_INDIRECT_BRANCH 1
#endif
#ifndef PR_SPEC_DISABLE
#define PR_SPEC_DISABLE (1UL << 2)
#endif

#ifndef PKEY_DISABLE_ACCESS
#define PKEY_DISABLE_ACCESS 0x1
#endif

static int guest_memory_pkey = -1;

/* Each protection key occupies exactly 2 bits in the PKRU register. */
#define BITS_PER_KEY 2
/* The mask used to extract/write the 2-bit permission flags. */
#define KEY_MASK 3U
/* Max protection keys supported on x86_64 */
#define KEY_COUNT 16

static inline __attribute__((target("pku"), always_inline))
uint32_t rdpkru(void)
{
    return _rdpkru_u32();
}

static inline __attribute__((target("pku"), always_inline)) void wrpkru(
        uint32_t pkru)
{
    _wrpkru(pkru);
}

static inline __attribute__((target("pku"), always_inline)) int inline_pkey_get(
        int pkey)
{
    uint32_t pkru = rdpkru();
    return (pkru >> (pkey * BITS_PER_KEY)) & KEY_MASK;
}

static inline __attribute__((target("pku"), always_inline)) void
inline_pkey_set(int pkey, unsigned int access_rights)
{
    uint32_t pkru = rdpkru();
    pkru &= ~(KEY_MASK << (pkey * BITS_PER_KEY));
    pkru |= ((access_rights & KEY_MASK) << (pkey * BITS_PER_KEY));
    wrpkru(pkru);
}

static inline __attribute__((always_inline)) intptr_t local_syscall3(
        intptr_t num, intptr_t arg1, intptr_t arg2, intptr_t arg3)
{
    intptr_t ret = num;
    asm volatile("syscall\n"
               : "+a"(ret)
               : "D"(arg1), "S"(arg2), "d"(arg3)
               : "rcx", "r11", "memory");
    return ret;
}

/*
 * Custom structures to parse FPU xstate and extract the PKRU register.
 * These match the Linux kernel x86_64 signal frame ABI.
 */
struct fpstate_64 {
    uint16_t cwd;
    uint16_t swd;
    uint16_t twd;
    uint16_t fop;
    uint64_t rip;
    uint64_t rdp;
    uint32_t mxcsr;
    uint32_t mxcsr_mask;
    uint32_t st_space[32];  /*  8x  FP registers, 16 bytes each */
    uint32_t xmm_space[64]; /* 16x XMM registers, 16 bytes each */
    uint32_t reserved1[12];
    union {
        uint32_t reserved2[12];
        struct {
            uint32_t magic1;
            uint32_t extended_size;
            uint64_t xstate_bv;
            uint32_t xstate_size;
            uint32_t padding[7];
        } sw_reserved; /* Extended state is encoded here */
    };
};

#define FP_XSTATE_MAGIC1 0x46505853U
#define FP_XSTATE_MAGIC2 0x46505845U
#define XFEATURE_PKRU 9
#define XSTATE_PKRU (1ULL << XFEATURE_PKRU)

static int pkru_offset = -2; /* -2 means uninitialized */

static int get_pkru_offset(void)
{
    uint32_t eax, ebx, ecx, edx;
    __cpuid_count(0xd, XFEATURE_PKRU, eax, ebx, ecx, edx);
    if (ebx == 0) {
        return -1;
    }
    return (int)ebx;
}

static int qemu_get_pkru_offset(void)
{
    if (pkru_offset == -2) {
        pkru_offset = get_pkru_offset();
    }
    return pkru_offset;
}

static __attribute__((target("pku"))) bool do_qemu_pkey_sigsegv_recovery(
    const siginfo_t *si, ucontext_t *ucontext, int pkey)
{
    if (pkey < 0 || pkey >= KEY_COUNT) {
        return false;
    }

    if (!ucontext) {
        return false;
    }

    void *fpstate = ucontext->uc_mcontext.fpregs;
    if (fpstate == NULL) {
        return false;
    }

    struct fpstate_64 *fpstate_64 = (struct fpstate_64 *)fpstate;

    if (fpstate_64->sw_reserved.magic1 != FP_XSTATE_MAGIC1) {
        return false;
    }

    uint32_t *magic2 =
            (uint32_t *)((char *)fpstate + fpstate_64->sw_reserved.xstate_size);
    if (*magic2 != FP_XSTATE_MAGIC2) {
        return false;
    }

    if ((fpstate_64->sw_reserved.xstate_bv & XSTATE_PKRU) == 0) {
        return false;
    }

    int pkr_offset = qemu_get_pkru_offset();
    if (pkr_offset < 0 || pkr_offset >= fpstate_64->sw_reserved.xstate_size) {
        return false;
    }

    uint32_t *pkru = (uint32_t *)((char *)fpstate + pkr_offset);

    uint32_t access_rights = (*pkru >> (pkey * BITS_PER_KEY)) & KEY_MASK;
    if (access_rights == 0) {
        return false;
    }

    /*
     * Flush microarchitectural state (IBPB) before returning to avoid
     * speculative execution.
     */
    prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_INDIRECT_BRANCH, PR_SPEC_DISABLE, 0,
                0);

    /*
     * Clear bits in the saved PKRU so that access is unrestricted upon
     * returning from the signal handler.
     */
    *pkru &= ~(KEY_MASK << (pkey * BITS_PER_KEY));

    return true;
}

static void (*old_sigaction_func)(int, siginfo_t *, void *);
static void (*old_sighandler_func)(int);

static int pkey_recovery_handler_installed;
static int pkey_for_recovery = -1;

static void qemu_pkey_sigsegv_handler(int si_signo, siginfo_t *si,
                                      void *raw_ucontext)
{
    ucontext_t *const ucontext = (ucontext_t *)raw_ucontext;

    if (si_signo == SIGSEGV && si != NULL && si->si_code == SEGV_PKUERR) {
        int pkey = qatomic_read(&pkey_for_recovery);
        if (pkey >= 0 && do_qemu_pkey_sigsegv_recovery(si, ucontext, pkey)) {
            return;
        }
    }

    void (*old_sigaction)(int, siginfo_t *, void *) =
            qatomic_read(&old_sigaction_func);
    if (old_sigaction != NULL) {
        old_sigaction(si_signo, si, raw_ucontext);
        return;
    }

    void (*old_sighandler)(int) = qatomic_read(&old_sighandler_func);
    if (old_sighandler != NULL && old_sighandler != SIG_DFL &&
            old_sighandler != SIG_IGN)
{
        old_sighandler(si_signo);
        return;
    }

    /* Fallback: abort */
    const char msg[] = "QEMU: Received unexpected Pkey SIGSEGV\n";
    int unused __attribute__((unused)) =
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
    abort();
}

static void qemu_register_pkey_recovery_handler(void)
{
    struct sigaction old_sigact = {0};
    struct sigaction new_sigact = {0};

    if (qatomic_xchg(&pkey_recovery_handler_installed, 1)) {
        return; /* Already installed */
    }

    if (sigaction(SIGSEGV, NULL, &old_sigact) < 0) {
        error_report("QEMU Pkey: Failed to get current SIGSEGV handler");
        qatomic_set(&pkey_recovery_handler_installed, 0);
        return;
    }

    if (old_sigact.sa_flags & SA_RESETHAND) {
        error_report(
            "QEMU Pkey: Incompatible SA_RESETHAND flags in old handler");
        qatomic_set(&pkey_recovery_handler_installed, 0);
        return;
    }

    if (old_sigact.sa_flags & SA_SIGINFO) {
        qatomic_set(&old_sigaction_func, old_sigact.sa_sigaction);
    } else {
        qatomic_set(&old_sighandler_func, old_sigact.sa_handler);
    }

    new_sigact = old_sigact;
    new_sigact.sa_flags |= SA_SIGINFO;
    new_sigact.sa_sigaction = &qemu_pkey_sigsegv_handler;

    if (sigaction(SIGSEGV, &new_sigact, &old_sigact) < 0) {
        error_report("QEMU Pkey: Failed to register SIGSEGV handler");
        qatomic_set(&pkey_recovery_handler_installed, 0);
        return;
    }
}

static void qemu_add_pkey_for_recovery(int pkey)
{
    int expected = -1;
    /* We only support one recovery pkey at a time */
    if (qatomic_cmpxchg(&pkey_for_recovery, expected, pkey) != expected) {
        error_report("QEMU Pkey: Recovery Pkey already set to %d",
                     pkey_for_recovery);
    }
}

__attribute__((target("pku"))) void qemu_init_guest_memory_pkey(void)
{
    if (guest_memory_pkey != -1) {
        return;
    }

    const char *enable_pkey = getenv("QEMU_ENABLE_PKEY_GUEST_MEMORY");
    if (enable_pkey && strcmp(enable_pkey, "1") == 0) {
        int pkey = pkey_alloc(0, 0);
        if (pkey == -1) {
            error_report("pkey_alloc failed for guest memory: %s",
                         strerror(errno));
        } else {
            /* Register recovery signal handler and add pkey for recovery. */
            qemu_register_pkey_recovery_handler();
            qemu_add_pkey_for_recovery(pkey);

            guest_memory_pkey = pkey;
        }
    }
}

__attribute__((target("pku"))) int qemu_pkey_mprotect_guest_memory(void *addr,
                                                                   size_t len,
                                                                   int prot)
{
    int pkey = guest_memory_pkey;
    if (pkey == -1) {
        return 0;
    }
    return pkey_mprotect(addr, len, prot, pkey);
}

__attribute__((target("pku"))) void qemu_reset_pkey_with_ibpb(void)
{
    int pkey = guest_memory_pkey;
    if (pkey == -1) {
        return;
    }
    if (inline_pkey_get(pkey) == 0) {
        return;
    }

    /* IBPB */
    prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_INDIRECT_BRANCH, PR_SPEC_DISABLE, 0,
                0);

    inline_pkey_set(pkey, 0);
}

__attribute__((target("pku"))) int qemu_pkey_kvm_run(int fd, void *arg)
{
    int pkey = guest_memory_pkey;
    if (pkey == -1) {
        return ioctl(fd, KVM_RUN, arg);
    }

    assert(pkey >= 0 && pkey < KEY_COUNT);

    inline_pkey_set(pkey, 0);

    intptr_t ret = local_syscall3(__NR_ioctl, fd, KVM_RUN, (intptr_t)arg);

    inline_pkey_set(pkey, PKEY_DISABLE_ACCESS);

    if (ret < 0) {
        errno = -ret;
        ret = -1;
    }
    return (int)ret;
}

#else
/* Dummy implementations for all other configurations (non-x86_64 Linux, */
/* Windows, macOS, etc.) */
#if defined(CONFIG_LINUX)
#include <linux/kvm.h>
#include <sys/ioctl.h>

int qemu_pkey_kvm_run(int fd, void *arg)
{ return ioctl(fd, KVM_RUN, arg); }
#endif

void qemu_init_guest_memory_pkey(void)
{}

void qemu_reset_pkey_with_ibpb(void)
{}

int qemu_pkey_mprotect_guest_memory(void *addr, size_t len, int prot)
{
    return 0;
}
#endif
