/*
 *  i386 cpu init and loop
 *
 *  Copyright (c) 2013 Stacey D. Son
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
#ifndef _TARGET_ARCH_CPU_H_
#define _TARGET_ARCH_CPU_H_

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

static void target_cpu_loop(CPUArchState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    abi_ulong pc;
    /* target_siginfo_t info; */

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case 0x80:
            /* syscall from int $0x80 */
            if (bsd_type == target_freebsd) {
                abi_ulong params = (abi_ulong) env->regs[R_ESP] +
                    sizeof(int32_t);
                int32_t syscall_nr = env->regs[R_EAX];
                int32_t arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8;

                if (syscall_nr == TARGET_FREEBSD_NR_syscall) {
                    get_user_s32(syscall_nr, params);
                    params += sizeof(int32_t);
                } else if (syscall_nr == TARGET_FREEBSD_NR___syscall) {
                    get_user_s32(syscall_nr, params);
                    params += sizeof(int64_t);
                }
                get_user_s32(arg1, params);
                params += sizeof(int32_t);
                get_user_s32(arg2, params);
                params += sizeof(int32_t);
                get_user_s32(arg3, params);
                params += sizeof(int32_t);
                get_user_s32(arg4, params);
                params += sizeof(int32_t);
                get_user_s32(arg5, params);
                params += sizeof(int32_t);
                get_user_s32(arg6, params);
                params += sizeof(int32_t);
                get_user_s32(arg7, params);
                params += sizeof(int32_t);
                get_user_s32(arg8, params);
                env->regs[R_EAX] = do_freebsd_syscall(env,
                                                      syscall_nr,
                                                      arg1,
                                                      arg2,
                                                      arg3,
                                                      arg4,
                                                      arg5,
                                                      arg6,
                                                      arg7,
                                                      arg8);
            } else { /* if (bsd_type == target_openbsd) */
                env->regs[R_EAX] = do_openbsd_syscall(env,
                                                      env->regs[R_EAX],
                                                      env->regs[R_EBX],
                                                      env->regs[R_ECX],
                                                      env->regs[R_EDX],
                                                      env->regs[R_ESI],
                                                      env->regs[R_EDI],
                                                      env->regs[R_EBP]);
            }
            if (((abi_ulong)env->regs[R_EAX]) >= (abi_ulong)(-515)) {
                env->regs[R_EAX] = -env->regs[R_EAX];
                env->eflags |= CC_C;
            } else {
                env->eflags &= ~CC_C;
            }
            break;
#ifndef TARGET_ABI32
        case EXCP_SYSCALL:
            /* syscall from syscall instruction */
            if (bsd_type == target_freebsd) {
                env->regs[R_EAX] = do_freebsd_syscall(env,
                                                      env->regs[R_EAX],
                                                      env->regs[R_EDI],
                                                      env->regs[R_ESI],
                                                      env->regs[R_EDX],
                                                      env->regs[R_ECX],
                                                      env->regs[8],
                                                      env->regs[9], 0, 0);
            } else { /* if (bsd_type == target_openbsd) */
                env->regs[R_EAX] = do_openbsd_syscall(env,
                                                      env->regs[R_EAX],
                                                      env->regs[R_EDI],
                                                      env->regs[R_ESI],
                                                      env->regs[R_EDX],
                                                      env->regs[10],
                                                      env->regs[8],
                                                      env->regs[9]);
            }
            env->eip = env->exception_next_eip;
            if (((abi_ulong)env->regs[R_EAX]) >= (abi_ulong)(-515)) {
                env->regs[R_EAX] = -env->regs[R_EAX];
                env->eflags |= CC_C;
            } else {
                env->eflags &= ~CC_C;
            }
            break;
#endif
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        default:
            pc = env->segs[R_CS].base + env->eip;
            fprintf(stderr,
                    "qemu: 0x%08lx: unhandled CPU exception 0x%x - aborting\n",
                    (long)pc, trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}

#endif /* _TARGET_ARCH_CPU_H_ */
