/*
 * Wrappers around Linux futex syscall and similar
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Author:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/*
 * Note that a wake-up can also be caused by common futex usage patterns in
 * unrelated code that happened to have previously used the futex word's
 * memory location (e.g., typical futex-based implementations of Pthreads
 * mutexes can cause this under some conditions).  Therefore, qemu_futex_wait()
 * callers should always conservatively assume that it is a spurious wake-up,
 * and use the futex word's value (i.e., the user-space synchronization scheme)
 * to decide whether to continue to block or not.
 */

#ifndef QEMU_FUTEX_H
#define QEMU_FUTEX_H

#include "qemu/timer.h"
#define HAVE_FUTEX

#ifdef CONFIG_LINUX
#include <sys/syscall.h>
#include <linux/futex.h>
#include <linux/time_types.h>

#ifdef __NR_futex_time64
#define qemu_futex(...)              syscall(__NR_futex_time64, __VA_ARGS__)
#else
#define qemu_futex(...)              syscall(__NR_futex, __VA_ARGS__)
#endif

static inline void qemu_futex_wake_all(void *f)
{
    qemu_futex(f, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
}

static inline void qemu_futex_wake_single(void *f)
{
    qemu_futex(f, FUTEX_WAKE, 1, NULL, NULL, 0);
}

static inline bool qemu_futex_timedwait(void *f, unsigned val, int64_t ns)
{
    struct __kernel_timespec ts;
    uint32_t bitset = FUTEX_BITSET_MATCH_ANY;

    ts.tv_sec = ns / NANOSECONDS_PER_SECOND;
    ts.tv_nsec = ns % NANOSECONDS_PER_SECOND;

    while (qemu_futex(f, FUTEX_WAIT_BITSET, (int) val, &ts, NULL, bitset)) {
        switch (errno) {
        case EWOULDBLOCK:
            return true;
        case EINTR:
            break; /* get out of switch and retry */
        case ETIMEDOUT:
            return false;
        default:
            abort();
        }
    }

    return true;
}
#elif defined(CONFIG_WIN32)
#include <synchapi.h>

static inline void qemu_futex_wake_all(void *f)
{
    WakeByAddressAll(f);
}

static inline void qemu_futex_wake_single(void *f)
{
    WakeByAddressSingle(f);
}

static inline bool qemu_futex_timedwait(void *f, unsigned val, int64_t ns)
{
    int64_t now = get_clock();
    DWORD duration = MIN((ns - now) / SCALE_MS, INFINITE);

    return ns > now && WaitOnAddress(f, &val, sizeof(val), duration);
}
#else
#undef HAVE_FUTEX
#endif

#ifdef HAVE_FUTEX
static inline void qemu_futex_wait(void *f, unsigned val)
{
    qemu_futex_timedwait(f, val, INT64_MAX);
}
#endif

#endif /* QEMU_FUTEX_H */
