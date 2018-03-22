void cpu_loop(CPUNios2State *env)
{
    CPUState *cs = ENV_GET_CPU(env);
    Nios2CPU *cpu = NIOS2_CPU(cs);
    target_siginfo_t info;
    int trapnr, gdbsig, ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        gdbsig = 0;

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_TRAP:
            if (env->regs[R_AT] == 0) {
                abi_long ret;
                qemu_log_mask(CPU_LOG_INT, "\nSyscall\n");

                ret = do_syscall(env, env->regs[2],
                                 env->regs[4], env->regs[5], env->regs[6],
                                 env->regs[7], env->regs[8], env->regs[9],
                                 0, 0);

                if (env->regs[2] == 0) {    /* FIXME: syscall 0 workaround */
                    ret = 0;
                }

                env->regs[2] = abs(ret);
                /* Return value is 0..4096 */
                env->regs[7] = (ret > 0xfffffffffffff000ULL);
                env->regs[CR_ESTATUS] = env->regs[CR_STATUS];
                env->regs[CR_STATUS] &= ~0x3;
                env->regs[R_EA] = env->regs[R_PC] + 4;
                env->regs[R_PC] += 4;
                break;
            } else {
                qemu_log_mask(CPU_LOG_INT, "\nTrap\n");

                env->regs[CR_ESTATUS] = env->regs[CR_STATUS];
                env->regs[CR_STATUS] &= ~0x3;
                env->regs[R_EA] = env->regs[R_PC] + 4;
                env->regs[R_PC] = cpu->exception_addr;

                gdbsig = TARGET_SIGTRAP;
                break;
            }
        case 0xaa:
            switch (env->regs[R_PC]) {
            /*case 0x1000:*/  /* TODO:__kuser_helper_version */
            case 0x1004:      /* __kuser_cmpxchg */
                start_exclusive();
                if (env->regs[4] & 0x3) {
                    goto kuser_fail;
                }
                ret = get_user_u32(env->regs[2], env->regs[4]);
                if (ret) {
                    end_exclusive();
                    goto kuser_fail;
                }
                env->regs[2] -= env->regs[5];
                if (env->regs[2] == 0) {
                    put_user_u32(env->regs[6], env->regs[4]);
                }
                end_exclusive();
                env->regs[R_PC] = env->regs[R_RA];
                break;
            /*case 0x1040:*/  /* TODO:__kuser_sigtramp */
            default:
                ;
kuser_fail:
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                /* TODO: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->regs[R_PC];
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        default:
            EXCP_DUMP(env, "\nqemu: unhandled CPU exception %#x - aborting\n",
                     trapnr);
            gdbsig = TARGET_SIGILL;
            break;
        }
        if (gdbsig) {
            gdb_handlesig(cs, gdbsig);
            if (gdbsig != TARGET_SIGTRAP) {
                exit(EXIT_FAILURE);
            }
        }

        process_pending_signals(env);
    }
}
