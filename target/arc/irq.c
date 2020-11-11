/*
 * QEMU ARC CPU - IRQ subsystem
 *
 * Copyright (c) 2020 Synopsys Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "irq.h"
#include "exec/cpu_ldst.h"
#include "translate.h"
#include "qemu/host-utils.h"

/* Static functions and variables. */

static uint32_t save_reg_pair_32[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
};

static uint32_t save_reg_pair_16[] = {
    0, 2, 10, 12, 14, 26, 28, 30
};

bool enabled_interrupts = false;

/* Given a struct STATUS_R, pack it to 32 bit. */
uint32_t pack_status32(status_t *status_r)
{
    uint32_t res = 0x00000000;

    res |= (status_r->IEf & 0x1) << 31;
    res |= (status_r->USf & 0x1) << 20;
    res |= (status_r->ADf & 0x1) << 19;
    res |= (status_r->RBf & 0x7) << 16;
    res |= (status_r->ESf & 0x1) << 15;
    res |= (status_r->SCf & 0x1) << 14;
    res |= (status_r->DZf & 0x1) << 13;
    res |= (status_r->Lf  & 0x1) << 12;
    res |= (status_r->Zf  & 0x1) << 11;
    res |= (status_r->Nf  & 0x1) << 10;
    res |= (status_r->Cf  & 0x1) << 9;
    res |= (status_r->Vf  & 0x1) << 8;
    res |= (status_r->Uf  & 0x1) << 7;
    res |= (status_r->DEf & 0x1) << 6;
    res |= (status_r->AEf & 0x1) << 5;
    res |= (status_r->Ef  & 0xf) << 1;

    return res;
}

/* Reverse of the above function. */
void unpack_status32(status_t *status_r, uint32_t value)
{
    status_r->IEf = ((value >> 31) & 0x1);
    status_r->USf = ((value >> 20) & 0x1);
    status_r->ADf = ((value >> 19) & 0x1);
    status_r->RBf = ((value >> 16) & 0x7);
    status_r->ESf = ((value >> 15) & 0x1);
    status_r->SCf = ((value >> 14) & 0x1);
    status_r->DZf = ((value >> 13) & 0x1);
    status_r->Lf  = ((value >> 12) & 0x1);
    status_r->Zf  = ((value >> 11) & 0x1);
    status_r->Nf  = ((value >> 10) & 0x1);
    status_r->Cf  = ((value >> 9)  & 0x1);
    status_r->Vf  = ((value >> 8)  & 0x1);
    status_r->Uf  = ((value >> 7)  & 0x1);
    status_r->DEf = ((value >> 6)  & 0x1);
    status_r->AEf = ((value >> 5)  & 0x1);
    status_r->Ef  = ((value >> 1)  & 0xf);
}

/* Return from fast interrupts. */

static void arc_rtie_firq(CPUARCState *env)
{
    assert(env->stat.AEf == 0);

    qemu_log_mask(CPU_LOG_INT, "[IRQ] exit firq: U=%d, AUX_IRQ_ACT.U=%d\n",
                  env->stat.Uf, env->aux_irq_act >> 31);

    /* Clear currently active interrupt. */
    env->aux_irq_act &= 0xfffffffe;

    /* Check if we need to restore userland SP. */
    if (((env->aux_irq_act & 0xFFFF) == 0) && (env->aux_irq_act & 0x80000000)) {
        switchSP(env);
    }

    env->stat = env->stat_l1; /* FIXME use status32_p0 reg. */
    env->aux_irq_act &= ~(env->stat.Uf << 31); /* Keep U-bit in sync. */

    /* FIXME! fix current reg bank if RB bit is changed. */

    CPU_PCL(env) = CPU_ILINK(env);
    env->pc = CPU_ILINK(env);
}

/* Implements a pop operation from the CPU stack. */
static uint32_t irq_pop(CPUARCState *env, const char *str)
{
    uint32_t rval;
    rval = cpu_ldl_data(env, CPU_SP(env));

    qemu_log_mask(CPU_LOG_INT, "[IRQ] Pop [SP:0x%08x] => 0x%08x (%s)\n",
                  CPU_SP(env), rval, str ? str : "unk");
    CPU_SP(env) += 4;
    return rval;
}

/* Return from regular interrupts. */

static void arc_rtie_irq(CPUARCState *env)
{
    uint32_t tmp;
    ARCCPU *cpu = env_archcpu(env);

    assert((env->aux_irq_act & 0xFFFF) != 0);
    assert(env->stat.AEf == 0);

    /* Clear currently active interrupt. */
    tmp = ctz32(env->aux_irq_act & 0xffff);

    qemu_log_mask(CPU_LOG_INT,
                  "[IRQ] exit irq:%d IRQ_ACT:0x%08x PRIO:%d\n",
                  env->icause[tmp], env->aux_irq_act, tmp);

    /*
     * FIXME! I assume the current active interrupt is the one which is
     * the highest in the aux_irq_act register.
     */
    env->aux_irq_act &= ~(1 << tmp);

    qemu_log_mask(CPU_LOG_INT,
                  "[IRQ] exit irq:%d U:%d AE:%d IE:%d E:%d IRQ_ACT:0x%08x\n",
                  env->icause[tmp], env->stat.Uf, env->stat.AEf, env->stat.IEf,
                  env->stat.Ef, env->aux_irq_act);

    if (((env->aux_irq_act & 0xffff) == 0) &&
        (env->aux_irq_act & 0x80000000) && (env->aux_irq_ctrl & (1 << 11))) {
        switchSP(env);
    }

    /* Pop requested number of registers. */
    /* FIXME! select rf16 when needed. */
    uint32_t *save_reg_pair = save_reg_pair_32;
    char regname[6];
    uint32_t i;
    for (i = 0; i < (env->aux_irq_ctrl & 0x1F); ++i) {
        sprintf(regname, "r%d", save_reg_pair[i]);
        env->r[save_reg_pair[i]] = irq_pop(env, (const char *) regname);
        sprintf(regname, "r%d", save_reg_pair[i] + 1);
        env->r[save_reg_pair[i] + 1] = irq_pop(env, (const char *) regname);
    }

    /* Pop BLINK */
    if (env->aux_irq_ctrl & (1 << 9) && ((env->aux_irq_ctrl & 0x1F) != 16)) {
        CPU_BLINK(env) = irq_pop(env, "blink");
    }

    /* Pop lp_end, lp_start, lp_count if aux_irq_ctrl.l bit is set. */
    if (env->aux_irq_ctrl & (1 << 10)) {
        env->lpe = irq_pop(env, "LP_END");
        env->lps = irq_pop(env, "LP_START");
        CPU_LP(env) = irq_pop(env, "lp");
    }

    /*
     * Pop EI_BASE, JLI_BASE, LDI_BASE if LP bit is set and Code
     * Density feature is enabled. FIXME!
     */
    if (cpu->cfg.code_density && (env->aux_irq_ctrl & (1 << 13))) {
        /* FIXME! env->aux_ei_base  = irq_pop(env); */
        /* FIXME! env->aux_ldi_base = irq_pop(env); */
        /* FIXME! env->aux_jli_base = irq_pop(env); */
        irq_pop(env, "dummy EI_BASE");
        irq_pop(env, "dummy LDI_BASE");
        irq_pop(env, "dummy JLI_BASE");
    }

    CPU_ILINK(env) = irq_pop(env, "PC"); /* CPU PC*/
    uint32_t tmp_stat = irq_pop(env, "STATUS32"); /* status. */
    unpack_status32(&env->stat, tmp_stat);

    /* Late switch to Kernel SP if previously in User thread. */
    if (((env->aux_irq_act & 0xffff) == 0)
        && env->stat.Uf && !(env->aux_irq_ctrl & (1 << 11))) {
        switchSP(env);
    }

    env->aux_irq_act &= ~(env->stat.Uf << 31); /* Keep U-bit in sync. */
    CPU_PCL(env) = CPU_ILINK(env);
    env->pc = CPU_ILINK(env);
}

/* Helper, implements entering in a fast irq. */
static void arc_enter_firq(ARCCPU *cpu, uint32_t vector)
{
    CPUARCState *env = &cpu->env;

    assert(env->stat.DEf == 0);
    assert(env->stat.is_delay_slot_instruction == 0);

    /* Reset RTC state machine -> AUX_RTC_CTRL &= 0x3fffffff */
    qemu_log_mask(CPU_LOG_INT,
                  "[IRQ] enter firq:%d U:%d AE:%d IE:%d E:%d\n",
                  vector, env->stat.Uf, env->stat.AEf, env->stat.IEf,
                  env->stat.Ef);

    /* Switch SP with AUX_SP. */
    if (env->stat.Uf) {
        switchSP(env);
    }

    /* Clobber ILINK with address of interrupting instruction. */
    CPU_ILINK(env) = env->pc & 0xfffffffe;
    env->stat_l1 = env->stat;

    /* Set stat {Z = U; U = 0; L = 1; ES = 0; DZ = 0; DE = 0;} */
    env->stat.Lf = 1;
    env->stat.Zf = env->stat.Uf; /* Old User/Kernel bit. */
    env->stat.Uf = 0;
    env->stat.ESf = 0;
    env->stat.DZf = 0;
    env->stat.DEf = 0;
    env->stat.is_delay_slot_instruction = 0;

    /* Set .RB to 1 if additional register banks are specified. */
    if (cpu->cfg.rgf_num_banks > 0) {
        env->stat.RBf = 1;
        /* FIXME! Switch to first register bank. */
    }
}

/* Implements a push operation to the CPU stack. */
static void irq_push(CPUARCState *env, uint32_t regval, const char *str)
{
    CPU_SP(env) -= 4;
    qemu_log_mask(CPU_LOG_INT, "[IRQ] Push [SP:0x%08x] <= 0x%08x (%s)\n",
                  CPU_SP(env), regval, str ? str : "unk");
    uint32_t uf = env->stat.Uf;
    env->stat.Uf = 0;
    cpu_stl_data(env, CPU_SP(env), regval);
    env->stat.Uf = uf;
}

/* Helper, implements the steps required to enter a simple interrupt. */
static void arc_enter_irq(ARCCPU *cpu, uint32_t vector)
{
    CPUARCState *env = &cpu->env;

    assert(env->stat.DEf == 0);
    assert(env->stat.is_delay_slot_instruction == 0);

    /* Reset RTC state machine -> AUX_RTC_CTRL &= 0x3fffffff */
    qemu_log_mask(CPU_LOG_INT, "[IRQ] enter irq:%d U:%d AE:%d IE:%d E:%d\n",
                  vector, env->stat.Uf, env->stat.AEf, env->stat.IEf,
                  env->stat.Ef);

    /* Early switch to kernel sp if previously in user thread */
    if (env->stat.Uf && !(env->aux_irq_ctrl & (1 << 11))) {
        switchSP(env);
    }

    /* Clobber ILINK with address of interrupting instruction. */
    CPU_ILINK(env) = env->pc & 0xfffffffe;

    /* Start pushing regs and stat. */
    irq_push(env, pack_status32(&env->stat), "STATUS32");
    irq_push(env, env->pc, "PC");

    /*
     * Push EI_BASE, JLI_BASE, LDI_BASE if LP bit is set and Code
     * Density feature is enabled.
     */
    if (cpu->cfg.code_density && (env->aux_irq_ctrl & (1 << 13))) {
        /* FIXME! irq_push(env, env->aux_jli_base, "JLI_BASE"); */
        /* FIXME! irq_push(env, env->aux_ldi_base, "LDI_BASE""); */
        /* FIXME! irq_push(env, env->aux_ei_base, "EI_BASE"); */
        irq_push(env, 0xdeadbeef, "dummy JLI_BASE");
        irq_push(env, 0xdeadbeef, "dummy LDI_BASE");
        irq_push(env, 0xdeadbeef, "dummy EI_BASE");
    }

    /* Push LP_COUNT, LP_START, LP_END registers if required. */
    if (env->aux_irq_ctrl & (1 << 10)) {
        irq_push(env, CPU_LP(env), "lp");
        irq_push(env, env->lps, "LP_START");
        irq_push(env, env->lpe, "LP_END");
    }

    /* Push BLINK register if required */
    if (env->aux_irq_ctrl & (1 << 9) && ((env->aux_irq_ctrl & 0x1F) != 16)) {
        irq_push(env, CPU_BLINK(env), "blink");
    }

    /* Push selected AUX_IRQ_CTRL.NR of registers onto stack. */
    uint32_t *save_reg_pair = cpu->cfg.rgf_num_regs == 32 ?
        save_reg_pair_32 : save_reg_pair_16;
    const uint32_t regspair = (cpu->cfg.rgf_num_regs == 32 ? 16 : 8);
    const uint32_t upperlimit = (env->aux_irq_ctrl & 0x1F) < regspair ?
        env->aux_irq_ctrl & 0x1F : regspair;
    char regname[6];
    uint32_t i;

    for (i = upperlimit; i > 0; --i) {
        sprintf(regname, "r%d", save_reg_pair[i - 1] + 1);
        irq_push(env, env->r[save_reg_pair[i - 1] + 1], (const char *) regname);
        sprintf(regname, "r%d", save_reg_pair[i - 1]);
        irq_push(env, env->r[save_reg_pair[i - 1]], (const char *) regname);
    }

    /* Late switch to Kernel SP if previously in User thread. */
    if (env->stat.Uf && (env->aux_irq_ctrl & (1 << 11))) {
        switchSP(env);
    }

    /* Set STATUS bits */
    env->stat.Zf = env->stat.Uf; /* Old User/Kernel mode. */
    env->stat.Lf = 1;
    env->stat.ESf = 0;
    env->stat.DZf = 0;
    env->stat.DEf  = 0;
    env->stat.Uf = 0;
}

/* Function implementation for reading the IRQ related aux regs. */
uint32_t aux_irq_get(const struct arc_aux_reg_detail *aux_reg_detail,
                     void *data)
{
    CPUARCState *env = (CPUARCState *) data;
    uint32_t tmp;

    /* extract selected IRQ. */
    const uint32_t irq = env->irq_select;
    const arc_irq_t *irq_bank = &env->irq_bank[irq];

    switch (aux_reg_detail->id) {
    case AUX_ID_irq_pending:
        return irq_bank->pending | (irq > 15 ? (env->aux_irq_hint == irq) : 0);

    case AUX_ID_irq_select:
        return env->irq_select;

    case AUX_ID_irq_priority:
        return irq_bank->priority;

    case AUX_ID_irq_trigger:
        return irq_bank->trigger;

    case AUX_ID_irq_status:
        return (irq_bank->priority
                | irq_bank->enable << 4
                | irq_bank->trigger << 5
                | (irq_bank->pending
                   | (irq > 15 ? ((env->aux_irq_hint == irq) << 31) : 0)));

    case AUX_ID_aux_irq_act:
        return env->aux_irq_act;

    case AUX_ID_aux_irq_ctrl:
        return env->aux_irq_ctrl;

    case AUX_ID_icause:
        if ((env->aux_irq_act & 0xffff) == 0) {
            return 0;
        }
        tmp = ctz32(env->aux_irq_act & 0xffff);
        return env->icause[tmp];

    case AUX_ID_irq_build:
        return env->irq_build;

    case AUX_ID_int_vector_base:
        return env->intvec;

    case AUX_ID_vecbase_ac_build:
        return env->vecbase_build;
        break;

    case AUX_ID_aux_user_sp:
        return env->aux_user_sp;

    case AUX_ID_aux_irq_hint:
        return env->aux_irq_hint;

    default:
        break;
    }
    return 0;
}

/* Function implementation for writing the IRQ related aux regs. */
void aux_irq_set(const struct arc_aux_reg_detail *aux_reg_detail,
                 uint32_t val, void *data)
{
    CPUARCState *env = (CPUARCState *) data;
    const uint32_t irq = env->irq_select;
    arc_irq_t *irq_bank = &env->irq_bank[irq];

    qemu_log_mask(CPU_LOG_INT, "[IRQ] set aux_reg: %s, with 0x%08x\n",
                  arc_aux_reg_name[aux_reg_detail->id],
                  val);


    switch (aux_reg_detail->id) {
    case AUX_ID_irq_select:
        if (val <= (16 + ((env->irq_build >> 8) & 0xff)))
            env->irq_select = val;
        else
            qemu_log_mask(LOG_UNIMP,
                          "[IRQ] Invalid write 0x%08x to IRQ_SELECT aux reg.\n",
                          val);
        break;

    case AUX_ID_aux_irq_hint:
        qemu_mutex_lock_iothread();
        if (val == 0) {
            qemu_irq_lower(env->irq[env->aux_irq_hint]);
        } else if (val >= 16) {
            qemu_irq_raise(env->irq[val]);
            env->aux_irq_hint = val;
        }
        qemu_mutex_unlock_iothread();
        break;

    case AUX_ID_irq_pulse_cancel:
        irq_bank->pending = irq_bank->trigger ? (val & 0x01) : 0;
        break;

    case AUX_ID_irq_trigger:
        irq_bank->trigger = val & 0x01;
        break;

    case AUX_ID_irq_priority:
        if (val <= ((env->irq_build >> 24) & 0x0f)) {
            irq_bank->priority = val & 0x0f;
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "[IRQ] Invalid write 0x%08x to IRQ_PRIORITY aux reg.\n",
                          val);
        }
        break;

    case AUX_ID_aux_irq_ctrl:
        env->aux_irq_ctrl = val & 0x2e1f;
        break;

    case AUX_ID_irq_enable:
        irq_bank->enable = val & 0x01;
        break;

    case AUX_ID_aux_irq_act:
        env->aux_irq_act = val & 0x8000ffff;
        break;

    case AUX_ID_int_vector_base:
        env->intvec = val;
        break;

    case AUX_ID_aux_user_sp:
        env->aux_user_sp = val;
        break;

    default:
        break;
    }
}

/* Check if we can interrupt the cpu. */

bool arc_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    ARCCPU *cpu = ARC_CPU(cs);
    CPUARCState *env = &cpu->env;
    bool found = false;
    uint32_t vectno = 0;
    uint32_t offset, priority;

    /* Check if we should execute this interrupt. */
    if (env->stat.Hf
        /* The interrupts are enabled. */
        || (env->stat.IEf == 0)
        /* We are not in an exception. */
        || env->stat.AEf
        /* Disable interrupts to happen after MissI exceptions. */
        || enabled_interrupts == false
        /* In a delay slot of branch */
        || env->stat.is_delay_slot_instruction
        || env->stat.DEf
        || (!(interrupt_request & CPU_INTERRUPT_HARD))) {
        return false;
    }

    /* Check if any interrupts are pending. */
    if (!env->irq_priority_pending
        /* Or we are serving at the same priority level. */
        || (ctz32(env->irq_priority_pending) >= ctz32(env->aux_irq_act))) {
        return false;
    }

    /* Find the first IRQ to serve. */
    priority = 0;
    do {
        for (vectno = 0;
             vectno < cpu->cfg.number_of_interrupts; vectno++) {
            if (env->irq_bank[16 + vectno].priority == priority
                && env->irq_bank[16 + vectno].enable
                && env->irq_bank[16 + vectno].pending) {
                found = true;
                break;
            }
        }
    } while (!found && ((++priority) <= env->stat.Ef));

    /* No valid interrupt has been found. */
    if (!found) {
        return false;
    }

    qemu_log_mask(CPU_LOG_INT, "[IRQ] interrupt at pc=0x%08x\n", env->pc);

    /* Adjust vector number. */
    vectno += 16;

    /* Set the AUX_IRQ_ACT. */
    if ((env->aux_irq_act & 0xffff) == 0) {
        env->aux_irq_act |= env->stat.Uf << 31;
    }
    env->aux_irq_act |= 1 << priority;

    /* Set ICAUSE register. */
    env->icause[priority] = vectno;

    /* Do FIRQ if possible. */
    if (cpu->cfg.firq_option && priority == 0) {
        arc_enter_firq(cpu, vectno);
    } else {
        arc_enter_irq(cpu, vectno);
    }

    /* XX. The PC is set with the appropriate exception vector. */
    offset = vectno << 2;
    env->pc = cpu_ldl_code(env, env->intvec + offset);
    CPU_PCL(env) = env->pc & 0xfffffffe;

    qemu_log_mask(CPU_LOG_INT, "[IRQ] isr=0x%08x vec=0x%08x, priority=0x%04x\n",
                  env->pc, offset, priority);

    return true;
}

/* To be called in the RTIE helper. */

bool arc_rtie_interrupts(CPUARCState *env)
{
    ARCCPU *cpu = env_archcpu(env);

    if (env->stat.AEf || ((env->aux_irq_act & 0xffff) == 0)) {
        return false;
    }

    /* FIXME! Reset RTC state. */

    if ((env->aux_irq_act & 0xffff) == 1 && cpu->cfg.firq_option) {
        arc_rtie_firq(env);
    } else {
        arc_rtie_irq(env);
    }
    return true;
}

/* Switch between AUX USER SP and CPU's SP. */
void switchSP(CPUARCState *env)
{
    uint32_t tmp;
    qemu_log_mask(CPU_LOG_INT,
                  "[%s] swap: r28 = 0x%08x  AUX_USER_SP = 0x%08x\n",
                  (env->aux_irq_act & 0xFFFF) ? "IRQ" : "EXCP",
                  CPU_SP(env), env->aux_user_sp);

    tmp = env->aux_user_sp;
    env->aux_user_sp = CPU_SP(env);
    CPU_SP(env) = tmp;
    /*
     * TODO: maybe we need to flush the tcg buffer to switch into
     * kernel mode.
     */
}

/* Reset the IRQ subsytem. */
void arc_resetIRQ(ARCCPU *cpu)
{
    CPUARCState *env = &cpu->env;
    uint32_t i;

    if (!cpu->cfg.has_interrupts) {
        return;
    }

    for (i = 0; i < (cpu->cfg.number_of_interrupts & 0xff); i++) {
        env->irq_bank[16 + i].enable = 1;
    }

    if (cpu->cfg.has_timer_0) {
        /* FIXME! add build default timer0 priority. */
        env->irq_bank[16].priority = 0;
    }

    if (cpu->cfg.has_timer_1) {
        /* FIXME! add build default timer1 priority. */
        env->irq_bank[17].priority = 0;
    }

    qemu_log_mask(CPU_LOG_RESET, "[IRQ] Reset the IRQ subsystem.");
}

/* Initializing the IRQ subsystem. */
void arc_initializeIRQ(ARCCPU *cpu)
{
    CPUARCState *env = &cpu->env;
    uint32_t i;

    if (cpu->cfg.has_interrupts) {
        /* FIXME! add N (NMI) bit. */
        env->irq_build = 0x01 | ((cpu->cfg.number_of_interrupts & 0xff) << 8) |
            ((cpu->cfg.external_interrupts & 0xff) << 16) |
            ((cpu->cfg.number_of_levels & 0x0f) << 24) |
            (cpu->cfg.firq_option ? (1 << 28) : 0);

        for (i = 0; i < (cpu->cfg.number_of_interrupts & 0xff); i++) {
            env->irq_bank[16 + i].enable = 1;
        }

        env->vecbase_build = (cpu->cfg.intvbase_preset & (~0x3ffff))
            | (0x04 << 2);
        env->intvec = cpu->cfg.intvbase_preset & (~0x3ffff);
    } else {
        env->irq_build = 0;
    }
}
