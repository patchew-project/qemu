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

#ifndef CLONE_IO
#define CLONE_IO                0x80000000      /* Clone io context */
#endif

/*
 * We can't directly call the host clone syscall, because this will
 * badly confuse libc (breaking mutexes, for example). So we must
 * divide clone flags into:
 *  * flag combinations that look like pthread_create()
 *  * flag combinations that look like fork()
 *  * flags we can implement within QEMU itself
 *  * flags we can't support and will return an error for
 *
 * For thread creation, all these flags must be present; for
 * fork, none must be present.
 */
#define CLONE_THREAD_FLAGS                              \
    (CLONE_VM | CLONE_FS | CLONE_FILES |                \
     CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM)

/*
 * These flags are ignored:
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

/*
 * CLONE_VFORK is special cased early in do_fork(). The other flag bits
 * have almost all been allocated. We cannot support any of
 * CLONE_NEWNS, CLONE_NEWCGROUP, CLONE_NEWUTS, CLONE_NEWIPC,
 * CLONE_NEWUSER, CLONE_NEWPID, CLONE_NEWNET, CLONE_PTRACE, CLONE_UNTRACED.
 * The checks against the invalid thread masks above will catch these.
 * (The one remaining unallocated bit is 0x1000 which used to be CLONE_PID.)
 */

/**
 * do_clone:
 * Arguments as for clone(2), returns target errnos.
 */
static abi_long do_clone(CPUArchState *env, unsigned int flags,
                         abi_ulong newsp, abi_ulong parent_tidptr,
                         abi_ulong child_tidptr, target_ulong newtls)
{
    CPUState *cpu = ENV_GET_CPU(env);
    abi_long ret;
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
        /* If CLONE_VM, we consider it a new thread.  */
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

        /* Create a new CPU instance.  */
        new_env = cpu_copy(env);

        /* Init regs that differ from the parent.  */
        cpu_clone_regs_child(new_env, newsp);
        cpu_clone_regs_parent(env);
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

        /*
         * It is not safe to deliver signals until the child has finished
         * initializing, so temporarily block all signals.
         */
        sigfillset(&sigmask);
        sigprocmask(SIG_BLOCK, &sigmask, &info.sigmask);

        /*
         * If this is our first additional thread, we need to ensure we
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
            ret = host_to_target_errno(ret);
        }
        pthread_mutex_unlock(&info.mutex);
        pthread_cond_destroy(&info.cond);
        pthread_mutex_destroy(&info.mutex);
        pthread_mutex_unlock(&clone_lock);
    } else {
        /* If no CLONE_VM, we consider it a fork.  */
        if (flags & CLONE_INVALID_FORK_FLAGS) {
            return -TARGET_EINVAL;
        }

        /* We can't support custom termination signals.  */
        if ((flags & CSIGNAL) != TARGET_SIGCHLD) {
            return -TARGET_EINVAL;
        }

        if (block_signals()) {
            return -TARGET_ERESTARTSYS;
        }

        fork_start();
        ret = fork();
        if (ret == 0) {
            /* Child Process.  */
            cpu_clone_regs_child(env, newsp);
            fork_end(1);
            /*
             * There is a race condition here.  The parent process could
             * theoretically read the TID in the child process before the
             * child tid is set.  This would require using either ptrace
             * (not implemented) or having *_tidptr to point at a shared
             * memory mapping.  We can't repeat the spinlock hack used
             * above because the child process gets its own copy of the lock.
             */
            if (flags & CLONE_CHILD_SETTID) {
                put_user_u32(sys_gettid(), child_tidptr);
            }
            if (flags & CLONE_PARENT_SETTID) {
                put_user_u32(sys_gettid(), parent_tidptr);
            }
            ts = (TaskState *)cpu->opaque;
            if (flags & CLONE_SETTLS) {
                cpu_set_tls(env, newtls);
            }
            if (flags & CLONE_CHILD_CLEARTID) {
                ts->child_tidptr = child_tidptr;
            }
        } else {
            cpu_clone_regs_parent(env);
            fork_end(0);
            ret = get_errno(ret);
        }
    }
    return ret;
}

#if defined(TARGET_MICROBLAZE) || \
    defined(TARGET_CLONE_BACKWARDS) || \
    defined(TARGET_CLONE_BACKWARDS2)
SYSCALL_ARGS(clone)
{
    /*
     * Linux manages to have three "standard" orderings for its
     * arguments to clone(); the BACKWARDS and BACKWARDS2 defines
     * match the kernel's CONFIG_CLONE_* settings.
     * Microblaze is further special in that it uses a sixth
     * implicit argument to clone for the TLS pointer.
     *
     * Standardize on the non-BACKWARDS ordering.
     */
# if defined(TARGET_MICROBLAZE)
    /* We have already assigned out[0-1].  */
    out[2] = in[3];
    out[3] = in[4];
    out[4] = in[5];
# elif defined(TARGET_CLONE_BACKWARDS)
    /* We have already assigned out[0-2].  */
    out[3] = in[4];
    out[4] = in[3];
# elif defined(TARGET_CLONE_BACKWARDS2)
    /* We have already assigned out[2-4].  */
    out[0] = in[1];
    out[1] = in[0];
# else
#  error Missing case
# endif
    return def;
}
#else
#define args_clone NULL
#endif

SYSCALL_IMPL(clone)
{
    return do_clone(cpu_env, arg1, arg2, arg3, arg4, arg5);
}

static abi_long do_execveat(int dirfd, abi_ulong guest_path,
                            abi_ulong guest_argp, abi_ulong guest_envp,
                            int flags)
{
    char **argp, **envp;
    int argc, envc;
    abi_ulong gp;
    abi_ulong addr;
    char **q, *p;
    int total_size = 0;
    abi_long ret = -TARGET_EFAULT;

    argc = 0;
    for (gp = guest_argp; gp; gp += sizeof(abi_ulong)) {
        if (get_user_ual(addr, gp)) {
            goto execve_nofree;
        }
        if (!addr) {
            break;
        }
        argc++;
    }
    envc = 0;
    for (gp = guest_envp; gp; gp += sizeof(abi_ulong)) {
        if (get_user_ual(addr, gp)) {
            goto execve_nofree;
        }
        if (!addr) {
            break;
        }
        envc++;
    }

    argp = g_new0(char *, argc + 1);
    envp = g_new0(char *, envc + 1);

    for (gp = guest_argp, q = argp; gp; gp += sizeof(abi_ulong), q++) {
        char *this_q;

        if (get_user_ual(addr, gp)) {
            goto execve_free;
        }
        if (!addr) {
            break;
        }
        this_q = lock_user_string(addr);
        if (!this_q) {
            goto execve_free;
        }
        *q = this_q;
        total_size += strlen(this_q) + 1;
    }

    for (gp = guest_envp, q = envp; gp; gp += sizeof(abi_ulong), q++) {
        char *this_q;

        if (get_user_ual(addr, gp)) {
            goto execve_free;
        }
        if (!addr) {
            break;
        }
        this_q = lock_user_string(addr);
        if (!this_q) {
            goto execve_free;
        }
        *q = this_q;
        total_size += strlen(this_q) + 1;
    }

    p = lock_user_string(guest_path);
    if (!p) {
        goto execve_free;
    }

    /*
     * Although execve() is not an interruptible syscall it is
     * a special case where we must use the safe_syscall wrapper:
     * if we allow a signal to happen before we make the host
     * syscall then we will 'lose' it, because at the point of
     * execve the process leaves QEMU's control. So we use the
     * safe syscall wrapper to ensure that we either take the
     * signal as a guest signal, or else it does not happen
     * before the execve completes and makes it the other
     * program's problem.
     */
    ret = get_errno(safe_execveat(dirfd, p, argp, envp, flags));
    unlock_user(p, guest_path, 0);

 execve_free:
    for (gp = guest_argp, q = argp; *q; gp += sizeof(abi_ulong), q++) {
        if (get_user_ual(addr, gp) || !addr) {
            break;
        }
        unlock_user(*q, addr, 0);
    }
    for (gp = guest_envp, q = envp; *q; gp += sizeof(abi_ulong), q++) {
        if (get_user_ual(addr, gp) || !addr) {
            break;
        }
        unlock_user(*q, addr, 0);
    }
    g_free(argp);
    g_free(envp);

 execve_nofree:
    return ret;
}

SYSCALL_IMPL(execve)
{
    return do_execveat(AT_FDCWD, arg1, arg2, arg3, 0);
}

SYSCALL_IMPL(execveat)
{
    return do_execveat(arg1, arg2, arg3, arg4, arg5);
}

SYSCALL_IMPL(exit)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    int status = arg1;

    /*
     * In old applications this may be used to implement _exit(2).
     * However in threaded applictions it is used for thread termination,
     * and _exit_group is used for application termination.
     * Do thread termination if we have more then one thread.
     */
    if (block_signals()) {
        return -TARGET_ERESTARTSYS;
    }

    cpu_list_lock();

    if (CPU_NEXT(first_cpu)) {
        TaskState *ts;

        /* Remove the CPU from the list.  */
        QTAILQ_REMOVE_RCU(&cpus, cpu, node);

        cpu_list_unlock();

        ts = cpu->opaque;
        if (ts->child_tidptr) {
            put_user_u32(0, ts->child_tidptr);
            sys_futex(g2h(ts->child_tidptr), FUTEX_WAKE, INT_MAX,
                      NULL, NULL, 0);
        }
        thread_cpu = NULL;
        object_unref(OBJECT(cpu));
        g_free(ts);
        rcu_unregister_thread();
        pthread_exit(NULL);
    }

    cpu_list_unlock();
    preexit_cleanup(cpu_env, status);
    _exit(status);
}

#if defined(TARGET_NR_fork) || defined(TARGET_NR_vfork)
SYSCALL_IMPL(fork)
{
    return do_clone(cpu_env, TARGET_SIGCHLD, 0, 0, 0, 0);
}
#endif

#ifdef TARGET_NR_gethostname
SYSCALL_IMPL(gethostname)
{
    char *name = lock_user(VERIFY_WRITE, arg1, arg2, 0);
    abi_long ret;
    
    if (!name) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(gethostname(name, arg2));
    unlock_user(name, arg1, arg2);
    return ret;
}
#endif

SYSCALL_IMPL(getpgid)
{
    return get_errno(getpgid(arg1));
}

#ifdef TARGET_NR_getpgrp
SYSCALL_IMPL(getpgrp)
{
    return get_errno(getpgrp());
}
#endif

#ifdef TARGET_NR_getpid
SYSCALL_IMPL(getpid)
{
    return getpid();
}
#endif

#ifdef TARGET_NR_getppid
SYSCALL_IMPL(getppid)
{
    return getppid();
}
#endif

#ifdef TARGET_NR_getrlimit
SYSCALL_IMPL(getrlimit)
{
    int resource = target_to_host_resource(arg1);
    struct target_rlimit *target_rlim;
    struct rlimit rlim;
    abi_long ret;

    ret = get_errno(getrlimit(resource, &rlim));
    if (!is_error(ret)) {
        if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0)) {
            return -TARGET_EFAULT;
        }
        target_rlim->rlim_cur = host_to_target_rlim(rlim.rlim_cur);
        target_rlim->rlim_max = host_to_target_rlim(rlim.rlim_max);
        unlock_user_struct(target_rlim, arg2, 1);
    }
    return ret;
}
#endif

SYSCALL_IMPL(getrusage)
{
    struct rusage rusage;
    abi_long ret = get_errno(getrusage(arg1, &rusage));

    if (!is_error(ret)) {
        ret = host_to_target_rusage(arg2, &rusage);
    }
    return ret;
}

SYSCALL_IMPL(getsid)
{
    return get_errno(getsid(arg1));
}

#ifdef TARGET_NR_getxpid
SYSCALL_IMPL(getxpid)
{
    /* Alpha specific */
    cpu_env->ir[IR_A4] = getppid();
    return getpid();
}
#endif

#ifdef TARGET_NR_nice
SYSCALL_IMPL(nice)
{
    return get_errno(nice(arg1));
}
#endif

SYSCALL_IMPL(sethostname)
{
    void *p = lock_user_string(arg1);
    abi_long ret;

    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(sethostname(p, arg2));
    unlock_user(p, arg1, 0);
    return ret;
}

SYSCALL_IMPL(setpgid)
{
    return get_errno(setpgid(arg1, arg2));
}

#ifdef TARGET_NR_setrlimit
SYSCALL_IMPL(setrlimit)
{
    int resource = target_to_host_resource(arg1);
    struct target_rlimit *target_rlim;
    struct rlimit rlim;

    if (!lock_user_struct(VERIFY_READ, target_rlim, arg2, 1)) {
        return -TARGET_EFAULT;
    }
    rlim.rlim_cur = target_to_host_rlim(target_rlim->rlim_cur);
    rlim.rlim_max = target_to_host_rlim(target_rlim->rlim_max);
    unlock_user_struct(target_rlim, arg2, 0);

    /*
     * If we just passed through resource limit settings for memory then
     * they would also apply to QEMU's own allocations, and QEMU will
     * crash or hang or die if its allocations fail. Ideally we would
     * track the guest allocations in QEMU and apply the limits ourselves.
     * For now, just tell the guest the call succeeded but don't actually
     * limit anything.
     */
    if (resource != RLIMIT_AS &&
        resource != RLIMIT_DATA &&
        resource != RLIMIT_STACK) {
        return get_errno(setrlimit(resource, &rlim));
    } else {
        return 0;
    }
}
#endif

SYSCALL_IMPL(setsid)
{
    return get_errno(setsid());
}

SYSCALL_IMPL(times)
{
    abi_ulong target_buf = arg1;
    struct tms tms;
    abi_long ret;

    ret = get_errno(times(&tms));
    if (target_buf) {
        struct target_tms *tmsp = lock_user(VERIFY_WRITE, target_buf,
                                            sizeof(struct target_tms), 0);
        if (!tmsp) {
            return -TARGET_EFAULT;
        }
        tmsp->tms_utime = tswapal(host_to_target_clock_t(tms.tms_utime));
        tmsp->tms_stime = tswapal(host_to_target_clock_t(tms.tms_stime));
        tmsp->tms_cutime = tswapal(host_to_target_clock_t(tms.tms_cutime));
        tmsp->tms_cstime = tswapal(host_to_target_clock_t(tms.tms_cstime));
        unlock_user(tmsp, target_buf, sizeof(struct target_tms));
    }
    if (!is_error(ret)) {
        ret = host_to_target_clock_t(ret);
    }
    return ret;
}

/*
 * Map host to target signal numbers for the wait family of syscalls.
 * Assume all other status bits are the same.
 */
int host_to_target_waitstatus(int status)
{
    if (WIFSIGNALED(status)) {
        return host_to_target_signal(WTERMSIG(status)) | (status & ~0x7f);
    }
    if (WIFSTOPPED(status)) {
        return (host_to_target_signal(WSTOPSIG(status)) << 8)
               | (status & 0xff);
    }
    return status;
}

SYSCALL_IMPL(wait4)
{
    int status;
    pid_t pid = arg1;
    abi_ulong status_ptr = arg2;
    int options = arg3;
    abi_ulong target_rusage = arg4;
    struct rusage rusage;
    struct rusage *rusage_ptr = target_rusage ? &rusage : NULL;
    abi_long ret;

    ret = get_errno(safe_wait4(pid, &status, options, rusage_ptr));
    if (!is_error(ret)) {
        if (status_ptr && ret) {
            status = host_to_target_waitstatus(status);
            if (put_user_s32(status, status_ptr)) {
                return -TARGET_EFAULT;
            }
        }
        if (target_rusage) {
            abi_long err = host_to_target_rusage(target_rusage, &rusage);
            if (err) {
                ret = err;
            }
        }
    }
    return ret;
}

SYSCALL_IMPL(waitid)
{
    idtype_t idtype = arg1;
    id_t id = arg2;
    abi_ulong target_info = arg3;
    int options = arg4;
    abi_ulong target_rusage = arg5;
    siginfo_t info, *info_ptr = target_info ? &info : NULL;
    struct rusage rusage;
    struct rusage *rusage_ptr = target_rusage ? &rusage : NULL;
    abi_long ret;

    info.si_pid = 0;
    ret = get_errno(safe_waitid(idtype, id, info_ptr, options, rusage_ptr));
    if (!is_error(ret)) {
        if (target_info && info.si_pid != 0) {
            target_siginfo_t *p = lock_user(VERIFY_WRITE, target_info,
                                            sizeof(target_siginfo_t), 0);
            if (!p) {
                return -TARGET_EFAULT;
            }
            host_to_target_siginfo(p, &info);
            unlock_user(p, target_info, sizeof(target_siginfo_t));
        }
        if (target_rusage) {
            abi_long err = host_to_target_rusage(target_rusage, &rusage);
            if (err) {
                ret = err;
            }
        }
    }
    return ret;
}

#ifdef TARGET_NR_waitpid
SYSCALL_IMPL(waitpid)
{
    pid_t pid = arg1;
    abi_ulong target_status = arg2;
    int options = arg3;
    int status;
    abi_long ret;

    ret = get_errno(safe_wait4(pid, &status, options, NULL));
    if (!is_error(ret)
        && target_status
        && ret
        && put_user_s32(host_to_target_waitstatus(status), target_status)) {
        return -TARGET_EFAULT;
    }
    return ret;
}
#endif
