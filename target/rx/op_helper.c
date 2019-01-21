/*
 *  RX helper functions
 *
 *  Copyright (c) 2018 Yoshinori Sato
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

static uint32_t psw_c(CPURXState *env)
{
    int m = env->op_mode & 0x000f;
    int c;
    switch (m) {
    case RX_PSW_OP_NONE:
        return env->psw_c;
    case RX_PSW_OP_ADD:
        c = (env->op_r[m - 1] < env->op_a1[m - 1]);
        break;
    case RX_PSW_OP_SUB:
    case RX_PSW_OP_STRING:
        c = (env->op_r[m - 1] <= env->op_a1[m - 1]);
        break;
    case RX_PSW_OP_BTST:
    case RX_PSW_OP_ROT:
        c = (env->op_r[m - 1] != 0);
        break;
    case RX_PSW_OP_SHLL:
    case RX_PSW_OP_SHAR:
    case RX_PSW_OP_SHLR:
        c = (env->op_a1[m - 1] != 0);
        break;
    case RX_PSW_OP_ABS:
        c = (env->op_r[m - 1] == 0);
        break;
    default:
        g_assert_not_reached();
        return -1;
    }
    env->psw_c = c;
    env->op_mode &= ~0x000f;
    return c;
}

uint32_t helper_psw_c(CPURXState *env)
{
    return psw_c(env);
}

static uint32_t psw_z(CPURXState *env)
{
    int m = (env->op_mode >> 4) & 0x000f;
    if (m == RX_PSW_OP_NONE)
        return env->psw_z;
    else {
        env->psw_z = (env->op_r[m - 1] == 0);
        env->op_mode &= ~0x00f0;
        return env->psw_z;
    }
}

static uint32_t psw_s(CPURXState *env)
{
    int m = (env->op_mode >> 8) & 0x000f;
    int s;
    switch (m) {
    case RX_PSW_OP_NONE:
        return env->psw_s;
    case RX_PSW_OP_FCMP:
        s = (env->op_r[m - 1] == 2);
        break;
    default:
        s = ((env->op_r[m - 1] & 0x80000000UL) != 0);
        break;
    }
    env->psw_s = s;
    env->op_mode &= ~0x0f00;
    return s;
}

uint32_t helper_psw_s(CPURXState *env)
{
    return psw_s(env);
}

static uint32_t psw_o(CPURXState *env)
{
    int m = (env->op_mode >> 12) & 0x000f;
    int o;

    switch (m) {
    case RX_PSW_OP_NONE:
        return env->psw_o;
    case RX_PSW_OP_ABS:
        o = (env->op_a1[m - 1] == 0x80000000UL);
        break;
    case RX_PSW_OP_ADD: {
            uint32_t r1, r2;
            r1 = ~(env->op_a1[m - 1] ^ env->op_a2[m - 1]);
            r2 = (env->op_a1[m - 1] ^ env->op_r[m - 1]);
            o = (r1 & r2) >> 31;
            break;
        }
    case RX_PSW_OP_SUB: {
            uint32_t r1, r2;
            r1 = (env->op_a1[m - 1] ^ env->op_a2[m - 1]);
            r2 = (env->op_a1[m - 1] ^ env->op_r[m - 1]);
            o = (r1 & r2) >> 31;
            break;
    }
    case RX_PSW_OP_DIV:
        o = (env->op_a1[m - 1] == 0) ||
            ((env->op_a1[m - 1] == -1) &&
             (env->op_a2[m - 1] == 0x80000000UL));
        break;
    case RX_PSW_OP_SHLL:
        o = ((env->op_a2[m - 1] & 0x80000000UL) ^
             (env->op_r[m - 1] & 0x80000000UL)) != 0;
        break;
    case RX_PSW_OP_SHAR:
        o = 0;
        break;
    default:
        g_assert_not_reached();
        return -1;
    }
    env->psw_o = o;
    env->op_mode &= ~0xf000;
    return o;
}

uint32_t helper_psw_o(CPURXState *env)
{
    return psw_o(env);
}

static uint32_t cond_psw_z(CPURXState *env, int set)
{
    return psw_z(env) ^ set;
}

static uint32_t cond_psw_c(CPURXState *env, int set)
{
    return psw_c(env) ^ set;
}

static uint32_t cond_psw_s(CPURXState *env, int set)
{
    return psw_s(env) ^ set;
}

static uint32_t cond_psw_o(CPURXState *env, int set)
{
    return psw_o(env) ^ set;
}

uint32_t helper_cond(CPURXState *env, uint32_t cond)
{
    uint32_t c, z, s, o;

    switch (cond) {
    case 0: /* z */
    case 1: /* nz */
        return cond_psw_z(env, cond);
    case 2: /* c */
    case 3: /* nc */
        return cond_psw_c(env, cond - 2);
    case 4: /* gtu (C&^Z) == 1 */
    case 5: /* leu (C&^Z) == 0 */
        c = psw_c(env);
        z = psw_z(env);
        return (c && !z) == (5 - cond);
    case 6: /* pz (S == 0) */
    case 7: /* n (S == 1) */
        return cond_psw_s(env, 7 - cond);
    case 8: /* ge (S^O)==0 */
    case 9: /* lt (S^O)==1 */
        s = psw_s(env);
        o = psw_o(env);
        return (s | o) == (cond - 8);
    case 10: /* gt ((S^O)|Z)==0 */
    case 11: /* le ((S^O)|Z)==1 */
        s = psw_s(env);
        o = psw_o(env);
        z = psw_z(env);
        return ((s ^ o) | z) == (cond - 10);
    case 12: /* o */
    case 13: /* no */
        return cond_psw_o(env, 13 - cond);
    case 14: /* always true */
        return 1;
    case 15:
        return 0;
    default:
        g_assert_not_reached();
        return -1;
    }
}

uint32_t rx_get_psw_low(CPURXState *env)
{
    return (psw_o(env) << 3) |
        (psw_s(env) << 2) |
        (psw_z(env) << 1) |
        (psw_c(env) << 0);
}

void helper_update_psw(CPURXState *env)
{
    struct {
        uint32_t *p;
        uint32_t (*fn)(CPURXState *);
    } const update_proc[] = {
        {&env->psw_c, psw_c},
        {&env->psw_z, psw_z},
        {&env->psw_s, psw_s},
        {&env->psw_o, psw_o},
    };
    int i;

    for (i = 0; i < 4; i++) {
        *(update_proc[i].p) = update_proc[i].fn(env);
    }
    g_assert((env->op_mode & 0xffff) == 0);
}

static inline void QEMU_NORETURN raise_exception(CPURXState *env, int index,
                                                 uintptr_t retaddr)
{
    CPUState *cs = CPU(rx_env_get_cpu(env));

    cs->exception_index = index;
    cpu_loop_exit_restore(cs, retaddr);
}

void QEMU_NORETURN helper_raise_privilege_violation(CPURXState *env)
{
    raise_exception(env, 20, GETPC());
}

void QEMU_NORETURN helper_raise_access_fault(CPURXState *env)
{
    raise_exception(env, 21, GETPC());
}

void QEMU_NORETURN helper_raise_illegal_instruction(CPURXState *env)
{
    raise_exception(env, 23, GETPC());
}

void QEMU_NORETURN helper_wait(CPURXState *env)
{
    CPUState *cs = CPU(rx_env_get_cpu(env));

    cs->halted = 1;
    env->in_sleep = 1;
    raise_exception(env, EXCP_HLT, 0);
}

void QEMU_NORETURN helper_debug(CPURXState *env)
{
    CPUState *cs = CPU(rx_env_get_cpu(env));

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

void QEMU_NORETURN helper_rxint(CPURXState *env, uint32_t vec)
{
    CPUState *cs = CPU(rx_env_get_cpu(env));

    cs->interrupt_request |= CPU_INTERRUPT_SOFT;
    env->sirq = vec;
    raise_exception(env, 0x100, 0);
}

void QEMU_NORETURN helper_rxbrk(CPURXState *env)
{
    CPUState *cs = CPU(rx_env_get_cpu(env));

    cs->interrupt_request |= CPU_INTERRUPT_SOFT;
    env->sirq = 0;
    raise_exception(env, 0x100, 0);
}

static void update_fpsw(CPURXState *env, uintptr_t retaddr)
{
    int xcpt, cause, enable;

    xcpt = get_float_exception_flags(&env->fp_status);

    /* Clear the cause entries */
    env->fpsw &= ~FPSW_CAUSE_MASK;

    if (unlikely(xcpt)) {
        if (xcpt & float_flag_invalid) {
            env->fpsw |= FPSW_CAUSE_V;
        }
        if (xcpt & float_flag_divbyzero) {
            env->fpsw |= FPSW_CAUSE_Z;
        }
        if (xcpt & float_flag_overflow) {
            env->fpsw |= FPSW_CAUSE_O;
        }
        if (xcpt & float_flag_underflow) {
            env->fpsw |= FPSW_CAUSE_U;
        }
        if (xcpt & float_flag_inexact) {
            env->fpsw |= FPSW_CAUSE_X;
        }

        /* Accumulate in flag entries */
        env->fpsw |= (env->fpsw & FPSW_CAUSE_MASK)
                      << (FPSW_FLAG_SHIFT - FPSW_CAUSE_SHIFT);
        env->fpsw |= ((env->fpsw >> FPSW_FLAG_V) |
                      (env->fpsw >> FPSW_FLAG_O) |
                      (env->fpsw >> FPSW_FLAG_Z) |
                      (env->fpsw >> FPSW_FLAG_U) |
                      (env->fpsw >> FPSW_FLAG_X)) << FPSW_FLAG_S;

        /* Generate an exception if enabled */
        cause = (env->fpsw & FPSW_CAUSE_MASK) >> FPSW_CAUSE_SHIFT;
        enable = (env->fpsw & FPSW_ENABLE_MASK) >> FPSW_ENABLE_SHIFT;
        if (cause & enable) {
            raise_exception(env, 21, retaddr);
        }
    }
}

void helper_to_fpsw(CPURXState *env, uint32_t val)
{
    static const int roundmode[] = {
        float_round_nearest_even,
        float_round_to_zero,
        float_round_up,
        float_round_down,
    };
    env->fpsw = val & FPSW_MASK;
    set_float_rounding_mode(roundmode[val & FPSW_RM_MASK],
                            &env->fp_status);
    set_flush_to_zero((val & FPSW_DN) != 0, &env->fp_status);
}

typedef float32 (*floatfunc)(float32 f1, float32 f2, float_status *st);
float32 helper_floatop(CPURXState *env, uint32_t op,
                       float32 t0, float32 t1)
{
    static const floatfunc fop[] = {
        float32_sub,
        NULL,
        float32_add,
        float32_mul,
        float32_div,
    };
    int st, xcpt;
    if (op != 1) {
        t0 = fop[op](t0, t1, &env->fp_status);
        update_fpsw(env, GETPC());
    } else {
        st = float32_compare(t0, t1, &env->fp_status);
        xcpt = get_float_exception_flags(&env->fp_status);
        env->fpsw &= ~FPSW_CAUSE_MASK;

        if (xcpt & float_flag_invalid) {
            env->fpsw |= FPSW_CAUSE_V;
            if (env->fpsw & FPSW_ENABLE_V) {
                raise_exception(env, 21, GETPC());
            }
        }
        switch (st) {
        case float_relation_unordered:
            return (float32)0;
        case float_relation_equal:
            return (float32)1;
        case float_relation_less:
            return (float32)2;
        }
    }
    return t0;
}

uint32_t helper_ftoi(CPURXState *env, float32 t0)
{
    uint32_t ret;
    ret = float32_to_int32_round_to_zero(t0, &env->fp_status);
    update_fpsw(env, GETPC());
    return ret;
}

uint32_t helper_round(CPURXState *env, float32 t0)
{
    uint32_t ret;
    ret = float32_to_int32(t0, &env->fp_status);
    update_fpsw(env, GETPC());
    return ret;
}

float32 helper_itof(CPURXState *env, uint32_t t0)
{
    float32 ret;
    ret = int32_to_float32(t0, &env->fp_status);
    update_fpsw(env, GETPC());
    return ret;
}

static uint32_t *cr_ptr(CPURXState *env, uint32_t cr)
{
    switch (cr) {
    case 0:
        return &env->psw;
    case 2:
        return &env->usp;
    case 3:
        return &env->fpsw;
    case 8:
        return &env->bpsw;
    case 9:
        return &env->bpc;
    case 10:
        return &env->isp;
    case 11:
        return &env->fintv;
    case 12:
        return &env->intb;
    default:
        return NULL;
    }
}

void rx_cpu_pack_psw(CPURXState *env)
{
    helper_update_psw(env);
    env->psw = (
        (env->psw_ipl << 24) | (env->psw_pm << 20) |
        (env->psw_u << 17) | (env->psw_i << 16) |
        (env->psw_o << 3) | (env->psw_s << 2) |
        (env->psw_z << 1) | (env->psw_c << 0));
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
    env->op_mode = 0;
}

uint32_t helper_mvfc(CPURXState *env, uint32_t cr)
{
    uint32_t *crp = cr_ptr(env, cr);
    if (crp != NULL) {
        if (cr == 0) {
            rx_cpu_pack_psw(env);
        }
        if ((cr == 2 && env->psw_u) || (cr == 10 && !env->psw_u)) {
            return env->regs[0];
        } else {
            return *crp;
        }
    }
    return 0;
}

void helper_mvtc(CPURXState *env, uint32_t cr, uint32_t val)
{
    uint32_t *crp = cr_ptr(env, cr);
    if (crp != NULL) {
        *crp = val;
        if ((cr == 2 && env->psw_u) ||
            (cr == 10 && !env->psw_u)) {
            env->regs[0] = val;
        }
        if (cr == 0) {
            rx_cpu_unpack_psw(env, 0);
        }
    }
}

void helper_unpack_psw(CPURXState *env)
{
    uint32_t prev_u;
    prev_u = env->psw_u;
    rx_cpu_unpack_psw(env, 1);
    if (prev_u != env->psw_u) {
        if (env->psw_u) {
            env->isp = env->regs[0];
            env->regs[0] = env->usp;
        } else {
            env->usp = env->regs[0];
            env->regs[0] = env->isp;
        }
    }
}

void helper_racw(CPURXState *env, uint32_t shift)
{
    int64_t acc;
    acc = env->acc_m;
    acc = (acc << 32) | env->acc_l;
    acc <<= shift;
    acc += 0x0000000080000000LL;
    if (acc > 0x00007FFF00000000LL) {
        acc = 0x00007FFF00000000LL;
    } else if (acc < 0xFFFF800000000000LL) {
        acc = 0xFFFF800000000000LL;
    } else {
        acc &= 0xffffffff00000000;
    }
    env->acc_m = (acc >> 32);
    env->acc_l = (acc & 0xffffffff);
}

void tlb_fill(CPUState *cs, target_ulong addr, int size,
              MMUAccessType access_type, int mmu_idx, uintptr_t retaddr)
{
    uint32_t address, physical, prot;

    /* Linear mapping */
    address = physical = addr & TARGET_PAGE_MASK;
    prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    tlb_set_page(cs, address, physical, prot, mmu_idx, TARGET_PAGE_SIZE);
}
