/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synopsys Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "sysemu/runstate.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/ioport.h"
#include "translate-all.h"
#include "target/arc/regs.h"
#include "mmu.h"
#include "hw/arc/cpudevs.h"
#include "qemu/main-loop.h"
#include "irq.h"
#include "sysemu/sysemu.h"


static target_ulong get_status32(CPUARCState *env)
{
    target_ulong value = pack_status32(&env->stat);

    /* TODO: Implement debug mode */
    if (env->stat.Uf == 1) {
        value &= 0x00000f00;
    }

    if (env->stopped) {
        value |= BIT(0);
    }

    return value;
}

static void set_status32(CPUARCState *env, target_ulong value)
{
    /* TODO: Implement debug mode. */
    bool debug_mode = false;
    if (env->stat.Uf == 1) {
        value &= 0x00000f00;
    } else if (!debug_mode) {
        value &= 0xffff6f3f;
    }

    if (((env->stat.Uf >> 7)  & 0x1) != ((value >> 7)  & 0x1)) {
        tlb_flush(env_cpu(env));
    }

    unpack_status32(&env->stat, value);

    /* Implement HALT functionality.  */
    if (value & 0x01) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void do_exception_no_delayslot(CPUARCState *env, uint32_t index,
                                      uint32_t causecode, uint32_t param)
{
    CPUState *cs = env_cpu(env);
    cpu_restore_state(cs, GETPC(), true);
    env->eret = env->pc;
    env->erbta = env->bta;

    helper_raise_exception(env, index, causecode, param);
}

target_ulong helper_norm(CPUARCState *env, uint32_t src1)
{
    int i;
    int32_t tmp = (int32_t) src1;
    if (tmp == 0 || tmp == -1) {
        return 0;
    }
    for (i = 0; i <= 31; i++) {
        if ((tmp >> i) == 0) {
            break;
        }
        if ((tmp >> i) == -1) {
            break;
        }
    }
    return i;
}

target_ulong helper_normh(CPUARCState *env, uint32_t src1)
{
    int i;
    for (i = 0; i <= 15; i++) {
        if (src1 >> i == 0) {
            break;
        }
        if (src1 >> i == -1) {
            break;
        }
    }
    return i;
}

target_ulong helper_ffs(CPUARCState *env, uint32_t src)
{
    int i;
    if (src == 0) {
        return 31;
    }
    for (i = 0; i <= 31; i++) {
        if (((src >> i) & 1) != 0) {
            break;
        }
    }
    return i;
}

target_ulong helper_fls(CPUARCState *env, uint32_t src)
{
    int i;
    if (src == 0) {
        return 0;
    }

    for (i = 31; i >= 0; i--) {
        if (((src >> i) & 1) != 0) {
            break;
        }
    }
    return i;
}

static void report_aux_reg_error(uint32_t aux)
{
    if (((aux >= ARC_BCR1_START) && (aux <= ARC_BCR1_END)) ||
        ((aux >= ARC_BCR2_START) && (aux <= ARC_BCR2_END))) {
        qemu_log_mask(LOG_UNIMP, "Undefined BCR 0x%03x\n", aux);
    }

    error_report("Undefined AUX register 0x%03x, aborting", aux);
    exit(EXIT_FAILURE);
}

void helper_sr(CPUARCState *env, uint32_t val, uint32_t aux)
{
    struct arc_aux_reg_detail *aux_reg_detail =
        arc_aux_reg_struct_for_address(aux, ARC_OPCODE_ARCv2HS);

    if (aux_reg_detail == NULL) {
        report_aux_reg_error(aux);
    }

    /* saving return address in case an exception must be raised later */
    env->host_pc = GETPC();

    switch (aux_reg_detail->id) {
    case AUX_ID_lp_start:
        env->lps = val;
        break;

    case AUX_ID_lp_end:
        env->lpe = val;
        break;

    case AUX_ID_status32:
        set_status32(env, val);
        break;

    case AUX_ID_eret:
        env->eret = val;
        break;

    case AUX_ID_erbta:
        env->erbta = val;
        break;

    case AUX_ID_bta:
        env->bta = val;
        break;

    case AUX_ID_erstatus:
        unpack_status32(&env->stat_er, val);
        break;

    case AUX_ID_ecr:
        env->ecr = val;
        break;

    case AUX_ID_efa:
        env->efa = val;
        break;

    default:
        if (aux_reg_detail->aux_reg->set_func != NULL) {
            aux_reg_detail->aux_reg->set_func(aux_reg_detail, val,
                                              (void *) env);
        } else {
            /* setting a register that does not provide one is not allowed */
            arc_raise_exception(env, EXCP_INST_ERROR);
            /* TODO: are lr and sr possible delayslot instructions ? */
            /* TODO: what is this? can it be removed? */
            do_exception_no_delayslot(env, EXCP_INST_ERROR, 0, 0);
        }
        break;
    }
    cpu_outl(aux, val);
}

static target_ulong get_debug(CPUARCState *env)
{
    target_ulong res = 0x00000000;

    res |= (env->debug.LD) ? BIT(31) : 0;
    res |= (env->debug.SH) ? BIT(30) : 0;
    res |= (env->debug.BH) ? BIT(29) : 0;
    res |= (env->debug.UB) ? BIT(28) : 0;
    res |= (env->debug.ZZ) ? BIT(27) : 0;
    res |= (env->debug.RA) ? BIT(22) : 0;
    res |= (env->debug.IS) ? BIT(11) : 0;
    res |= (env->debug.FH) ? BIT(1)  : 0;
    res |= (env->debug.SS) ? BIT(0)  : 0;

    return res;
}

static target_ulong get_identity(CPUARCState *env)
{
    target_ulong chipid = 0xffff, arcnum = 0, arcver, res;

    switch (env->family) {
    case ARC_OPCODE_ARC700:
        arcver = 0x34;
        break;

    case ARC_OPCODE_ARCv2EM:
        arcver = 0x44;
        break;

    case ARC_OPCODE_ARCv2HS:
        arcver = 0x54;
        break;

    default:
        arcver = 0;

    }

    /* TODO: in SMP, arcnum depends on the cpu instance. */
    res = ((chipid & 0xFFFF) << 16) | ((arcnum & 0xFF) << 8) | (arcver & 0xFF);
    return res;
}

target_ulong helper_lr(CPUARCState *env, uint32_t aux)
{
    target_ulong result = 0;

    struct arc_aux_reg_detail *aux_reg_detail =
        arc_aux_reg_struct_for_address(aux, ARC_OPCODE_ARCv2HS);

    if (aux_reg_detail == NULL) {
        report_aux_reg_error(aux);
    }

    /* saving return address in case an exception must be raised later */
    env->host_pc = GETPC();

    switch (aux_reg_detail->id) {
    case AUX_ID_aux_volatile:
        result = 0xc0000000;
        break;

    case AUX_ID_lp_start:
        result = env->lps;
        break;

    case AUX_ID_lp_end:
        result = env->lpe;
        break;

    case AUX_ID_identity:
        result = get_identity(env);
        break;

    case AUX_ID_exec_ctrl:
        result = 0;
        break;

    case AUX_ID_debug:
        result = get_debug(env);
        break;

    case AUX_ID_pc:
        result = env->pc & 0xfffffffe;
        break;

    case AUX_ID_status32:
        result = get_status32(env);
        break;

    case AUX_ID_mpy_build:
            result = env->mpy_build;
            break;

    case AUX_ID_isa_config:
        result = env->isa_config;
        break;

    case AUX_ID_eret:
        result = env->eret;
        break;

    case AUX_ID_erbta:
        result = env->erbta;
        break;

    case AUX_ID_erstatus:
        if (is_user_mode(env)) {
            arc_raise_exception(env, EXCP_PRIVILEGEV);
        }
        result = pack_status32(&env->stat_er);
        break;

    case AUX_ID_ecr:
        result = env->ecr;
        break;

    case AUX_ID_efa:
        result = env->efa;
        break;

    case AUX_ID_bta:
        result = env->bta;
        break;

    case AUX_ID_bta_l1:
        result = env->bta_l1;
        break;

    case AUX_ID_bta_l2:
        result = env->bta_l2;
        break;

    default:
        if (aux_reg_detail->aux_reg->get_func != NULL) {
            result = aux_reg_detail->aux_reg->get_func(aux_reg_detail,
                                                       (void *) env);
        } else {
            /* TODO: is lr and sr possible delayslot instructions ? */
            assert(0);
            arc_raise_exception(env, EXCP_INST_ERROR);
            do_exception_no_delayslot(env, EXCP_INST_ERROR, 0, 0);
        }
        break;
    }

    return result;
}

void QEMU_NORETURN helper_halt(CPUARCState *env, uint32_t npc)
{
    CPUState *cs = env_cpu(env);
    if (env->stat.Uf) {
        cs->exception_index = EXCP_PRIVILEGEV;
        env->causecode = 0;
        env->param = 0;
         /* Restore PC such that we point at the faulty instruction.  */
        env->eret = env->pc;
    } else {
        env->pc = npc;
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
    }
    cpu_loop_exit(cs);
}

void helper_rtie(CPUARCState *env)
{
    CPUState *cs = env_cpu(env);
    if (env->stat.Uf) {
        cs->exception_index = EXCP_PRIVILEGEV;
        env->causecode = 0;
        env->param = 0;
         /* Restore PC such that we point at the faulty instruction.  */
        env->eret = env->pc;
        cpu_loop_exit(cs);
        return;
    }

    if (env->stat.AEf || (env->aux_irq_act & 0xFFFF) == 0) {
        assert(env->stat.Uf == 0);

        CPU_PCL(env) = env->eret;
        env->pc = env->eret;

        env->stat = env->stat_er;
        env->bta = env->erbta;

        /* If returning to userland, restore SP.  */
        if (env->stat.Uf) {
            switchSP(env);
        }

        qemu_log_mask(CPU_LOG_INT, "[EXCP] RTIE @0x%08x ECR:0x%08x\n",
                      env->r[63], env->ecr);
    } else {
        arc_rtie_interrupts(env);
        qemu_log_mask(CPU_LOG_INT, "[IRQ] RTIE @0x%08x STATUS32:0x%08x\n",
                      env->r[63], pack_status32(&env->stat));
    }

    helper_zol_verify(env, env->pc);
}

void helper_flush(CPUARCState *env)
{
    tb_flush((CPUState *)env_archcpu(env));
}

/*
 * This should only be called from translate, via gen_raise_exception.
 * We expect that ENV->PC has already been updated.
 */

void QEMU_NORETURN helper_raise_exception(CPUARCState *env,
                                          uint32_t index,
                                          uint32_t causecode,
                                          uint32_t param)
{
    CPUState *cs = env_cpu(env);
    /* Cannot restore state here. */
    /* cpu_restore_state(cs, GETPC(), true); */
    cs->exception_index = index;
    env->causecode = causecode;
    env->param = param;
    cpu_loop_exit(cs);
}

void helper_zol_verify(CPUARCState *env, uint32_t npc)
{
    if (npc == env->lpe) {
        if (env->r[60] > 1) {
            env->r[60] -= 1;
            helper_raise_exception(env, (uint32_t) EXCP_LPEND_REACHED, 0,
                                   env->lps);
        } else {
            env->r[60] = 0;
        }
    }
}
void helper_fake_exception(CPUARCState *env, uint32_t pc)
{
    helper_raise_exception(env, (uint32_t) EXCP_FAKE, 0, pc);
}

uint32_t helper_get_status32(CPUARCState *env)
{
    return get_status32(env);
}

void helper_set_status32(CPUARCState *env, uint32_t value)
{
    set_status32(env, value);
}

uint32_t helper_carry_add_flag(uint32_t dest, uint32_t b, uint32_t c)
{
    uint32_t t1, t2, t3;

    t1 = b & c;
    t2 = b & (~dest);
    t3 = c & (~dest);
    t1 = t1 | t2 | t3;
    return (t1 >> 31) & 1;
}

uint32_t helper_overflow_add_flag(uint32_t dest, uint32_t b, uint32_t c)
{
    dest >>= 31;
    b >>= 31;
    c >>= 31;
    if ((dest == 0 && b == 1 && c == 1)
        || (dest == 1 && b == 0 && c == 0)) {
        return 1;
    } else {
        return 0;
    }
}

uint32_t helper_overflow_sub_flag(uint32_t dest, uint32_t b, uint32_t c)
{
    dest >>= 31;
    b >>= 31;
    c >>= 31;
    if ((dest == 1 && b == 0 && c == 1)
        || (dest == 0 && b == 1 && c == 0)) {
        return 1;
    } else {
        return 0;
    }
}

uint32_t helper_repl_mask(uint32_t dest, uint32_t src, uint32_t mask)
{
    uint32_t ret = dest & (~mask);
    ret |= (src & mask);

    return ret;
}

uint32_t helper_mpymu(CPUARCState *env, uint32_t b, uint32_t c)
{
    uint64_t _b = (uint64_t) b;
    uint64_t _c = (uint64_t) c;

    return (uint32_t) ((_b * _c) >> 32);
}

uint32_t helper_mpym(CPUARCState *env, uint32_t b, uint32_t c)
{
    int64_t _b = (int64_t) ((int32_t) b);
    int64_t _c = (int64_t) ((int32_t) c);

    /*
     * fprintf(stderr, "B = 0x%llx, C = 0x%llx, result = 0x%llx\n",
     *         _b, _c, _b * _c);
     */
    return (_b * _c) >> 32;
}


/*
 * throw "illegal instruction" exception if more than available
 * registers are asked to be saved/restore.
 */
static void check_enter_leave_nr_regs(CPUARCState *env,
                                      uint8_t      regs,
                                      uintptr_t    host_pc)
{
    const uint8_t rgf_num_regs = env_archcpu(env)->cfg.rgf_num_regs;
    if ((rgf_num_regs == 32 && regs > 14) ||
        (rgf_num_regs == 16 && regs >  3)) {
        CPUState *cs = env_cpu(env);
        cpu_restore_state(cs, host_pc, true);
        cs->exception_index = EXCP_INST_ERROR;
        env->causecode      = 0x00;
        env->param          = 0x00;
        env->eret           = env->pc;
        env->erbta          = env->bta;
        cpu_loop_exit(cs);
    }
}

/*
 * throw "illegal instruction sequence" exception if we are in a
 * delay/execution slot.
 */
static void check_delay_or_execution_slot(CPUARCState *env,
                                          uintptr_t    host_pc)
{
    if (env->stat.DEf || env->stat.ESf) {
        CPUState *cs = env_cpu(env);
        cpu_restore_state(cs, host_pc, true);
        cs->exception_index = EXCP_INST_ERROR;
        env->causecode      = 0x01;
        env->param          = 0x00;
        env->eret           = env->pc;
        env->erbta          = env->bta;
        cpu_loop_exit(cs);
    }
}

/*
 * Throw "misaligned" exception if 'addr' is not 32-bit aligned.
 * This check is done irrelevant of status32.AD bit.
 */
static void check_addr_is_word_aligned(CPUARCState *env,
                                       target_ulong addr,
                                       uintptr_t    host_pc)
{
    if (addr & 0x3) {
        CPUState *cs = env_cpu(env);
        cpu_restore_state(cs, host_pc, true);
        cs->exception_index = EXCP_MISALIGNED;
        env->causecode      = 0x00;
        env->param          = 0x00;
        env->efa            = addr;
        env->eret           = env->pc;
        env->erbta          = env->bta;
        cpu_loop_exit(cs);
    }
}

/*
 * helper for enter_s instruction.
 * after we are done, stack layout would be:
 * ,- top -.
 * | blink |
 * | r13   |
 * | r14   |
 * | ...   |
 * | r26   |
 * | fp    |
 * `-------'
 */
void helper_enter(CPUARCState *env, uint32_t u6)
{
    /* nothing to do? then bye-bye! */
    if (!u6) {
        return;
    }

    uint8_t regs       = u6 & 0x0f; /* u[3:0] determines registers to save */
    bool    save_fp    = u6 & 0x10; /* u[4] indicates if fp must be saved  */
    bool    save_blink = u6 & 0x20; /* u[5] indicates saving of blink      */
    uint8_t stack_size = 4 * (regs + save_fp + save_blink);

    /* number of regs to be saved must be sane */
    check_enter_leave_nr_regs(env, regs, GETPC());

    /* this cannot be executed in a delay/execution slot */
    check_delay_or_execution_slot(env, GETPC());

    /* stack must be a multiple of 4 (32 bit aligned) */
    check_addr_is_word_aligned(env, CPU_SP(env) - stack_size, GETPC());

    uint32_t tmp_sp = CPU_SP(env);

    if (save_fp) {
        tmp_sp -= 4;
        cpu_stl_data(env, tmp_sp, CPU_FP(env));
    }

    for (uint8_t gpr = regs; gpr >= 1; --gpr) {
        tmp_sp -= 4;
        cpu_stl_data(env, tmp_sp, env->r[13 + gpr - 1]);
    }

    if (save_blink) {
        tmp_sp -= 4;
        cpu_stl_data(env, tmp_sp, CPU_BLINK(env));
    }

    CPU_SP(env) = tmp_sp;

    /* now that sp has been allocated, shall we write it to fp? */
    if (save_fp) {
        CPU_FP(env) = CPU_SP(env);
    }
}

/*
 * helper for leave_s instruction.
 * a stack layout of below is assumed:
 * ,- top -.
 * | blink |
 * | r13   |
 * | r14   |
 * | ...   |
 * | r26   |
 * | fp    |
 * `-------'
 */
void helper_leave(CPUARCState *env, uint32_t u7)
{
    /* nothing to do? then bye-bye! */
    if (!u7) {
        return;
    }

    uint8_t regs       = u7 & 0x0f; /* u[3:0] determines registers to save */
    bool restore_fp    = u7 & 0x10; /* u[4] indicates if fp must be saved  */
    bool restore_blink = u7 & 0x20; /* u[5] indicates saving of blink      */
    bool jump_to_blink = u7 & 0x40; /* u[6] should we jump to blink?       */

    /* number of regs to be restored must be sane */
    check_enter_leave_nr_regs(env, regs, GETPC());

    /* this cannot be executed in a delay/execution slot */
    check_delay_or_execution_slot(env, GETPC());

    /*
     * stack must be a multiple of 4 (32 bit aligned). we must take into
     * account if sp is going to use fp's value or not.
     */
    const target_ulong addr = restore_fp ? CPU_FP(env) : CPU_SP(env);
    check_addr_is_word_aligned(env, addr, GETPC());

    /*
     * if fp is in the picture, then first we have to use the current
     * fp as the stack pointer for restoring.
     */
    if (restore_fp) {
        CPU_SP(env) = CPU_FP(env);
    }

    uint32_t tmp_sp = CPU_SP(env);

    if (restore_blink) {
        CPU_BLINK(env) = cpu_ldl_data(env, tmp_sp);
        tmp_sp += 4;
    }

    for (uint8_t gpr = 0; gpr < regs; ++gpr) {
        env->r[13 + gpr] = cpu_ldl_data(env, tmp_sp);
        tmp_sp += 4;
    }

    if (restore_fp) {
        CPU_FP(env) = cpu_ldl_data(env, tmp_sp);
        tmp_sp += 4;
    }

    CPU_SP(env) = tmp_sp;

    /* now that we are done, should we jump to blink? */
    if (jump_to_blink) {
        CPU_PCL(env) = CPU_BLINK(env);
        env->pc      = CPU_BLINK(env);
    }
}

/*
 * uint32_t lf_variable = 0;
 * uint32_t helper_get_lf(void)
 * {
 *   return lf_variable;
 * }
 * void helper_set_lf(uint32_t v)
 * {
 *   lf_variable = v;
 * }
 */

/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
