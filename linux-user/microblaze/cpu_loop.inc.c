void cpu_loop(CPUMBState *env)
{
    CPUState *cs = CPU(mb_env_get_cpu(env));
    int trapnr, ret;
    target_siginfo_t info;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case 0xaa:
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = 0;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
	case EXCP_INTERRUPT:
	  /* just indicate that signals should be handled asap */
	  break;
        case EXCP_BREAK:
            /* Return address is 4 bytes after the call.  */
            env->regs[14] += 4;
            env->sregs[SR_PC] = env->regs[14];
            ret = do_syscall(env,
                             env->regs[12],
                             env->regs[5],
                             env->regs[6],
                             env->regs[7],
                             env->regs[8],
                             env->regs[9],
                             env->regs[10],
                             0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                /* Wind back to before the syscall. */
                env->sregs[SR_PC] -= 4;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->regs[3] = ret;
            }
            /* All syscall exits result in guest r14 being equal to the
             * PC we return to, because the kernel syscall exit "rtbd" does
             * this. (This is true even for sigreturn(); note that r14 is
             * not a userspace-usable register, as the kernel may clobber it
             * at any point.)
             */
            env->regs[14] = env->sregs[SR_PC];
            break;
        case EXCP_HW_EXCP:
            env->regs[17] = env->sregs[SR_PC] + 4;
            if (env->iflags & D_FLAG) {
                env->sregs[SR_ESR] |= 1 << 12;
                env->sregs[SR_PC] -= 4;
                /* FIXME: if branch was immed, replay the imm as well.  */
            }

            env->iflags &= ~(IMM_FLAG | D_FLAG);

            switch (env->sregs[SR_ESR] & 31) {
                case ESR_EC_DIVZERO:
                    info.si_signo = TARGET_SIGFPE;
                    info.si_errno = 0;
                    info.si_code = TARGET_FPE_FLTDIV;
                    info._sifields._sigfault._addr = 0;
                    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                    break;
                case ESR_EC_FPU:
                    info.si_signo = TARGET_SIGFPE;
                    info.si_errno = 0;
                    if (env->sregs[SR_FSR] & FSR_IO) {
                        info.si_code = TARGET_FPE_FLTINV;
                    }
                    if (env->sregs[SR_FSR] & FSR_DZ) {
                        info.si_code = TARGET_FPE_FLTDIV;
                    }
                    info._sifields._sigfault._addr = 0;
                    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                    break;
                default:
                    printf ("Unhandled hw-exception: 0x%x\n",
                            env->sregs[SR_ESR] & ESR_EC_MASK);
                    cpu_dump_state(cs, stderr, fprintf, 0);
                    exit(EXIT_FAILURE);
                    break;
            }
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig(cs, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                  }
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(cs, stderr, fprintf, 0);
            exit(EXIT_FAILURE);
        }
        process_pending_signals (env);
    }
}
