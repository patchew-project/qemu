/*
 * miscellaneous FreeBSD system call shims
 *
 * Copyright (c) 2013-2014 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef OS_MISC_H
#define OS_MISC_H

#include <sys/cpuset.h>
#include <sys/random.h>
#include <sched.h>
#include <kenv.h>

int shm_open2(const char *path, int flags, mode_t mode, int shmflags,
    const char *);

/* sched_setparam(2) */
static inline abi_long do_freebsd_sched_setparam(pid_t pid,
        abi_ulong target_sp_addr)
{
    abi_long ret;
    struct sched_param host_sp;

    ret = get_user_s32(host_sp.sched_priority, target_sp_addr);
    if (!is_error(ret)) {
        ret = get_errno(sched_setparam(pid, &host_sp));
    }
    return ret;
}

/* sched_get_param(2) */
static inline abi_long do_freebsd_sched_getparam(pid_t pid,
        abi_ulong target_sp_addr)
{
    abi_long ret;
    struct sched_param host_sp;

    ret = get_errno(sched_getparam(pid, &host_sp));
    if (!is_error(ret)) {
        ret = put_user_s32(host_sp.sched_priority, target_sp_addr);
    }
    return ret;
}

/* sched_setscheduler(2) */
static inline abi_long do_freebsd_sched_setscheduler(pid_t pid, int policy,
        abi_ulong target_sp_addr)
{
    abi_long ret;
    struct sched_param host_sp;

    ret = get_user_s32(host_sp.sched_priority, target_sp_addr);
    if (!is_error(ret)) {
        ret = get_errno(sched_setscheduler(pid, policy, &host_sp));
    }
    return ret;
}

/* sched_getscheduler(2) */
static inline abi_long do_freebsd_sched_getscheduler(pid_t pid)
{

    return get_errno(sched_getscheduler(pid));
}

/* sched_getscheduler(2) */
static inline abi_long do_freebsd_sched_rr_get_interval(pid_t pid,
        abi_ulong target_ts_addr)
{
    abi_long ret;
    struct timespec host_ts;

    ret = get_errno(sched_rr_get_interval(pid, &host_ts));
    if (!is_error(ret)) {
        ret = h2t_freebsd_timespec(target_ts_addr, &host_ts);
    }
    return ret;
}

/* cpuset(2) */
static inline abi_long do_freebsd_cpuset(abi_ulong target_cpuid)
{
    abi_long ret;
    cpusetid_t setid;

    ret = get_errno(cpuset(&setid));
    if (is_error(ret)) {
        return ret;
    }
    return put_user_s32(setid, target_cpuid);
}

#define target_to_host_cpuset_which(hp, t) { \
    (*hp) = t;                               \
} while (0)

#define target_to_host_cpuset_level(hp, t) { \
    (*hp) = t;                               \
} while (0)

/* cpuset_setid(2) */
static inline abi_long do_freebsd_cpuset_setid(CPUArchState *env, abi_long arg1,
        abi_ulong arg2, abi_ulong arg3, abi_ulong arg4, abi_ulong arg5)
{
    id_t id;    /* 64-bit value */
    cpusetid_t setid;
    cpuwhich_t which;

    target_to_host_cpuset_which(&which, arg1);
#if TARGET_ABI_BITS == 32
    /* See if we need to align the register pairs */
    if (regpairs_aligned(env)) {
        id = target_arg64(arg3, arg4);
        setid = arg5;
    } else {
        id = target_arg64(arg2, arg3);
        setid = arg4;
    }
#else
    id = arg2;
    setid = arg3;
#endif
    return get_errno(cpuset_setid(which, id, setid));
}

/* cpuset_getid(2) */
static inline abi_long do_freebsd_cpuset_getid(abi_long arg1, abi_ulong arg2,
        abi_ulong arg3, abi_ulong arg4, abi_ulong arg5)
{
    abi_long ret;
    id_t id;    /* 64-bit value */
    cpusetid_t setid;
    cpuwhich_t which;
    cpulevel_t level;
    abi_ulong target_setid;

    target_to_host_cpuset_which(&which, arg1)
        ;
    target_to_host_cpuset_level(&level, arg2);
#if TARGET_ABI_BITS == 32
    id = target_arg64(arg3, arg4);
    target_setid = arg5;
#else
    id = arg3;
    target_setid = arg4;
#endif
    ret = get_errno(cpuset_getid(level, which, id, &setid));
    if (is_error(ret)) {
        return ret;
    }
    return put_user_s32(setid, target_setid);
}

static abi_ulong copy_from_user_cpuset_mask(cpuset_t *mask,
        abi_ulong target_mask_addr)
{
        int i, j, k;
        abi_ulong b, *target_mask;

        target_mask = lock_user(VERIFY_READ, target_mask_addr,
                                CPU_SETSIZE / 8, 1);
        if (target_mask == NULL) {
                return -TARGET_EFAULT;
        }
        CPU_ZERO(mask);
        k = 0;
        for (i = 0; i < ((CPU_SETSIZE / 8) / sizeof(abi_ulong)); i++) {
                __get_user(b, &target_mask[i]);
                for (j = 0; j < TARGET_ABI_BITS; j++) {
                        if ((b >> j) & 1) {
                                CPU_SET(k, mask);
                        }
                        k++;
                }
        }
        unlock_user(target_mask, target_mask_addr, 0);

        return 0;
}

static abi_ulong copy_to_user_cpuset_mask(abi_ulong target_mask_addr,
        cpuset_t *mask)
{
        int i, j, k;
        abi_ulong b, *target_mask;

        target_mask = lock_user(VERIFY_WRITE, target_mask_addr,
                                CPU_SETSIZE / 8, 0);
        if (target_mask == NULL) {
                return -TARGET_EFAULT;
        }
        k = 0;
        for (i = 0; i < ((CPU_SETSIZE / 8) / sizeof(abi_ulong)); i++) {
                b = 0;
                for (j = 0; j < TARGET_ABI_BITS; j++) {
                        b |= ((CPU_ISSET(k, mask) != 0) << j);
                        k++;
                }
                __put_user(b, &target_mask[i]);
        }
        unlock_user(target_mask, target_mask_addr, (CPU_SETSIZE / 8));

        return 0;
}

/* cpuset_getaffinity(2) */
/* cpuset_getaffinity(cpulevel_t, cpuwhich_t, id_t, size_t, cpuset_t *); */
static inline abi_long do_freebsd_cpuset_getaffinity(cpulevel_t level,
        cpuwhich_t which, abi_ulong arg3, abi_ulong arg4, abi_ulong arg5,
        abi_ulong arg6)
{
        cpuset_t mask;
        abi_long ret;
    id_t id;    /* 64-bit */
    abi_ulong setsize, target_mask;

#if TARGET_ABI_BITS == 32
    id = (id_t)target_arg64(arg3, arg4);
    setsize = arg5;
    target_mask = arg6;
#else
    id = (id_t)arg3;
    setsize = arg4;
    target_mask = arg5;
#endif

        ret = get_errno(cpuset_getaffinity(level, which, id, setsize, &mask));
        if (ret == 0) {
                ret = copy_to_user_cpuset_mask(target_mask, &mask);
        }

    return ret;
}

/* cpuset_setaffinity(2) */
/* cpuset_setaffinity(cpulevel_t, cpuwhich_t, id_t, size_t, const cpuset_t *);*/
static inline abi_long do_freebsd_cpuset_setaffinity(cpulevel_t level,
        cpuwhich_t which, abi_ulong arg3, abi_ulong arg4, abi_ulong arg5,
        abi_ulong arg6)
{
        cpuset_t mask;
        abi_long ret;
    id_t id; /* 64-bit */
    abi_ulong setsize, target_mask;

#if TARGET_ABI_BITS == 32
    id = (id_t)target_arg64(arg3, arg4);
    setsize = arg5;
    target_mask = arg6;
#else
    id = (id_t)arg3;
    setsize = arg4;
    target_mask = arg5;
#endif

        ret = copy_from_user_cpuset_mask(&mask, target_mask);
        if (ret == 0) {
                ret = get_errno(cpuset_setaffinity(level, which, id, setsize,
                                                   &mask));
        }

        return ret;
}

/*
 * Pretend there are no modules loaded into the kernel. Don't allow loading or
 * unloading of modules. This works well for tests, and little else seems to
 * care. Will reevaluate if examples are found that do matter.
 */


#endif /* OS_MISC_H */
