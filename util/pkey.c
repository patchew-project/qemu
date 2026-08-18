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

#else
/* Dummy implementations for all other configurations (non-x86_64 Linux, */
/* Windows, macOS, etc.) */
#if defined(CONFIG_LINUX)
#include <linux/kvm.h>
#include <sys/ioctl.h>
#endif

void qemu_init_guest_memory_pkey(void)
{}

int qemu_pkey_mprotect_guest_memory(void *addr, size_t len, int prot)
{
    return 0;
}
#endif
