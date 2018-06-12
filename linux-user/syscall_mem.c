/*
 *  Linux memory-related syscalls
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
#include "qemu/cutils.h"
#include "qemu/path.h"
#include "qemu.h"
#include "syscall.h"
#include <elf.h>
#include <linux/unistd.h>


static bitmask_transtbl const mmap_flags_tbl[] = {
    { TARGET_MAP_SHARED, TARGET_MAP_SHARED, MAP_SHARED, MAP_SHARED },
    { TARGET_MAP_PRIVATE, TARGET_MAP_PRIVATE, MAP_PRIVATE, MAP_PRIVATE },
    { TARGET_MAP_FIXED, TARGET_MAP_FIXED, MAP_FIXED, MAP_FIXED },
    { TARGET_MAP_ANONYMOUS, TARGET_MAP_ANONYMOUS,
      MAP_ANONYMOUS, MAP_ANONYMOUS },
    { TARGET_MAP_GROWSDOWN, TARGET_MAP_GROWSDOWN,
      MAP_GROWSDOWN, MAP_GROWSDOWN },
    { TARGET_MAP_DENYWRITE, TARGET_MAP_DENYWRITE,
      MAP_DENYWRITE, MAP_DENYWRITE },
    { TARGET_MAP_EXECUTABLE, TARGET_MAP_EXECUTABLE,
      MAP_EXECUTABLE, MAP_EXECUTABLE },
    { TARGET_MAP_LOCKED, TARGET_MAP_LOCKED, MAP_LOCKED, MAP_LOCKED },
    { TARGET_MAP_NORESERVE, TARGET_MAP_NORESERVE,
      MAP_NORESERVE, MAP_NORESERVE },
    { TARGET_MAP_HUGETLB, TARGET_MAP_HUGETLB, MAP_HUGETLB, MAP_HUGETLB },
    /* MAP_STACK had been ignored by the kernel for quite some time.
       Recognize it for the target insofar as we do not want to pass
       it through to the host.  */
    { TARGET_MAP_STACK, TARGET_MAP_STACK, 0, 0 },
    { 0, 0, 0, 0 }
};


SYSCALL_IMPL(mlock)
{
    return get_errno(mlock(g2h(arg1), arg2));
}
SYSCALL_DEF(mlock, ARG_PTR, ARG_DEC);

SYSCALL_IMPL(mlockall)
{
    int host_flag = 0;
    if (arg1 & TARGET_MLOCKALL_MCL_CURRENT) {
        host_flag |= MCL_CURRENT;
    }
    if (arg1 & TARGET_MLOCKALL_MCL_FUTURE) {
        host_flag |= MCL_FUTURE;
    }
    return get_errno(mlockall(host_flag));
}
SYSCALL_DEF(mlockall, ARG_HEX);

#if (defined(TARGET_I386) && defined(TARGET_ABI32)) || \
    (defined(TARGET_ARM) && defined(TARGET_ABI32)) || \
    defined(TARGET_M68K) || defined(TARGET_CRIS) || \
    defined(TARGET_MICROBLAZE) || defined(TARGET_S390X)
SYSCALL_ARGS(mmap)
{
    abi_ulong ptr = in[0];
    abi_long *v = lock_user(VERIFY_READ, ptr, 6 * sizeof(abi_long), 1);
    if (v == NULL) {
        errno = EFAULT;
        return NULL;
    }
    out[0] = tswapal(v[0]);
    out[1] = tswapal(v[1]);
    out[2] = tswapal(v[2]);
    out[3] = tswapal(v[3]);
    out[4] = tswapal(v[4]);
    out[5] = tswapal(v[5]);
    unlock_user(v, ptr, 0);
    return def;
}
#else
# define args_mmap NULL
#endif

SYSCALL_IMPL(mmap)
{
    int host_flags = target_to_host_bitmask(arg4, mmap_flags_tbl);
    return get_errno(target_mmap(arg1, arg2, arg3, host_flags, arg5, arg6));
}

const SyscallDef def_mmap = {
    .name = "mmap",
    .args = args_mmap,
    .impl = impl_mmap,
    .print_ret = print_syscall_ptr_ret,
    .arg_type = { ARG_PTR, ARG_DEC, ARG_MMAPPROT,
                  ARG_MMAPFLAG, ARG_DEC, ARG_DEC }
};

#ifdef TARGET_NR_mmap2
/* Define mmap2 in terms of mmap.  */
/* Note that there is a fundamental problem here in that
 * target_mmap has an offset parameter that is abi_ulong
 * and not off_t.  This means that we cannot actually pass
 * through a 64-bit file offset as intended.
 */

#ifndef MMAP_SHIFT
# define MMAP_SHIFT 12
#endif

SYSCALL_ARGS(mmap2)
{
    /* We have already assigned out[0-4].  */
    out[5] = (uint64_t)(abi_ulong)in[5] << MMAP_SHIFT;
    return def;
}

const SyscallDef def_mmap2 = {
    .name = "mmap2",
    .args = args_mmap2,
    .impl = impl_mmap,
    .print_ret = print_syscall_ptr_ret,
    .arg_type = { ARG_PTR, ARG_DEC, ARG_MMAPPROT,
                  ARG_MMAPFLAG, ARG_DEC, ARG_DEC64 },
};
#endif

SYSCALL_IMPL(mprotect)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    TaskState *ts = cpu->opaque;

    /* Special hack to detect libc making the stack executable.  */
    if ((arg3 & PROT_GROWSDOWN)
        && arg1 >= ts->info->stack_limit
        && arg1 <= ts->info->start_stack) {
        arg3 &= ~PROT_GROWSDOWN;
        arg2 = arg2 + arg1 - ts->info->stack_limit;
        arg1 = ts->info->stack_limit;
    }
    return get_errno(target_mprotect(arg1, arg2, arg3));
}
SYSCALL_DEF(mprotect, ARG_PTR, ARG_DEC, ARG_MMAPPROT);

SYSCALL_IMPL(mremap)
{
    return get_errno(target_mremap(arg1, arg2, arg3, arg4, arg5));
}

const SyscallDef def_mremap = {
    .name = "mremap",
    .impl = impl_mremap,
    .print_ret = print_syscall_ptr_ret,
    .arg_type = { ARG_PTR, ARG_DEC, ARG_DEC, ARG_HEX, ARG_PTR }
};

SYSCALL_IMPL(msync)
{
    return get_errno(msync(g2h(arg1), arg2, arg3));
}
SYSCALL_DEF(msync, ARG_PTR, ARG_DEC, ARG_HEX);

SYSCALL_IMPL(munlock)
{
    return get_errno(munlock(g2h(arg1), arg2));
}
SYSCALL_DEF(munlock, ARG_PTR, ARG_DEC);

SYSCALL_IMPL(munlockall)
{
    return get_errno(munlockall());
}
SYSCALL_DEF(munlockall);

SYSCALL_IMPL(munmap)
{
    return get_errno(target_munmap(arg1, arg2));
}
SYSCALL_DEF(munmap, ARG_PTR, ARG_DEC);
