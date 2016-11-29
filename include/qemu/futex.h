/*
 * Wrappers around Linux futex syscall
 *
 * Copyright Red Hat, Inc. 2015
 *
 * Author:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <sys/syscall.h>
#include <linux/futex.h>

#define futex(...)              syscall(__NR_futex, __VA_ARGS__)

static inline void futex_wake(void *f, int n)
{
    futex(f, FUTEX_WAKE, n, NULL, NULL, 0);
}

static inline void futex_wait(void *f, unsigned val)
{
    while (futex(f, FUTEX_WAIT, (int) val, NULL, NULL, 0)) {
        switch (errno) {
        case EWOULDBLOCK:
            return;
        case EINTR:
            break; /* get out of switch and retry */
        default:
            abort();
        }
    }
}
