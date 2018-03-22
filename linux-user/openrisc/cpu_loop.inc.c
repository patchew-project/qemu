void cpu_loop(CPUOpenRISCState *env)
{
    CPUState *cs = CPU(openrisc_env_get_cpu(env));
    int trapnr;
    abi_long ret;
    target_siginfo_t info;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_SYSCALL:
            env->pc += 4;   /* 0xc00; */
            ret = do_syscall(env,
                             cpu_get_gpr(env, 11), /* return value       */
                             cpu_get_gpr(env, 3),  /* r3 - r7 are params */
                             cpu_get_gpr(env, 4),
                             cpu_get_gpr(env, 5),
                             cpu_get_gpr(env, 6),
                             cpu_get_gpr(env, 7),
                             cpu_get_gpr(env, 8), 0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                env->pc -= 4;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                cpu_set_gpr(env, 11, ret);
            }
            break;
        case EXCP_DPF:
        case EXCP_IPF:
        case EXCP_RANGE:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            info.si_code = TARGET_SEGV_MAPERR;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_ALIGN:
            info.si_signo = TARGET_SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_BUS_ADRALN;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_ILLEGAL:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPC;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_FPE:
            info.si_signo = TARGET_SIGFPE;
            info.si_errno = 0;
            info.si_code = 0;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_INTERRUPT:
            /* We processed the pending cpu work above.  */
            break;
        case EXCP_DEBUG:
            trapnr = gdb_handlesig(cs, TARGET_SIGTRAP);
            if (trapnr) {
                info.si_signo = trapnr;
                info.si_errno = 0;
                info.si_code = TARGET_TRAP_BRKPT;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}
