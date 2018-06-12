/*
 *  Linux process related syscalls
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
#include <grp.h>
#include <sys/fsuid.h>
#include <linux/unistd.h>


/* We must do direct syscalls for setting UID/GID, because we want to
 * implement the Linux system call semantics of "change only for this thread",
 * not the libc/POSIX semantics of "change for all threads in process".
 * (See http://ewontfix.com/17/ for more details.)
 * We use the 32-bit version of the syscalls if present; if it is not
 * then either the host architecture supports 32-bit UIDs natively with
 * the standard syscall, or the 16-bit UID is the best we can do.
 */
#ifdef __NR_setuid32
#define __NR_sys_setuid __NR_setuid32
#else
#define __NR_sys_setuid __NR_setuid
#endif
#ifdef __NR_setgid32
#define __NR_sys_setgid __NR_setgid32
#else
#define __NR_sys_setgid __NR_setgid
#endif
#ifdef __NR_setresuid32
#define __NR_sys_setresuid __NR_setresuid32
#else
#define __NR_sys_setresuid __NR_setresuid
#endif
#ifdef __NR_setresgid32
#define __NR_sys_setresgid __NR_setresgid32
#else
#define __NR_sys_setresgid __NR_setresgid
#endif

_syscall1(int, sys_setuid, uid_t, uid)
_syscall1(int, sys_setgid, gid_t, gid)
_syscall3(int, sys_setresuid, uid_t, ruid, uid_t, euid, uid_t, suid)
_syscall3(int, sys_setresgid, gid_t, rgid, gid_t, egid, gid_t, sgid)

#ifndef __NR_gettid
#define __NR_gettid  -1
#endif
_syscall0(int, gettid)

#ifndef __NR_set_tid_address
#define __NR_set_tid_address  -1
#endif
_syscall1(int, set_tid_address, int *, tidptr)


/* CLONE_VFORK is special cased early in do_fork(). The other flag bits
 * have almost all been allocated. We cannot support any of
 * CLONE_NEWNS, CLONE_NEWCGROUP, CLONE_NEWUTS, CLONE_NEWIPC,
 * CLONE_NEWUSER, CLONE_NEWPID, CLONE_NEWNET, CLONE_PTRACE, CLONE_UNTRACED.
 * The checks against the invalid thread masks above will catch these.
 * (The one remaining unallocated bit is 0x1000 which used to be CLONE_PID.)
 */

#ifndef CLONE_IO
#define CLONE_IO                0x80000000      /* Clone io context */
#endif

/* We can't directly call the host clone syscall, because this will
 * badly confuse libc (breaking mutexes, for example). So we must
 * divide clone flags into:
 *  * flag combinations that look like pthread_create()
 *  * flag combinations that look like fork()
 *  * flags we can implement within QEMU itself
 *  * flags we can't support and will return an error for
 */
/* For thread creation, all these flags must be present; for
 * fork, none must be present.
 */
#define CLONE_THREAD_FLAGS                              \
    (CLONE_VM | CLONE_FS | CLONE_FILES |                \
     CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM)

/* These flags are ignored:
 * CLONE_DETACHED is now ignored by the kernel;
 * CLONE_IO is just an optimisation hint to the I/O scheduler
 */
#define CLONE_IGNORED_FLAGS                     \
    (CLONE_DETACHED | CLONE_IO)

/* Flags for fork which we can implement within QEMU itself */
#define CLONE_OPTIONAL_FORK_FLAGS               \
    (CLONE_SETTLS | CLONE_PARENT_SETTID |       \
     CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID)

/* Flags for thread creation which we can implement within QEMU itself */
#define CLONE_OPTIONAL_THREAD_FLAGS                             \
    (CLONE_SETTLS | CLONE_PARENT_SETTID |                       \
     CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID | CLONE_PARENT)

#define CLONE_INVALID_FORK_FLAGS                                        \
    (~(CSIGNAL | CLONE_OPTIONAL_FORK_FLAGS | CLONE_IGNORED_FLAGS))

#define CLONE_INVALID_THREAD_FLAGS                                      \
    (~(CSIGNAL | CLONE_THREAD_FLAGS | CLONE_OPTIONAL_THREAD_FLAGS |     \
       CLONE_IGNORED_FLAGS))

#define NEW_STACK_SIZE 0x40000

static pthread_mutex_t clone_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    CPUArchState *env;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    uint32_t tid;
    abi_ulong child_tidptr;
    abi_ulong parent_tidptr;
    sigset_t sigmask;
} new_thread_info;

static void *clone_func(void *arg)
{
    new_thread_info *info = arg;
    CPUArchState *env = info->env;
    CPUState *cpu = ENV_GET_CPU(env);
    TaskState *ts = (TaskState *)cpu->opaque;

    rcu_register_thread();
    tcg_register_thread();
    thread_cpu = cpu;
    info->tid = gettid();
    task_settid(ts);
    if (info->child_tidptr) {
        put_user_u32(info->tid, info->child_tidptr);
    }
    if (info->parent_tidptr) {
        put_user_u32(info->tid, info->parent_tidptr);
    }
    /* Enable signals.  */
    sigprocmask(SIG_SETMASK, &info->sigmask, NULL);
    /* Signal to the parent that we're ready.  */
    pthread_mutex_lock(&info->mutex);
    pthread_cond_broadcast(&info->cond);
    pthread_mutex_unlock(&info->mutex);
    /* Wait until the parent has finished initializing the tls state.  */
    pthread_mutex_lock(&clone_lock);
    pthread_mutex_unlock(&clone_lock);
    cpu_loop(env);
    /* never exits */
    return NULL;
}

static int do_fork(CPUArchState *env, unsigned int flags, abi_ulong newsp,
                   abi_ulong parent_tidptr, abi_ulong child_tidptr,
                   target_ulong newtls)
{
    CPUState *cpu = ENV_GET_CPU(env);
    int ret;
    TaskState *ts;
    CPUState *new_cpu;
    CPUArchState *new_env;
    sigset_t sigmask;

    flags &= ~CLONE_IGNORED_FLAGS;

    /* Emulate vfork() with fork() */
    if (flags & CLONE_VFORK) {
        flags &= ~(CLONE_VFORK | CLONE_VM);
    }

    if (flags & CLONE_VM) {
        TaskState *parent_ts = (TaskState *)cpu->opaque;
        new_thread_info info;
        pthread_attr_t attr;

        if (((flags & CLONE_THREAD_FLAGS) != CLONE_THREAD_FLAGS) ||
            (flags & CLONE_INVALID_THREAD_FLAGS)) {
            return -TARGET_EINVAL;
        }

        ts = g_new0(TaskState, 1);
        init_task_state(ts);

        /* Grab a mutex so that thread setup appears atomic.  */
        pthread_mutex_lock(&clone_lock);

        /* we create a new CPU instance. */
        new_env = cpu_copy(env);
        /* Init regs that differ from the parent.  */
        cpu_clone_regs(new_env, newsp);
        new_cpu = ENV_GET_CPU(new_env);
        new_cpu->opaque = ts;
        ts->bprm = parent_ts->bprm;
        ts->info = parent_ts->info;
        ts->signal_mask = parent_ts->signal_mask;

        if (flags & CLONE_CHILD_CLEARTID) {
            ts->child_tidptr = child_tidptr;
        }

        if (flags & CLONE_SETTLS) {
            cpu_set_tls(new_env, newtls);
        }

        memset(&info, 0, sizeof(info));
        pthread_mutex_init(&info.mutex, NULL);
        pthread_mutex_lock(&info.mutex);
        pthread_cond_init(&info.cond, NULL);
        info.env = new_env;
        if (flags & CLONE_CHILD_SETTID) {
            info.child_tidptr = child_tidptr;
        }
        if (flags & CLONE_PARENT_SETTID) {
            info.parent_tidptr = parent_tidptr;
        }

        ret = pthread_attr_init(&attr);
        ret = pthread_attr_setstacksize(&attr, NEW_STACK_SIZE);
        ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        /* It is not safe to deliver signals until the child has finished
           initializing, so temporarily block all signals.  */
        sigfillset(&sigmask);
        sigprocmask(SIG_BLOCK, &sigmask, &info.sigmask);

        /* If this is our first additional thread, we need to ensure we
         * generate code for parallel execution and flush old translations.
         */
        if (!parallel_cpus) {
            parallel_cpus = true;
            tb_flush(cpu);
        }

        ret = pthread_create(&info.thread, &attr, clone_func, &info);

        /* TODO: Free new CPU state if thread creation failed.  */

        sigprocmask(SIG_SETMASK, &info.sigmask, NULL);
        pthread_attr_destroy(&attr);
        if (ret == 0) {
            /* Wait for the child to initialize.  */
            pthread_cond_wait(&info.cond, &info.mutex);
            ret = info.tid;
        } else {
            ret = -host_to_target_errno(ret);
        }
        pthread_mutex_unlock(&info.mutex);
        pthread_cond_destroy(&info.cond);
        pthread_mutex_destroy(&info.mutex);
        pthread_mutex_unlock(&clone_lock);
    } else {
        /* if no CLONE_VM, we consider it is a fork */
        if (flags & CLONE_INVALID_FORK_FLAGS) {
            return -TARGET_EINVAL;
        }

        /* We can't support custom termination signals */
        if ((flags & CSIGNAL) != TARGET_SIGCHLD) {
            return -TARGET_EINVAL;
        }

        if (block_signals()) {
            return -TARGET_ERESTARTSYS;
        }

        fork_start();
        ret = fork();
        if (ret < 0) {
            return get_errno(-1);
        }
        if (ret == 0) {
            /* Child Process.  */
            cpu_clone_regs(env, newsp);
            fork_end(1);
            /* There is a race condition here.  The parent process could
               theoretically read the TID in the child process before the child
               tid is set.  This would require using either ptrace
               (not implemented) or having *_tidptr to point at a shared memory
               mapping.  We can't repeat the spinlock hack used above because
               the child process gets its own copy of the lock.  */
            if (flags & CLONE_CHILD_SETTID) {
                put_user_u32(gettid(), child_tidptr);
            }
            if (flags & CLONE_PARENT_SETTID) {
                put_user_u32(gettid(), parent_tidptr);
            }
            ts = (TaskState *)cpu->opaque;
            if (flags & CLONE_SETTLS) {
                cpu_set_tls(env, newtls);
            }
            if (flags & CLONE_CHILD_CLEARTID) {
                ts->child_tidptr = child_tidptr;
            }
        } else {
            fork_end(0);
        }
    }
    return ret;
}

SYSCALL_ARGS(clone)
{
    abi_ulong fl, sp, ptid, ctid, tls;

    /* Linux manages to have three different orderings for its
     * arguments to clone(); the BACKWARDS and BACKWARDS2 defines
     * match the kernel's CONFIG_CLONE_* settings.
     * Microblaze is further special in that it uses a sixth
     * implicit argument to clone for the TLS pointer.
     */
#if defined(TARGET_MICROBLAZE)
    fl = in[0], sp = in[1], ptid = in[3], ctid = in[4], tls = in[5];
#elif defined(TARGET_CLONE_BACKWARDS)
    fl = in[0], sp = in[1], ptid = in[2], tls = in[3], ctid = in[4];
#elif defined(TARGET_CLONE_BACKWARDS2)
    sp = in[0], fl = in[1], ptid = in[2], ctid = in[3], tls = in[4];
#else
    fl = in[0], sp = in[1], ptid = in[2], ctid = in[3], tls = in[4];
#endif
    out[0] = fl, out[1] = sp, out[2] = ptid, out[3] = ctid, out[4] = tls;
    return def;
}

SYSCALL_IMPL(clone)
{
    /* We've done all of the odd ABI adjustment above.  */
    return do_fork(cpu_env, arg1, arg2, arg3, arg4, arg5);
}
SYSCALL_DEF_ARGS(clone, ARG_CLONEFLAG, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR);

#ifdef TARGET_NR_fork
SYSCALL_IMPL(fork)
{
    return do_fork(cpu_env, TARGET_SIGCHLD, 0, 0, 0, 0);
}
SYSCALL_DEF(fork);
#endif

#ifdef TARGET_NR_vfork
SYSCALL_IMPL(vfork)
{
    return do_fork(cpu_env, CLONE_VFORK | CLONE_VM | TARGET_SIGCHLD,
                   0, 0, 0, 0);
}
SYSCALL_DEF(vfork);
#endif

#ifdef TARGET_NR_getegid
SYSCALL_IMPL(getegid)
{
    return get_errno(high2lowgid(getegid()));
}
SYSCALL_DEF(getegid);
#endif

#ifdef TARGET_NR_getegid32
SYSCALL_IMPL(getegid32)
{
    return get_errno(getegid());
}
SYSCALL_DEF(getegid32);
#endif

#ifdef TARGET_NR_geteuid
SYSCALL_IMPL(geteuid)
{
    return get_errno(high2lowuid(geteuid()));
}
SYSCALL_DEF(geteuid);
#endif

#ifdef TARGET_NR_geteuid32
SYSCALL_IMPL(geteuid32)
{
    return get_errno(geteuid());
}
SYSCALL_DEF(geteuid32);
#endif

#ifdef TARGET_NR_getgid
SYSCALL_IMPL(getgid)
{
    return get_errno(high2lowgid(getgid()));
}
SYSCALL_DEF(getgid);
#endif

#ifdef TARGET_NR_getgid32
SYSCALL_IMPL(getgid32)
{
    return get_errno(getgid());
}
SYSCALL_DEF(getgid32);
#endif

SYSCALL_IMPL(getgroups)
{
    int gidsetsize = arg1;
    gid_t *grouplist;
    abi_long ret;

    grouplist = g_try_new(gid_t, gidsetsize);
    if (!grouplist) {
        return -TARGET_ENOMEM;
    }
    ret = get_errno(getgroups(gidsetsize, grouplist));

    if (!is_error(ret) && gidsetsize != 0) {
        size_t target_grouplist_size = gidsetsize * sizeof(target_id);
        target_id *target_grouplist
            = lock_user(VERIFY_WRITE, arg2, target_grouplist_size, 0);
        if (target_grouplist) {
            int i;
            for (i = 0; i < ret; i++) {
                target_grouplist[i] = tswapid(high2lowgid(grouplist[i]));
            }
            unlock_user(target_grouplist, arg2, target_grouplist_size);
        } else {
            ret = -TARGET_EFAULT;
        }
    }
    g_free(grouplist);
    return ret;
}
SYSCALL_DEF(getgroups, ARG_DEC, ARG_PTR);

#ifdef TARGET_NR_getgroups32
SYSCALL_IMPL(getgroups32)
{
    int gidsetsize = arg1;
    gid_t *grouplist;
    abi_long ret;

    grouplist = g_try_new(gid_t, gidsetsize);
    if (!grouplist) {
        return -TARGET_ENOMEM;
    }
    ret = get_errno(getgroups(gidsetsize, grouplist));

    if (!is_error(ret) && gidsetsize != 0) {
        uint32_t *target_grouplist
            = lock_user(VERIFY_WRITE, arg2, gidsetsize * 4, 0);
        if (target_grouplist) {
            int i;
            for (i = 0; i < ret; i++) {
                target_grouplist[i] = tswap32(grouplist[i]);
            }
            unlock_user(target_grouplist, arg2, gidsetsize * 4);
        } else {
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}
SYSCALL_DEF(getgroups32, ARG_DEC, ARG_PTR);
#endif

#ifdef TARGET_NR_getresgid
SYSCALL_IMPL(getresgid)
{
    gid_t rgid, egid, sgid;
    abi_long ret = get_errno(getresgid(&rgid, &egid, &sgid));

    if (!is_error(ret) &&
        (put_user_id(high2lowgid(rgid), arg1) ||
         put_user_id(high2lowgid(egid), arg2) ||
         put_user_id(high2lowgid(sgid), arg3))) {
        return -TARGET_EFAULT;
    }
    return ret;
}
SYSCALL_DEF(getresgid, ARG_PTR, ARG_PTR, ARG_PTR);
#endif

#ifdef TARGET_NR_getresgid32
SYSCALL_IMPL(getresgid32)
{
    gid_t rgid, egid, sgid;
    abi_long ret = get_errno(getresgid(&rgid, &egid, &sgid));

    if (!is_error(ret) &&
        (put_user_u32(rgid, arg1) ||
         put_user_u32(egid, arg2) ||
         put_user_u32(sgid, arg3))) {
        return -TARGET_EFAULT;
    }
    return ret;
}
SYSCALL_DEF(getresgid32, ARG_PTR, ARG_PTR, ARG_PTR);
#endif

#ifdef TARGET_NR_getresuid
SYSCALL_IMPL(getresuid)
{
    uid_t ruid, euid, suid;
    abi_long ret = get_errno(getresuid(&ruid, &euid, &suid));

    if (!is_error(ret) &&
        (put_user_id(high2lowuid(ruid), arg1) ||
         put_user_id(high2lowuid(euid), arg2) ||
         put_user_id(high2lowuid(suid), arg3))) {
        return -TARGET_EFAULT;
    }
    return ret;
}
SYSCALL_DEF(getresuid, ARG_PTR, ARG_PTR, ARG_PTR);
#endif

#ifdef TARGET_NR_getresuid32
SYSCALL_IMPL(getresuid32)
{
    uid_t ruid, euid, suid;
    abi_long ret = get_errno(getresuid(&ruid, &euid, &suid));

    if (!is_error(ret) &&
        (put_user_u32(ruid, arg1) ||
         put_user_u32(euid, arg2) ||
         put_user_u32(suid, arg3))) {
        return -TARGET_EFAULT;
    }
    return ret;
}
SYSCALL_DEF(getresuid32, ARG_PTR, ARG_PTR, ARG_PTR);
#endif

#ifdef TARGET_NR_getpgrp
SYSCALL_IMPL(getpgrp)
{
    return get_errno(getpgrp());
}
SYSCALL_DEF(getpgrp);
#endif

#ifdef TARGET_NR_getpid
SYSCALL_IMPL(getpid)
{
    return get_errno(getpid());
}
SYSCALL_DEF(getpid);
#endif

#ifdef TARGET_NR_getppid
SYSCALL_IMPL(getppid)
{
    return get_errno(getppid());
}
SYSCALL_DEF(getppid);
#endif

SYSCALL_IMPL(gettid)
{
    return get_errno(gettid());
}
SYSCALL_DEF(gettid);

#ifdef TARGET_NR_getuid
SYSCALL_IMPL(getuid)
{
    return get_errno(high2lowuid(getuid()));
}
SYSCALL_DEF(getuid);
#endif

#ifdef TARGET_NR_getuid32
SYSCALL_IMPL(getuid32)
{
    return get_errno(getuid());
}
SYSCALL_DEF(getuid32);
#endif

#ifdef TARGET_NR_getxgid
SYSCALL_IMPL(getxgid)
{
    /* Alpha specific */
    cpu_env->ir[IR_A4] = getegid();
    return get_errno(getgid());
}
SYSCALL_DEF(getxgid);
#endif

#ifdef TARGET_NR_getxpid
SYSCALL_IMPL(getxpid)
{
    /* Alpha specific */
    cpu_env->ir[IR_A4] = getppid();
    return get_errno(getpid());
}
SYSCALL_DEF(getxpid);
#endif

#ifdef TARGET_NR_getxuid
SYSCALL_IMPL(getxuid)
{
    /* Alpha specific */
    cpu_env->ir[IR_A4] = geteuid();
    return get_errno(getuid());
}
SYSCALL_DEF(getxuid);
#endif

SYSCALL_IMPL(setfsgid)
{
    return get_errno(setfsgid(arg1));
}
SYSCALL_DEF(setfsgid, ARG_DEC);

#ifdef TARGET_NR_setfsgid32
SYSCALL_IMPL(setfsgid32)
{
    return get_errno(setfsgid(arg1));
}
SYSCALL_DEF(setfsgid32, ARG_DEC);
#endif

SYSCALL_IMPL(setfsuid)
{
    return get_errno(setfsuid(arg1));
}
SYSCALL_DEF(setfsuid, ARG_DEC);

#ifdef TARGET_NR_setfsuid32
SYSCALL_IMPL(setfsuid32)
{
    return get_errno(setfsuid(arg1));
}
SYSCALL_DEF(setfsuid32, ARG_DEC);
#endif

SYSCALL_IMPL(setgid)
{
    return get_errno(sys_setgid(low2highgid(arg1)));
}
SYSCALL_DEF(setgid, ARG_DEC);

#ifdef TARGET_NR_setgid32
SYSCALL_IMPL(setgid32)
{
    return get_errno(sys_setgid(arg1));
}
SYSCALL_DEF(setgid32, ARG_DEC);
#endif

SYSCALL_IMPL(setgroups)
{
    int gidsetsize = arg1;
    gid_t *grouplist = NULL;
    abi_long ret;

    if (gidsetsize != 0) {
        size_t target_grouplist_size = gidsetsize * sizeof(target_id);
        target_id *target_grouplist
            = lock_user(VERIFY_READ, arg2, target_grouplist_size, 1);
        int i;

        if (!target_grouplist) {
            return -TARGET_EFAULT;
        }
        grouplist = g_try_new(gid_t, gidsetsize);
        if (!grouplist) {
            unlock_user(target_grouplist, arg2, 0);
            return -TARGET_ENOMEM;
        }

        for (i = 0; i < gidsetsize; i++) {
            grouplist[i] = low2highgid(tswapid(target_grouplist[i]));
        }
        unlock_user(target_grouplist, arg2, 0);
    }
    ret = get_errno(setgroups(gidsetsize, grouplist));
    g_free(grouplist);
    return ret;
}
SYSCALL_DEF(setgroups, ARG_DEC, ARG_PTR);

#ifdef TARGET_NR_setgroups32
SYSCALL_IMPL(setgroups32)
{
    int gidsetsize = arg1;
    gid_t *grouplist = NULL;
    abi_long ret;

    if (gidsetsize != 0) {
        uint32_t *target_grouplist
            = lock_user(VERIFY_READ, arg2, gidsetsize * 4, 1);
        int i;

        if (!target_grouplist) {
            return -TARGET_EFAULT;
        }
        grouplist = g_try_new(gid_t, gidsetsize);
        if (!grouplist) {
            unlock_user(target_grouplist, arg2, 0);
            return -TARGET_ENOMEM;
        }

        for (i = 0; i < gidsetsize; i++) {
            grouplist[i] = tswap32(target_grouplist[i]);
        }
        unlock_user(target_grouplist, arg2, 0);
    }
    ret = get_errno(setgroups(gidsetsize, grouplist));
    g_free(grouplist);
    return ret;
}
SYSCALL_DEF(setgroups32, ARG_DEC, ARG_PTR);
#endif

SYSCALL_IMPL(setregid)
{
    return get_errno(setregid(low2highgid(arg1), low2highgid(arg2)));
}
SYSCALL_DEF(setregid, ARG_DEC, ARG_DEC);

#ifdef TARGET_NR_setregid32
SYSCALL_IMPL(setregid32)
{
    return get_errno(setregid(arg1, arg2));
}
SYSCALL_DEF(setregid32, ARG_DEC, ARG_DEC);
#endif

#ifdef TARGET_NR_setresgid
SYSCALL_IMPL(setresgid)
{
    return get_errno(sys_setresgid(low2highgid(arg1),
                                   low2highgid(arg2),
                                   low2highgid(arg3)));
}
SYSCALL_DEF(setresgid, ARG_DEC, ARG_DEC, ARG_DEC);
#endif

#ifdef TARGET_NR_setresgid32
SYSCALL_IMPL(setresgid32)
{
    return get_errno(sys_setresgid(arg1, arg2, arg3));
}
SYSCALL_DEF(setresgid32, ARG_DEC, ARG_DEC, ARG_DEC);
#endif

#ifdef TARGET_NR_setresuid
SYSCALL_IMPL(setresuid)
{
    return get_errno(sys_setresuid(low2highuid(arg1),
                                   low2highuid(arg2),
                                   low2highuid(arg3)));
}
SYSCALL_DEF(setresuid, ARG_DEC, ARG_DEC, ARG_DEC);
#endif

#ifdef TARGET_NR_setresuid32
SYSCALL_IMPL(setresuid32)
{
    return get_errno(sys_setresuid(arg1, arg2, arg3));
}
SYSCALL_DEF(setresuid32, ARG_DEC, ARG_DEC, ARG_DEC);
#endif

SYSCALL_IMPL(setreuid)
{
    return get_errno(setreuid(low2highuid(arg1), low2highuid(arg2)));
}
SYSCALL_DEF(setreuid, ARG_DEC, ARG_DEC);

#ifdef TARGET_NR_setreuid32
SYSCALL_IMPL(setreuid32)
{
    return get_errno(setreuid(arg1, arg2));
}
SYSCALL_DEF(setreuid32, ARG_DEC, ARG_DEC);
#endif

SYSCALL_IMPL(setsid)
{
    return get_errno(setsid());
}
SYSCALL_DEF(setsid);

SYSCALL_IMPL(setuid)
{
    return get_errno(sys_setuid(low2highuid(arg1)));
}
SYSCALL_DEF(setuid, ARG_DEC);

#ifdef TARGET_NR_setuid32
SYSCALL_IMPL(setuid32)
{
    return get_errno(sys_setuid(arg1));
}
SYSCALL_DEF(setuid32, ARG_DEC);
#endif

#ifdef TARGET_NR_get_thread_area
#if defined(TARGET_I386) && defined(TARGET_ABI32)
static abi_long do_get_thread_area(CPUX86State *env, abi_ulong ptr)
{
    struct target_modify_ldt_ldt_s *target_ldt_info;
    uint64_t *gdt_table = g2h(env->gdt.base);
    uint32_t base_addr, limit, flags;
    int seg_32bit, contents, read_exec_only, limit_in_pages, idx;
    int seg_not_present, useable, lm;
    uint32_t *lp, entry_1, entry_2;

    lock_user_struct(VERIFY_WRITE, target_ldt_info, ptr, 1);
    if (!target_ldt_info) {
        return -TARGET_EFAULT;
    }
    idx = tswap32(target_ldt_info->entry_number);
    if (idx < TARGET_GDT_ENTRY_TLS_MIN ||
        idx > TARGET_GDT_ENTRY_TLS_MAX) {
        unlock_user_struct(target_ldt_info, ptr, 1);
        return -TARGET_EINVAL;
    }
    lp = (uint32_t *)(gdt_table + idx);
    entry_1 = tswap32(lp[0]);
    entry_2 = tswap32(lp[1]);

    read_exec_only = ((entry_2 >> 9) & 1) ^ 1;
    contents = (entry_2 >> 10) & 3;
    seg_not_present = ((entry_2 >> 15) & 1) ^ 1;
    seg_32bit = (entry_2 >> 22) & 1;
    limit_in_pages = (entry_2 >> 23) & 1;
    useable = (entry_2 >> 20) & 1;
#ifdef TARGET_ABI32
    lm = 0;
#else
    lm = (entry_2 >> 21) & 1;
#endif
    flags = (seg_32bit << 0) | (contents << 1) |
        (read_exec_only << 3) | (limit_in_pages << 4) |
        (seg_not_present << 5) | (useable << 6) | (lm << 7);
    limit = (entry_1 & 0xffff) | (entry_2  & 0xf0000);
    base_addr = (entry_1 >> 16) |
        (entry_2 & 0xff000000) |
        ((entry_2 & 0xff) << 16);
    target_ldt_info->base_addr = tswapal(base_addr);
    target_ldt_info->limit = tswap32(limit);
    target_ldt_info->flags = tswap32(flags);
    unlock_user_struct(target_ldt_info, ptr, 1);
    return 0;
}
#endif

SYSCALL_IMPL(get_thread_area)
{
#if defined(TARGET_I386) && defined(TARGET_ABI32)
    return do_get_thread_area(cpu_env, arg1);
#elif defined(TARGET_M68K)
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    TaskState *ts = cpu->opaque;
    return ts->tp_value;
#else
    return -TARGET_ENOSYS;
#endif
}

const SyscallDef def_get_thread_area = {
    .name = "get_thread_area",
    .impl = impl_get_thread_area,
    .print_ret = print_syscall_ptr_ret,
#if defined(TARGET_I386) && defined(TARGET_ABI32)
    .arg_type = { ARG_PTR }
#endif
};
#endif

#ifdef TARGET_NR_set_thread_area
SYSCALL_IMPL(set_thread_area)
{
#if defined(TARGET_MIPS)
    cpu_env->active_tc.CP0_UserLocal = arg1;
    return 0;
#elif defined(TARGET_CRIS)
    if (arg1 & 0xff) {
        return -TARGET_EINVAL;
    }
    cpu_env->pregs[PR_PID] = arg1;
    return 0;
#elif defined(TARGET_I386) && defined(TARGET_ABI32)
    return do_set_thread_area(cpu_env, arg1);
#elif defined(TARGET_M68K)
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    TaskState *ts = cpu->opaque;
    ts->tp_value = arg1;
    return 0;
#else
    return -TARGET_ENOSYS;
#endif
}
SYSCALL_DEF(set_thread_area, ARG_PTR);
#endif

SYSCALL_IMPL(set_tid_address)
{
    return get_errno(set_tid_address((int *)g2h(arg1)));
}
SYSCALL_DEF(set_tid_address, ARG_PTR);

