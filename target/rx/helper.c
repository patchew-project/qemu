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

#include "cpu.h"
#include "exec/log.h"
#include "exec/cpu_ldst.h"
#include "sysemu/sysemu.h"

uint32_t update_psw_o(CPURXState *env)
{
    int o;

    switch (env->psw_op) {
    case RX_PSW_OP_NONE:
        return env->psw_o;
    case RX_PSW_OP_ADD: {
        uint32_t r1, r2;
        r1 = ~(env->psw_v[0] ^ env->psw_v[1]);
        r2 = (env->psw_v[0] ^ env->psw_v[2]);
        o = (r1 & r2) >> 31;
        break;
    }
    case RX_PSW_OP_SUB: {
        uint32_t r1, r2;
        r1 = (env->psw_v[0] ^ env->psw_v[1]);
        r2 = (env->psw_v[0] ^ env->psw_v[2]);
        o = (r1 & r2) >> 31;
        break;
    }
    case RX_PSW_OP_SHLL: {
        uint32_t m, v;
        m = (1 << env->psw_v[1]) - 1;
        v = env->psw_v[0] >> (32 - env->psw_v[1]);
        o = (v == 0) || (v == m);
        break;
    }
    default:
        g_assert_not_reached();
        return -1;
    }
    env->psw_o = o;
    env->psw_op = RX_PSW_OP_NONE;
    return o;
}

uint32_t rx_get_psw_low(CPURXState *env)
{
    return (update_psw_o(env) << 3) |
        (env->psw_s << 2) |
        (env->psw_z << 1) |
        (env->psw_c << 0);
}

uint32_t psw_cond(CPURXState *env, uint32_t cond)
{
    uint32_t c, z, s, o;

    switch (cond) {
    case 0: /* z */
        return env->psw_z != 0;
    case 1: /* nz */
        return env->psw_z == 0;
    case 2: /* c */
        return env->psw_c != 0;
    case 3: /* nc */
        return env->psw_c == 0;
    case 4: /* gtu (C&^Z) == 1 */
    case 5: /* leu (C&^Z) == 0 */
        c = env->psw_c != 0;
        z = env->psw_z != 0;
        return (c && !z) == (5 - cond);
    case 6: /* pz (S == 0) */
        return env->psw_s == 0;
    case 7: /* n (S == 1) */
        return env->psw_s != 0;
    case 8: /* ge (S^O)==0 */
    case 9: /* lt (S^O)==1 */
        s = env->psw_s != 0;
        o = update_psw_o(env);
        return (s | o) == (cond - 8);
    case 10: /* gt ((S^O)|Z)==0 */
    case 11: /* le ((S^O)|Z)==1 */
        s = env->psw_s != 0;
        o = update_psw_o(env);
        z = env->psw_z != 0;
        return ((s ^ o) | z) == (cond - 10);
    case 12: /* o */
        return update_psw_o(env) != 0;
    case 13: /* no */
        return update_psw_o(env) == 0;
    case 14: /* always true */
        return 1;
    case 15:
        return 0;
    default:
        g_assert_not_reached();
        return -1;
    }
}

void rx_cpu_unpack_psw(CPURXState *env, int all)
{
    if (env->psw_pm == 0) {
        env->psw_ipl = (env->psw >> 24) & 15;
        if (all) {
            env->psw_pm = (env->psw >> 20) & 1;
        }
        env->psw_u =  (env->psw >> 17) & 1;
        env->psw_i =  (env->psw >> 16) & 1;
    }
    env->psw_o =  (env->psw >> 3) & 1;
    env->psw_s =  (env->psw >> 2) & 1;
    env->psw_z =  (env->psw >> 1) & 1;
    env->psw_c =  (env->psw >> 0) & 1;
    env->psw_op = RX_PSW_OP_NONE;
}

void rx_cpu_do_interrupt(CPUState *cs)
{
    RXCPU *cpu = RXCPU(cs);
    CPURXState *env = &cpu->env;
    int do_irq = cs->interrupt_request &
        (CPU_INTERRUPT_HARD | CPU_INTERRUPT_SOFT | CPU_INTERRUPT_FIR);
    int irq_vector = -1;

    env->in_sleep = 0;

    if (do_irq & CPU_INTERRUPT_HARD) {
        irq_vector = env->irq;
        cs->interrupt_request &= ~CPU_INTERRUPT_HARD;
    }
    if (irq_vector == -1 && do_irq & CPU_INTERRUPT_SOFT) {
        irq_vector = env->sirq;
        cs->interrupt_request &= ~CPU_INTERRUPT_SOFT;
    }

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        if (cs->exception_index < 0x100) {
            const char *expname;
            switch (cs->exception_index) {
            case 20:
                expname = "previlage_violation";
                break;
            case 21:
                expname = "access_exception";
                break;
            case 23:
                expname = "illegal_instruction";
                break;
            case 25:
                expname = "fpu_exception";
                break;
            case 30:
                expname = "NMI_interrupt";
                break;
            }
            qemu_log("exception 0x%02x [%s] raised\n",
                     cs->exception_index, expname);
        } else {
            if (do_irq & CPU_INTERRUPT_FIR)
                qemu_log("fast interrupt raised\n");
            else
                qemu_log("interrupt 0x%02x raised\n",
                         irq_vector);
        }
        log_cpu_state(cs, 0);
    }
    if (env->psw_u) {
        env->usp = env->regs[0];
    } else {
        env->isp = env->regs[0];
    }
    update_psw_o(env);
    env->psw = pack_psw(env);
    if ((do_irq & CPU_INTERRUPT_FIR) == 0) {
        env->isp -= 4;
        cpu_stl_all(env, env->isp, env->psw);
        env->isp -= 4;
        cpu_stl_all(env, env->isp, env->pc);
    } else {
        env->bpc = env->pc;
        env->bpsw = env->psw;
    }
    env->psw_pm = env->psw_i = env->psw_u = 0;
    env->regs[0] = env->isp;
    if (do_irq) {
        if (do_irq & CPU_INTERRUPT_FIR) {
            env->pc = env->fintv;
            env->psw_ipl = 15;
            cs->interrupt_request &= ~CPU_INTERRUPT_FIR;
            qemu_set_irq(env->ack, 0);
            return;
        } else if (do_irq & CPU_INTERRUPT_HARD) {
            env->psw_ipl = env->intlevel;
            qemu_set_irq(env->ack, 0);
        }
        env->pc = cpu_ldl_all(env, env->intb + irq_vector * 4);
        return;
    } else {
        uint32_t vec = cs->exception_index;
        env->pc = cpu_ldl_all(env, 0xffffffc0 + vec * 4);
        return;
    }
}

bool rx_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    RXCPU *cpu = RXCPU(cs);
    CPURXState *env = &cpu->env;
    int accept = 0;
    /* software interrupt */
    if (interrupt_request & CPU_INTERRUPT_SOFT) {
        accept = 1;
    }
    /* hardware interrupt (Normal) */
    if ((interrupt_request & CPU_INTERRUPT_HARD) &&
        env->psw_i && (env->psw_ipl < env->intlevel)) {
        accept = 1;
    }
    /* hardware interrupt (FIR) */
    if ((interrupt_request & CPU_INTERRUPT_FIR) &&
        env->psw_i && (env->psw_ipl < 15)) {
        accept = 1;
    }
    if (accept) {
        rx_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

hwaddr rx_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return addr;
}
