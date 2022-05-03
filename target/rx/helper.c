/*
 *  RX emulation
 *
 *  Copyright (c) 2019 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "exec/log.h"
#include "exec/cpu_ldst.h"
#include "hw/irq.h"

void rx_cpu_unpack_psw(CPURXState *env, uint32_t psw, int rte)
{
    if (env->psw_pm == 0) {
        env->psw_ipl = FIELD_EX32(psw, PSW, IPL);
        if (rte) {
            /* PSW.PM can write RTE and RTFI */
            env->psw_pm = FIELD_EX32(psw, PSW, PM);
        }
        env->psw_u = FIELD_EX32(psw, PSW, U);
        env->psw_i = FIELD_EX32(psw, PSW, I);
    }
    env->psw_o = FIELD_EX32(psw, PSW, O) << 31;
    env->psw_s = FIELD_EX32(psw, PSW, S) << 31;
    env->psw_z = 1 - FIELD_EX32(psw, PSW, Z);
    env->psw_c = FIELD_EX32(psw, PSW, C);
}

#ifndef CONFIG_USER_ONLY

void rx_cpu_do_interrupt(CPUState *cs)
{
    RXCPU *cpu = RX_CPU(cs);
    CPURXState *env = &cpu->env;
    uint32_t vec = cs->exception_index;
    target_ulong vec_table = 0xffffff80u; /* fixed vector table */
    const char *expname;
    uint32_t save_psw;

    env->in_sleep = 0;

    if (env->psw_u) {
        env->usp = env->regs[0];
    } else {
        env->isp = env->regs[0];
    }
    save_psw = rx_cpu_pack_psw(env);
    env->psw_pm = env->psw_i = env->psw_u = 0;

    switch (vec) {
    case EXCP_FIRQ:
        env->bpc = env->pc;
        env->bpsw = save_psw;
        env->pc = env->fintv;
        env->psw_ipl = 15;
        cs->interrupt_request &= ~CPU_INTERRUPT_FIR;
        qemu_set_irq(env->ack, env->ack_irq);
        qemu_log_mask(CPU_LOG_INT, "fast interrupt raised\n");
        break;

    case EXCP_IRQ:
        env->psw_ipl = env->ack_ipl;
        cs->interrupt_request &= ~CPU_INTERRUPT_HARD;
        qemu_set_irq(env->ack, env->ack_irq);
        expname = "interrupt";
        vec_table = env->intb;
        vec = env->ack_ipl;
        goto do_stacked;

    case EXCP_PRIVILEGED:
        expname = "privilege violation";
        goto do_stacked;
    case EXCP_ACCESS:
        expname = "access exception";
        goto do_stacked;
    case EXCP_UNDEFINED:
        expname = "illegal instruction";
        goto do_stacked;
    case EXCP_FPU:
        expname = "fpu exception";
        goto do_stacked;
    case EXCP_NMI:
        expname = "non-maskable interrupt";
        goto do_stacked;
    case EXCP_RESET:
        expname = "reset interrupt";
        goto do_stacked;

    case EXCP_INTB_0 ... EXCP_INTB_255:
        expname = "unconditional trap";
        vec_table = env->intb;
        vec -= EXCP_INTB_0;
        goto do_stacked;

    do_stacked:
        env->isp -= 4;
        cpu_stl_data(env, env->isp, save_psw);
        env->isp -= 4;
        cpu_stl_data(env, env->isp, env->pc);
        env->pc = cpu_ldl_data(env, vec_table + vec * 4);
        qemu_log_mask(CPU_LOG_INT, "%s raised (0x%02x)\n", expname, vec);
        break;

    default:
        g_assert_not_reached();
    }
    env->regs[0] = env->isp;
}

bool rx_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    RXCPU *cpu = RX_CPU(cs);
    CPURXState *env = &cpu->env;
    int accept = 0;

    /* hardware interrupt (Normal) */
    if ((interrupt_request & CPU_INTERRUPT_HARD) &&
        env->psw_i && (env->psw_ipl < env->req_ipl)) {
        env->ack_irq = env->req_irq;
        env->ack_ipl = env->req_ipl;
        accept = EXCP_IRQ;
    }
    /* hardware interrupt (FIR) */
    if ((interrupt_request & CPU_INTERRUPT_FIR) &&
        env->psw_i && (env->psw_ipl < 15)) {
        accept = EXCP_FIRQ;
    }
    if (accept) {
        cs->exception_index = accept;
        rx_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

#endif /* !CONFIG_USER_ONLY */

hwaddr rx_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return addr;
}
