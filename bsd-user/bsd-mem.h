/*
 *  memory management system call shims and definitions
 *
 *  Copyright (c) 2013-15 Stacey D. Son
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
 * Copyright (c) 1982, 1986, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef BSD_USER_BSD_MEM_H
#define BSD_USER_BSD_MEM_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <fcntl.h>

#include "qemu-bsd.h"

extern struct bsd_shm_regions bsd_shm_regions[];
extern abi_ulong target_brk;
extern abi_ulong initial_target_brk;

/* mmap(2) */
static inline abi_long do_bsd_mmap(void *cpu_env, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6, abi_long arg7,
    abi_long arg8)
{
    if (regpairs_aligned(cpu_env) != 0) {
        arg6 = arg7;
        arg7 = arg8;
    }
    return get_errno(target_mmap(arg1, arg2, arg3,
                                 target_to_host_bitmask(arg4, mmap_flags_tbl),
                                 arg5, target_arg64(arg6, arg7)));
}

/* munmap(2) */
static inline abi_long do_bsd_munmap(abi_long arg1, abi_long arg2)
{
    return get_errno(target_munmap(arg1, arg2));
}

/* mprotect(2) */
static inline abi_long do_bsd_mprotect(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    return get_errno(target_mprotect(arg1, arg2, arg3));
}

/* msync(2) */
static inline abi_long do_bsd_msync(abi_long addr, abi_long len, abi_long flags)
{
    if (!guest_range_valid_untagged(addr, len)) {
        /* It seems odd, but POSIX wants this to be ENOMEM */
        return -TARGET_ENOMEM;
    }

    return get_errno(msync(g2h_untagged(addr), len, flags));
}

/* mlock(2) */
static inline abi_long do_bsd_mlock(abi_long arg1, abi_long arg2)
{
    if (!guest_range_valid_untagged(arg1, arg2)) {
        return -TARGET_EINVAL;
    }
    return get_errno(mlock(g2h_untagged(arg1), arg2));
}

/* munlock(2) */
static inline abi_long do_bsd_munlock(abi_long arg1, abi_long arg2)
{
    if (!guest_range_valid_untagged(arg1, arg2)) {
        return -TARGET_EINVAL;
    }
    return get_errno(munlock(g2h_untagged(arg1), arg2));
}

/* mlockall(2) */
static inline abi_long do_bsd_mlockall(abi_long arg1)
{
    return get_errno(mlockall(arg1));
}

/* munlockall(2) */
static inline abi_long do_bsd_munlockall(void)
{
    return get_errno(munlockall());
}

/* madvise(2) */
static inline abi_long do_bsd_madvise(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    abi_ulong len;
    int ret = 0;
    abi_long start = arg1;
    abi_long len_in = arg2;
    abi_long advice = arg3;

    if (start & ~TARGET_PAGE_MASK) {
        return -TARGET_EINVAL;
    }
    if (len_in == 0) {
        return 0;
    }
    len = TARGET_PAGE_ALIGN(len_in);
    if (len == 0 || !guest_range_valid_untagged(start, len)) {
        return -TARGET_EINVAL;
    }

    /*
     * Most advice values are hints, so ignoring and returning success is ok.
     *
     * However, some advice values such as MADV_DONTNEED, are not hints and
     * need to be emulated.
     *
     * A straight passthrough for those may not be safe because qemu sometimes
     * turns private file-backed mappings into anonymous mappings.
     * If all guest pages have PAGE_PASSTHROUGH set, mappings have the
     * same semantics for the host as for the guest.
     *
     * MADV_DONTNEED is passed through, if possible.
     * If passthrough isn't possible, we nevertheless (wrongly!) return
     * success, which is broken but some userspace programs fail to work
     * otherwise. Completely implementing such emulation is quite complicated
     * though.
     */
    mmap_lock();
    switch (advice) {
    case MADV_DONTNEED:
        if (page_check_range(start, len, PAGE_PASSTHROUGH)) {
            ret = get_errno(madvise(g2h_untagged(start), len, advice));
            if (ret == 0) {
                page_reset_target_data(start, start + len - 1);
            }
        }
    }
    mmap_unlock();

    return ret;
}

/* minherit(2) */
static inline abi_long do_bsd_minherit(abi_long addr, abi_long len,
        abi_long inherit)
{
    return get_errno(minherit(g2h_untagged(addr), len, inherit));
}

#endif /* BSD_USER_BSD_MEM_H */
