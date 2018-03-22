/***********************************************************/
/* CPUX86 core interface */

uint64_t cpu_get_tsc(CPUX86State *env)
{
    return cpu_get_host_ticks();
}

static void write_dt(void *ptr, unsigned long addr, unsigned long limit,
                     int flags)
{
    unsigned int e1, e2;
    uint32_t *p;
    e1 = (addr << 16) | (limit & 0xffff);
    e2 = ((addr >> 16) & 0xff) | (addr & 0xff000000) | (limit & 0x000f0000);
    e2 |= flags;
    p = ptr;
    p[0] = tswap32(e1);
    p[1] = tswap32(e2);
}

static uint64_t *idt_table;
#ifdef TARGET_X86_64
static void set_gate64(void *ptr, unsigned int type, unsigned int dpl,
                       uint64_t addr, unsigned int sel)
{
    uint32_t *p, e1, e2;
    e1 = (addr & 0xffff) | (sel << 16);
    e2 = (addr & 0xffff0000) | 0x8000 | (dpl << 13) | (type << 8);
    p = ptr;
    p[0] = tswap32(e1);
    p[1] = tswap32(e2);
    p[2] = tswap32(addr >> 32);
    p[3] = 0;
}
/* only dpl matters as we do only user space emulation */
static void set_idt(int n, unsigned int dpl)
{
    set_gate64(idt_table + n * 2, 0, dpl, 0, 0);
}
#else
static void set_gate(void *ptr, unsigned int type, unsigned int dpl,
                     uint32_t addr, unsigned int sel)
{
    uint32_t *p, e1, e2;
    e1 = (addr & 0xffff) | (sel << 16);
    e2 = (addr & 0xffff0000) | 0x8000 | (dpl << 13) | (type << 8);
    p = ptr;
    p[0] = tswap32(e1);
    p[1] = tswap32(e2);
}

/* only dpl matters as we do only user space emulation */
static void set_idt(int n, unsigned int dpl)
{
    set_gate(idt_table + n, 0, dpl, 0, 0);
}
#endif

void cpu_loop(CPUX86State *env)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    int trapnr;
    abi_ulong pc;
    abi_ulong ret;
    target_siginfo_t info;

    for(;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch(trapnr) {
        case 0x80:
            /* linux syscall from int $0x80 */
            ret = do_syscall(env,
                             env->regs[R_EAX],
                             env->regs[R_EBX],
                             env->regs[R_ECX],
                             env->regs[R_EDX],
                             env->regs[R_ESI],
                             env->regs[R_EDI],
                             env->regs[R_EBP],
                             0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                env->eip -= 2;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->regs[R_EAX] = ret;
            }
            break;
#ifndef TARGET_ABI32
        case EXCP_SYSCALL:
            /* linux syscall from syscall instruction */
            ret = do_syscall(env,
                             env->regs[R_EAX],
                             env->regs[R_EDI],
                             env->regs[R_ESI],
                             env->regs[R_EDX],
                             env->regs[10],
                             env->regs[8],
                             env->regs[9],
                             0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                env->eip -= 2;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->regs[R_EAX] = ret;
            }
            break;
#endif
        case EXCP0B_NOSEG:
        case EXCP0C_STACK:
            info.si_signo = TARGET_SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_SI_KERNEL;
            info._sifields._sigfault._addr = 0;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP0D_GPF:
            /* XXX: potential problem if ABI32 */
#ifndef TARGET_X86_64
            if (env->eflags & VM_MASK) {
                handle_vm86_fault(env);
            } else
#endif
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SI_KERNEL;
                info._sifields._sigfault._addr = 0;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP0E_PAGE:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            if (!(env->error_code & 1))
                info.si_code = TARGET_SEGV_MAPERR;
            else
                info.si_code = TARGET_SEGV_ACCERR;
            info._sifields._sigfault._addr = env->cr[2];
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP00_DIVZ:
#ifndef TARGET_X86_64
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else
#endif
            {
                /* division by zero */
                info.si_signo = TARGET_SIGFPE;
                info.si_errno = 0;
                info.si_code = TARGET_FPE_INTDIV;
                info._sifields._sigfault._addr = env->eip;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP01_DB:
        case EXCP03_INT3:
#ifndef TARGET_X86_64
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else
#endif
            {
                info.si_signo = TARGET_SIGTRAP;
                info.si_errno = 0;
                if (trapnr == EXCP01_DB) {
                    info.si_code = TARGET_TRAP_BRKPT;
                    info._sifields._sigfault._addr = env->eip;
                } else {
                    info.si_code = TARGET_SI_KERNEL;
                    info._sifields._sigfault._addr = 0;
                }
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP04_INTO:
        case EXCP05_BOUND:
#ifndef TARGET_X86_64
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else
#endif
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SI_KERNEL;
                info._sifields._sigfault._addr = 0;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP06_ILLOP:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->eip;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
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
            pc = env->segs[R_CS].base + env->eip;
            EXCP_DUMP(env, "qemu: 0x%08lx: unhandled CPU exception 0x%x - aborting\n",
                      (long)pc, trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}
