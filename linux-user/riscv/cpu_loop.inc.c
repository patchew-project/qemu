void cpu_loop(CPURISCVState *env)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));
    int trapnr, signum, sigcode;
    target_ulong sigaddr;
    target_ulong ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        signum = 0;
        sigcode = 0;
        sigaddr = 0;

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case RISCV_EXCP_U_ECALL:
            env->pc += 4;
            if (env->gpr[xA7] == TARGET_NR_arch_specific_syscall + 15) {
                /* riscv_flush_icache_syscall is a no-op in QEMU as
                   self-modifying code is automatically detected */
                ret = 0;
            } else {
                ret = do_syscall(env,
                                 env->gpr[xA7],
                                 env->gpr[xA0],
                                 env->gpr[xA1],
                                 env->gpr[xA2],
                                 env->gpr[xA3],
                                 env->gpr[xA4],
                                 env->gpr[xA5],
                                 0, 0);
            }
            if (ret == -TARGET_ERESTARTSYS) {
                env->pc -= 4;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->gpr[xA0] = ret;
            }
            if (cs->singlestep_enabled) {
                goto gdbstep;
            }
            break;
        case RISCV_EXCP_ILLEGAL_INST:
            signum = TARGET_SIGILL;
            sigcode = TARGET_ILL_ILLOPC;
            break;
        case RISCV_EXCP_BREAKPOINT:
            signum = TARGET_SIGTRAP;
            sigcode = TARGET_TRAP_BRKPT;
            sigaddr = env->pc;
            break;
        case RISCV_EXCP_INST_PAGE_FAULT:
        case RISCV_EXCP_LOAD_PAGE_FAULT:
        case RISCV_EXCP_STORE_PAGE_FAULT:
            signum = TARGET_SIGSEGV;
            sigcode = TARGET_SEGV_MAPERR;
            break;
        case EXCP_DEBUG:
        gdbstep:
            signum = gdb_handlesig(cs, TARGET_SIGTRAP);
            sigcode = TARGET_TRAP_BRKPT;
            break;
        default:
            EXCP_DUMP(env, "\nqemu: unhandled CPU exception %#x - aborting\n",
                     trapnr);
            exit(EXIT_FAILURE);
        }

        if (signum) {
            target_siginfo_t info = {
                .si_signo = signum,
                .si_errno = 0,
                .si_code = sigcode,
                ._sifields._sigfault._addr = sigaddr
            };
            queue_signal(env, info.si_signo, QEMU_SI_KILL, &info);
        }

        process_pending_signals(env);
    }
}
