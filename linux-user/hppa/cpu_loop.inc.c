static abi_ulong hppa_lws(CPUHPPAState *env)
{
    uint32_t which = env->gr[20];
    abi_ulong addr = env->gr[26];
    abi_ulong old = env->gr[25];
    abi_ulong new = env->gr[24];
    abi_ulong size, ret;

    switch (which) {
    default:
        return -TARGET_ENOSYS;

    case 0: /* elf32 atomic 32bit cmpxchg */
        if ((addr & 3) || !access_ok(VERIFY_WRITE, addr, 4)) {
            return -TARGET_EFAULT;
        }
        old = tswap32(old);
        new = tswap32(new);
        ret = atomic_cmpxchg((uint32_t *)g2h(addr), old, new);
        ret = tswap32(ret);
        break;

    case 2: /* elf32 atomic "new" cmpxchg */
        size = env->gr[23];
        if (size >= 4) {
            return -TARGET_ENOSYS;
        }
        if (((addr | old | new) & ((1 << size) - 1))
            || !access_ok(VERIFY_WRITE, addr, 1 << size)
            || !access_ok(VERIFY_READ, old, 1 << size)
            || !access_ok(VERIFY_READ, new, 1 << size)) {
            return -TARGET_EFAULT;
        }
        /* Note that below we use host-endian loads so that the cmpxchg
           can be host-endian as well.  */
        switch (size) {
        case 0:
            old = *(uint8_t *)g2h(old);
            new = *(uint8_t *)g2h(new);
            ret = atomic_cmpxchg((uint8_t *)g2h(addr), old, new);
            ret = ret != old;
            break;
        case 1:
            old = *(uint16_t *)g2h(old);
            new = *(uint16_t *)g2h(new);
            ret = atomic_cmpxchg((uint16_t *)g2h(addr), old, new);
            ret = ret != old;
            break;
        case 2:
            old = *(uint32_t *)g2h(old);
            new = *(uint32_t *)g2h(new);
            ret = atomic_cmpxchg((uint32_t *)g2h(addr), old, new);
            ret = ret != old;
            break;
        case 3:
            {
                uint64_t o64, n64, r64;
                o64 = *(uint64_t *)g2h(old);
                n64 = *(uint64_t *)g2h(new);
#ifdef CONFIG_ATOMIC64
                r64 = atomic_cmpxchg__nocheck((uint64_t *)g2h(addr), o64, n64);
                ret = r64 != o64;
#else
                start_exclusive();
                r64 = *(uint64_t *)g2h(addr);
                ret = 1;
                if (r64 == o64) {
                    *(uint64_t *)g2h(addr) = n64;
                    ret = 0;
                }
                end_exclusive();
#endif
            }
            break;
        }
        break;
    }

    env->gr[28] = ret;
    return 0;
}

void cpu_loop(CPUHPPAState *env)
{
    CPUState *cs = CPU(hppa_env_get_cpu(env));
    target_siginfo_t info;
    abi_ulong ret;
    int trapnr;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_SYSCALL:
            ret = do_syscall(env, env->gr[20],
                             env->gr[26], env->gr[25],
                             env->gr[24], env->gr[23],
                             env->gr[22], env->gr[21], 0, 0);
            switch (ret) {
            default:
                env->gr[28] = ret;
                /* We arrived here by faking the gateway page.  Return.  */
                env->iaoq_f = env->gr[31];
                env->iaoq_b = env->gr[31] + 4;
                break;
            case -TARGET_ERESTARTSYS:
            case -TARGET_QEMU_ESIGRETURN:
                break;
            }
            break;
        case EXCP_SYSCALL_LWS:
            env->gr[21] = hppa_lws(env);
            /* We arrived here by faking the gateway page.  Return.  */
            env->iaoq_f = env->gr[31];
            env->iaoq_b = env->gr[31] + 4;
            break;
        case EXCP_ITLB_MISS:
        case EXCP_DTLB_MISS:
        case EXCP_NA_ITLB_MISS:
        case EXCP_NA_DTLB_MISS:
        case EXCP_IMP:
        case EXCP_DMP:
        case EXCP_DMB:
        case EXCP_PAGE_REF:
        case EXCP_DMAR:
        case EXCP_DMPI:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            info.si_code = TARGET_SEGV_ACCERR;
            info._sifields._sigfault._addr = env->cr[CR_IOR];
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_UNALIGN:
            info.si_signo = TARGET_SIGBUS;
            info.si_errno = 0;
            info.si_code = 0;
            info._sifields._sigfault._addr = env->cr[CR_IOR];
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_ILL:
        case EXCP_PRIV_OPR:
        case EXCP_PRIV_REG:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->iaoq_f;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_OVERFLOW:
        case EXCP_COND:
        case EXCP_ASSIST:
            info.si_signo = TARGET_SIGFPE;
            info.si_errno = 0;
            info.si_code = 0;
            info._sifields._sigfault._addr = env->iaoq_f;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_DEBUG:
            trapnr = gdb_handlesig(cs, TARGET_SIGTRAP);
            if (trapnr) {
                info.si_signo = trapnr;
                info.si_errno = 0;
                info.si_code = TARGET_TRAP_BRKPT;
                queue_signal(env, trapnr, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        default:
            g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}
