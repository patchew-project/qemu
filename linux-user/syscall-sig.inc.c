/*
 *  Linux signal related syscalls
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

#ifdef TARGET_NR_alarm
SYSCALL_IMPL(alarm)
{
    return alarm(arg1);
}
#endif

SYSCALL_IMPL(kill)
{
    return get_errno(safe_kill(arg1, target_to_host_signal(arg2)));
}

#ifdef TARGET_NR_pause
SYSCALL_IMPL(pause)
{
    if (!block_signals()) {
        CPUState *cpu = ENV_GET_CPU(cpu_env);
        TaskState *ts = cpu->opaque;
        sigsuspend(&ts->signal_mask);
    }
    return -TARGET_EINTR;
}
#endif

SYSCALL_IMPL(rt_sigaction)
{
    abi_long ret;
#if defined(TARGET_ALPHA)
    /*
     * For Alpha and SPARC this is a 5 argument syscall, with
     * a 'restorer' parameter which must be copied into the
     * sa_restorer field of the sigaction struct.
     * For Alpha that 'restorer' is arg5; for SPARC it is arg4,
     * and arg5 is the sigsetsize.
     * Alpha also has a separate rt_sigaction struct that it uses
     * here; SPARC uses the usual sigaction struct.
     */
    struct target_rt_sigaction *rt_act;
    struct target_sigaction act, oact, *pact = NULL;

    if (arg4 != sizeof(target_sigset_t)) {
        return -TARGET_EINVAL;
    }
    if (arg2) {
        if (!lock_user_struct(VERIFY_READ, rt_act, arg2, 1)) {
            return -TARGET_EFAULT;
        }
        act._sa_handler = rt_act->_sa_handler;
        act.sa_mask = rt_act->sa_mask;
        act.sa_flags = rt_act->sa_flags;
        act.sa_restorer = arg5;
        unlock_user_struct(rt_act, arg2, 0);
        pact = &act;
    }
    ret = get_errno(do_sigaction(arg1, pact, &oact));
    if (!is_error(ret) && arg3) {
        if (!lock_user_struct(VERIFY_WRITE, rt_act, arg3, 0)) {
            return -TARGET_EFAULT;
        }
        rt_act->_sa_handler = oact._sa_handler;
        rt_act->sa_mask = oact.sa_mask;
        rt_act->sa_flags = oact.sa_flags;
        unlock_user_struct(rt_act, arg3, 1);
    }
#else
# ifdef TARGET_SPARC
    target_ulong restorer = arg4;
    target_ulong sigsetsize = arg5;
# else
    target_ulong sigsetsize = arg4;
# endif
    struct target_sigaction act, oact, *pact = NULL;

    if (sigsetsize != sizeof(target_sigset_t)) {
        return -TARGET_EINVAL;
    }
    if (arg2) {
        if (!lock_user_struct(VERIFY_READ, pact, arg2, 1)) {
            return -TARGET_EFAULT;
        }
        act = *pact;
        unlock_user_struct(pact, arg2, 0);
# ifdef TARGET_ARCH_HAS_KA_RESTORER
        act.ka_restorer = restorer;
# endif
        pact = &act;
    }

    ret = get_errno(do_sigaction(arg1, pact, &oact));

    if (!is_error(ret) && arg3) {
        if (!lock_user_struct(VERIFY_WRITE, pact, arg3, 0)) {
            return -TARGET_EFAULT;
        }
        *pact = oact;
        unlock_user_struct(pact, arg3, 1);
    }
#endif
    return ret;
}

SYSCALL_IMPL(rt_sigpending)
{
    sigset_t set;
    abi_long ret;

    /*
     * Yes, this check is >, not != like most. We follow the kernel's
     * logic and it does it like this because it implements
     * NR_sigpending through the same code path, and in that case
     * the old_sigset_t is smaller in size.
     */
    if (arg2 > sizeof(target_sigset_t)) {
        return -TARGET_EINVAL;
    }

    ret = get_errno(sigpending(&set));
    if (!is_error(ret)) {
        void *p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0);
        if (!p) {
            return -TARGET_EFAULT;
        }
        host_to_target_sigset(p, &set);
        unlock_user(p, arg1, sizeof(target_sigset_t));
    }
    return ret;
}

SYSCALL_IMPL(rt_sigprocmask)
{
    int how = 0;
    sigset_t set, oldset, *set_ptr = NULL;
    abi_long ret;
    void *p;

    if (arg4 != sizeof(target_sigset_t)) {
        return -TARGET_EINVAL;
    }

    if (arg2) {
        switch (arg1) {
        case TARGET_SIG_BLOCK:
            how = SIG_BLOCK;
            break;
        case TARGET_SIG_UNBLOCK:
            how = SIG_UNBLOCK;
            break;
        case TARGET_SIG_SETMASK:
            how = SIG_SETMASK;
            break;
        default:
            return -TARGET_EINVAL;
        }
        p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1);
        if (!p) {
            return -TARGET_EFAULT;
        }
        target_to_host_sigset(&set, p);
        unlock_user(p, arg2, 0);
        set_ptr = &set;
    }

    ret = do_sigprocmask(how, set_ptr, &oldset);

    if (!is_error(ret) && arg3) {
        p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0);
        if (!p) {
            return -TARGET_EFAULT;
        }
        host_to_target_sigset(p, &oldset);
        unlock_user(p, arg3, sizeof(target_sigset_t));
    }
    return ret;
}

SYSCALL_IMPL(rt_sigqueueinfo)
{
    siginfo_t uinfo;
    void *p;

    p = lock_user(VERIFY_READ, arg3, sizeof(target_siginfo_t), 1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    target_to_host_siginfo(&uinfo, p);
    unlock_user(p, arg3, 0);

    return get_errno(sys_rt_sigqueueinfo(arg1, arg2, &uinfo));
}

SYSCALL_IMPL(rt_sigsuspend)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    TaskState *ts = cpu->opaque;
    abi_long ret;
    void *p;

    if (arg2 != sizeof(target_sigset_t)) {
        return -TARGET_EINVAL;
    }
    p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    target_to_host_sigset(&ts->sigsuspend_mask, p);
    unlock_user(p, arg1, 0);

    ret = get_errno(safe_rt_sigsuspend(&ts->sigsuspend_mask, SIGSET_T_SIZE));
    if (ret != -TARGET_ERESTARTSYS) {
        ts->in_sigsuspend = 1;
    }
    return ret;
}

SYSCALL_IMPL(rt_sigtimedwait)
{
    sigset_t set;
    struct timespec uts, *puts = NULL;
    siginfo_t uinfo;
    abi_long ret;
    void *p;

    if (arg4 != sizeof(target_sigset_t)) {
        return -TARGET_EINVAL;
    }
    p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    target_to_host_sigset(&set, p);
    unlock_user(p, arg1, 0);
    if (arg3) {
        puts = &uts;
        target_to_host_timespec(puts, arg3);
    }

    ret = get_errno(safe_rt_sigtimedwait(&set, &uinfo, puts, SIGSET_T_SIZE));
    if (!is_error(ret)) {
        if (arg2) {
            p = lock_user(VERIFY_WRITE, arg2, sizeof(target_siginfo_t), 0);
            if (!p) {
                return -TARGET_EFAULT;
            }
            host_to_target_siginfo(p, &uinfo);
            unlock_user(p, arg2, sizeof(target_siginfo_t));
        }
        ret = host_to_target_signal(ret);
    }
    return ret;
}

SYSCALL_IMPL(rt_tgsigqueueinfo)
{
    siginfo_t uinfo;
    void *p;

    p = lock_user(VERIFY_READ, arg4, sizeof(target_siginfo_t), 1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    target_to_host_siginfo(&uinfo, p);
    unlock_user(p, arg4, 0);

    return get_errno(sys_rt_tgsigqueueinfo(arg1, arg2, arg3, &uinfo));
}

#ifdef TARGET_NR_sigaction
SYSCALL_IMPL(sigaction)
{
    abi_long ret;
#if defined(TARGET_ALPHA)
    struct target_sigaction act, oact, *pact = NULL;
    struct target_old_sigaction *old_act;

    if (arg2) {
        if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1)) {
            return -TARGET_EFAULT;
        }
        act._sa_handler = old_act->_sa_handler;
        target_siginitset(&act.sa_mask, old_act->sa_mask);
        act.sa_flags = old_act->sa_flags;
        act.sa_restorer = 0;
        unlock_user_struct(old_act, arg2, 0);
        pact = &act;
    }

    ret = get_errno(do_sigaction(arg1, pact, &oact));

    if (!is_error(ret) && arg3) {
        if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0)) {
            return -TARGET_EFAULT;
        }
        old_act->_sa_handler = oact._sa_handler;
        old_act->sa_mask = oact.sa_mask.sig[0];
        old_act->sa_flags = oact.sa_flags;
        unlock_user_struct(old_act, arg3, 1);
    }
#elif defined(TARGET_MIPS)
    struct target_sigaction act, oact, *pact = NULL, *old_act;

    if (arg2) {
        if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1)) {
            return -TARGET_EFAULT;
        }
        act._sa_handler = old_act->_sa_handler;
        target_siginitset(&act.sa_mask, old_act->sa_mask.sig[0]);
        act.sa_flags = old_act->sa_flags;
        unlock_user_struct(old_act, arg2, 0);
        pact = &act;
    }

    ret = get_errno(do_sigaction(arg1, pact, &oact));

    if (!is_error(ret) && arg3) {
        if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0)) {
            return -TARGET_EFAULT;
        }
        old_act->_sa_handler = oact._sa_handler;
        old_act->sa_flags = oact.sa_flags;
        old_act->sa_mask.sig[0] = oact.sa_mask.sig[0];
        old_act->sa_mask.sig[1] = 0;
        old_act->sa_mask.sig[2] = 0;
        old_act->sa_mask.sig[3] = 0;
        unlock_user_struct(old_act, arg3, 1);
    }
#else
    struct target_old_sigaction *old_act;
    struct target_sigaction act, oact, *pact = NULL;

    if (arg2) {
        if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1)) {
            return -TARGET_EFAULT;
        }
        act._sa_handler = old_act->_sa_handler;
        target_siginitset(&act.sa_mask, old_act->sa_mask);
        act.sa_flags = old_act->sa_flags;
        act.sa_restorer = old_act->sa_restorer;
#ifdef TARGET_ARCH_HAS_KA_RESTORER
        act.ka_restorer = 0;
#endif
        unlock_user_struct(old_act, arg2, 0);
        pact = &act;
    }

    ret = get_errno(do_sigaction(arg1, pact, &oact));

    if (!is_error(ret) && arg3) {
        if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0)) {
            return -TARGET_EFAULT;
        }
        old_act->_sa_handler = oact._sa_handler;
        old_act->sa_mask = oact.sa_mask.sig[0];
        old_act->sa_flags = oact.sa_flags;
        old_act->sa_restorer = oact.sa_restorer;
        unlock_user_struct(old_act, arg3, 1);
    }
#endif
    return ret;
}
#endif

#ifdef TARGET_NR_sigpending
SYSCALL_IMPL(sigpending)
{
    sigset_t set;
    abi_long ret = get_errno(sigpending(&set));

    if (!is_error(ret)) {
        void *p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0);
        if (!p) {
            return -TARGET_EFAULT;
        }
        host_to_target_old_sigset(p, &set);
        unlock_user(p, arg1, sizeof(target_sigset_t));
    }
    return ret;
}
#endif

#ifdef TARGET_NR_sigprocmask
SYSCALL_IMPL(sigprocmask)
{
#if defined(TARGET_ALPHA)
    sigset_t set, oldset;
    abi_ulong mask;
    int how;
    abi_long ret;

    switch (arg1) {
    case TARGET_SIG_BLOCK:
        how = SIG_BLOCK;
        break;
    case TARGET_SIG_UNBLOCK:
        how = SIG_UNBLOCK;
        break;
    case TARGET_SIG_SETMASK:
        how = SIG_SETMASK;
        break;
    default:
        return -TARGET_EINVAL;
    }
    mask = arg2;
    target_to_host_old_sigset(&set, &mask);

    ret = do_sigprocmask(how, &set, &oldset);

    if (!is_error(ret)) {
        host_to_target_old_sigset(&mask, &oldset);
        ret = mask;
        ((CPUAlphaState *)cpu_env)->ir[IR_V0] = 0; /* force no error */
    }
#else
    sigset_t set, oldset, *set_ptr = NULL;
    int how = 0;
    abi_long ret;
    void *p;

    if (arg2) {
        switch (arg1) {
        case TARGET_SIG_BLOCK:
            how = SIG_BLOCK;
            break;
        case TARGET_SIG_UNBLOCK:
            how = SIG_UNBLOCK;
            break;
        case TARGET_SIG_SETMASK:
            how = SIG_SETMASK;
            break;
        default:
            return -TARGET_EINVAL;
        }
        p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1);
        if (!p) {
            return -TARGET_EFAULT;
        }
        target_to_host_old_sigset(&set, p);
        unlock_user(p, arg2, 0);
        set_ptr = &set;
    }

    ret = do_sigprocmask(how, set_ptr, &oldset);

    if (!is_error(ret) && arg3) {
        p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0);
        if (!p) {
            return -TARGET_EFAULT;
        }
        host_to_target_old_sigset(p, &oldset);
        unlock_user(p, arg3, sizeof(target_sigset_t));
    }
#endif
    return ret;
}
#endif

#ifdef TARGET_NR_sigsuspend
SYSCALL_IMPL(sigsuspend)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    TaskState *ts = cpu->opaque;
    abi_long ret;

#if defined(TARGET_ALPHA)
    abi_ulong mask = arg1;
    target_to_host_old_sigset(&ts->sigsuspend_mask, &mask);
#else
    void *p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    target_to_host_old_sigset(&ts->sigsuspend_mask, p);
    unlock_user(p, arg1, 0);
#endif

    ret = get_errno(safe_rt_sigsuspend(&ts->sigsuspend_mask, SIGSET_T_SIZE));
    if (ret != -TARGET_ERESTARTSYS) {
        ts->in_sigsuspend = 1;
    }
    return ret;
}
#endif

#ifdef TARGET_NR_sgetmask
SYSCALL_IMPL(sgetmask)
{
    sigset_t cur_set;
    abi_ulong target_set;
    abi_long ret = do_sigprocmask(0, NULL, &cur_set);

    if (!ret) {
        host_to_target_old_sigset(&target_set, &cur_set);
        ret = target_set;
    }
    return ret;
}
#endif

#ifdef TARGET_NR_ssetmask
SYSCALL_IMPL(ssetmask)
{
    sigset_t set, oset;
    abi_ulong target_set = arg1;
    abi_long ret;

    target_to_host_old_sigset(&set, &target_set);
    ret = do_sigprocmask(SIG_SETMASK, &set, &oset);
    if (!ret) {
        host_to_target_old_sigset(&target_set, &oset);
        ret = target_set;
    }
    return ret;
}
#endif
