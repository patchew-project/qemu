/*
 *  BSD syscalls
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *  Copyright (c) 2013-2014 Stacey D. Son
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

/*
 * We need the FreeBSD "legacy" definitions. Rust needs the FreeBSD 11 system
 * calls, so we have to emulate that despite FreeBSD being EOL'd.
 */
#define _WANT_FREEBSD11_STAT
#define _WANT_FREEBSD11_STATFS
#define _WANT_FREEBSD11_DIRENT
#define _WANT_KERNEL_ERRNO
#define _WANT_SEMUN
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/path.h"
#include <sys/syscall.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <utime.h>

#include "qemu.h"
#include "qemu-common.h"
#include "signal-common.h"
#include "user/syscall-trace.h"

#include "bsd-file.h"

void target_set_brk(abi_ulong new_brk)
{
}

/*
 * errno conversion.
 */
abi_long get_errno(abi_long ret)
{

    if (ret == -1) {
        return -host_to_target_errno(errno);
    } else {
        return ret;
    }
}

int host_to_target_errno(int err)
{
    /*
     * All the BSDs have the property that the error numbers are uniform across
     * all architectures for a given BSD, though they may vary between different
     * BSDs.
     */
    return err;
}

bool is_error(abi_long ret)
{

    return (abi_ulong)ret >= (abi_ulong)(-4096);
}

struct iovec *lock_iovec(int type, abi_ulong target_addr,
        int count, int copy)
{
    struct target_iovec *target_vec;
    struct iovec *vec;
    abi_ulong total_len, max_len;
    int i;
    int err = 0;
    bool bad_address = false;

    if (count == 0) {
        errno = 0;
        return NULL;
    }
    if (count < 0 || count > IOV_MAX) {
        errno = EINVAL;
        return NULL;
    }

    vec = calloc(count, sizeof(struct iovec));
    if (vec == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec == NULL) {
        err = EFAULT;
        goto fail2;
    }

    /*
     * ??? If host page size > target page size, this will result in a value
     * larger than what we can actually support.
     */
    max_len = 0x7fffffff & TARGET_PAGE_MASK;
    total_len = 0;

    for (i = 0; i < count; i++) {
        abi_ulong base = tswapal(target_vec[i].iov_base);
        abi_long len = tswapal(target_vec[i].iov_len);

        if (len < 0) {
            err = EINVAL;
            goto fail;
        } else if (len == 0) {
            /* Zero length pointer is ignored.  */
            vec[i].iov_base = 0;
        } else {
            vec[i].iov_base = lock_user(type, base, len, copy);
            /*
             * If the first buffer pointer is bad, this is a fault.  But
             * subsequent bad buffers will result in a partial write; this is
             * realized by filling the vector with null pointers and zero
             * lengths.
             */
            if (!vec[i].iov_base) {
                if (i == 0) {
                    err = EFAULT;
                    goto fail;
                } else {
                    bad_address = true;
                }
            }
            if (bad_address) {
                len = 0;
            }
            if (len > max_len - total_len) {
                len = max_len - total_len;
            }
        }
        vec[i].iov_len = len;
        total_len += len;
    }

    unlock_user(target_vec, target_addr, 0);
    return vec;

 fail:
    while (--i >= 0) {
        if (tswapal(target_vec[i].iov_len) > 0) {
            unlock_user(vec[i].iov_base, tswapal(target_vec[i].iov_base), 0);
        }
    }
    unlock_user(target_vec, target_addr, 0);
 fail2:
    free(vec);
    errno = err;
    return NULL;
}

void unlock_iovec(struct iovec *vec, abi_ulong target_addr,
        int count, int copy)
{
    struct target_iovec *target_vec;
    int i;

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec) {
        for (i = 0; i < count; i++) {
            abi_ulong base = tswapal(target_vec[i].iov_base);
            abi_long len = tswapal(target_vec[i].iov_len);
            if (len < 0) {
                break;
            }
            unlock_user(vec[i].iov_base, base, copy ? vec[i].iov_len : 0);
        }
        unlock_user(target_vec, target_addr, 0);
    }

    free(vec);
}

/*
 * do_syscall() should always have a single exit point at the end so that
 * actions, such as logging of syscall results, can be performed.  All errnos
 * that do_syscall() returns must be -TARGET_<errcode>.
 */
abi_long do_freebsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    return 0;
}

void syscall_init(void)
{
}
