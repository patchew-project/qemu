/*
 *  RX helper functions
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "fpu/softfloat.h"

#define OP_SMOVU 1
#define OP_SMOVF 0
#define OP_SMOVB 2

#define OP_SWHILE 0
#define OP_SUNTIL 4

static void set_fpmode(CPURXState *env, uint32_t val);
static inline void QEMU_NORETURN raise_exception(CPURXState *env, int index,
                                                 uintptr_t retaddr);
uint32_t helper_mvfc(CPURXState *env, uint32_t cr)
{
    switch (cr) {
    case 0:
        return pack_psw(env);
    case 2:
        return env->psw_u ? env->regs[0] : env->usp;
    case 3:
        return env->fpsw;
    case 8:
        return env->bpsw;
    case 9:
        return env->bpc;
    case 10:
        return env->psw_u ? env->isp : env->regs[0];
    case 11:
        return env->fintv;
    case 12:
        return env->intb;
    default:
        g_assert_not_reached();
        return -1;
    }
}

void helper_mvtc(CPURXState *env, uint32_t cr, uint32_t val)
{
    switch (cr) {
    case 0:
        env->psw = val;
        rx_cpu_unpack_psw(env, 0);
        break;
    case 2:
        env->usp = val;;
        if (env->psw_u) {
            env->regs[0] = val;
        }
        break;
    case 3:
        env->fpsw = val;
        set_fpmode(env, val);
        break;
    case 8:
        env->bpsw = val;
        break;
    case 9:
        env->bpc = val;
        break;
    case 10:
        env->isp = val;
        if (!env->psw_u) {
            env->regs[0] = val;
        }
        break;
    case 11:
        env->fintv = val;
        break;
    case 12:
        env->intb = val;
        break;
    default:
        g_assert_not_reached();
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

/* fp operations */
static void update_fpsw(CPURXState *env, float32 ret, uintptr_t retaddr)
{
    int xcpt, cause, enable;

    env->psw_z = (*((uint32_t *)&ret) == 0); \
    env->psw_s = (*((uint32_t *)&ret) >= 0x80000000UL); \

    xcpt = get_float_exception_flags(&env->fp_status);

    /* Clear the cause entries */
    env->fpsw = deposit32(env->fpsw, FPSW_CAUSE, 5, 0);

    if (unlikely(xcpt)) {
        if (xcpt & float_flag_invalid) {
            env->fpsw = deposit32(env->fpsw, FPSW_CAUSE_V, 1, 1);
            env->fpsw = deposit32(env->fpsw, FPSW_FLAG_V, 1, 1);
        }
        if (xcpt & float_flag_divbyzero) {
            env->fpsw = deposit32(env->fpsw, FPSW_CAUSE_Z, 1, 1);
            env->fpsw = deposit32(env->fpsw, FPSW_FLAG_Z, 1, 1);
        }
        if (xcpt & float_flag_overflow) {
            env->fpsw = deposit32(env->fpsw, FPSW_CAUSE_O, 1, 1);
            env->fpsw = deposit32(env->fpsw, FPSW_FLAG_O, 1, 1);
        }
        if (xcpt & float_flag_underflow) {
            env->fpsw = deposit32(env->fpsw, FPSW_CAUSE_U, 1, 1);
            env->fpsw = deposit32(env->fpsw, FPSW_FLAG_U, 1, 1);
        }
        if (xcpt & float_flag_inexact) {
            env->fpsw = deposit32(env->fpsw, FPSW_CAUSE_X, 1, 1);
            env->fpsw = deposit32(env->fpsw, FPSW_FLAG_X, 1, 1);
        }

        /* Generate an exception if enabled */
        cause = extract32(env->fpsw, FPSW_CAUSE, 5);
        enable = extract32(env->fpsw, FPSW_ENABLE, 5);
        if (cause & enable) {
            raise_exception(env, 21, retaddr);
        }
    }
}

static void set_fpmode(CPURXState *env, uint32_t val)
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

#define FLOATOP(op, func)                                           \
float32 helper_##op(CPURXState *env, float32 t0, float32 t1) \
{ \
    float32 ret; \
    ret = func(t0, t1, &env->fp_status); \
    update_fpsw(env, *(uint32_t *)&ret, GETPC());        \
    return ret; \
}

FLOATOP(fadd, float32_add)
FLOATOP(fsub, float32_sub)
FLOATOP(fmul, float32_mul)
FLOATOP(fdiv, float32_div)

void helper_fcmp(CPURXState *env, float32 t0, float32 t1)
{
    int st;
    st = float32_compare(t0, t1, &env->fp_status);
    update_fpsw(env, 0, GETPC());
    env->psw_z = env->psw_s = env->psw_o = 0;
    switch (st) {
    case float_relation_equal:
        env->psw_z = 0;
        break;
    case float_relation_less:
        env->psw_s = -1;
        break;
    case float_relation_unordered:
        env->psw_o = 1 << 31;
        break;
    }
}

uint32_t helper_ftoi(CPURXState *env, float32 t0)
{
    uint32_t ret;
    ret = float32_to_int32_round_to_zero(t0, &env->fp_status);
    update_fpsw(env, ret, GETPC());
    return ret;
}

uint32_t helper_round(CPURXState *env, float32 t0)
{
    uint32_t ret;
    ret = float32_to_int32(t0, &env->fp_status);
    update_fpsw(env, ret, GETPC());
    return ret;
}

float32 helper_itof(CPURXState *env, uint32_t t0)
{
    float32 ret;
    ret = int32_to_float32(t0, &env->fp_status);
    update_fpsw(env, *(uint32_t *)&ret, GETPC());
    return ret;
}

/* string operations */
void helper_scmpu(CPURXState *env)
{
    uint8_t tmp0, tmp1;
    if (env->regs[3] == 0) {
        return;
    }
    while (env->regs[3] != 0) {
        tmp0 = cpu_ldub_data_ra(env, env->regs[1]++, GETPC());
        tmp1 = cpu_ldub_data_ra(env, env->regs[2]++, GETPC());
        env->regs[3]--;
        if (tmp0 != tmp1 || tmp0 == '\0') {
            break;
        }
    }
    env->psw_z = tmp0 - tmp1;
    env->psw_c = (tmp0 >= tmp1);
}

void helper_sstr(CPURXState *env, uint32_t sz)
{
    while (env->regs[3] != 0) {
        switch (sz) {
        case 0:
            cpu_stb_data_ra(env, env->regs[1], env->regs[2], GETPC());
            break;
        case 1:
            cpu_stw_data_ra(env, env->regs[1], env->regs[2], GETPC());
            break;
        case 2:
            cpu_stl_data_ra(env, env->regs[1], env->regs[2], GETPC());
            break;
        }
        env->regs[1] += (1 << sz);
        env->regs[3]--;
    }
}

static void smov(uint32_t mode, CPURXState *env)
{
    uint8_t tmp;
    int dir;

    dir = (mode & OP_SMOVB) ? -1 : 1;
    while (env->regs[3] != 0) {
        tmp = cpu_ldub_data_ra(env, env->regs[2], env->pc);
        cpu_stb_data_ra(env, env->regs[1], tmp, env->pc);
        env->regs[1] += dir;
        env->regs[2] += dir;
        env->regs[3]--;
        if ((mode & OP_SMOVU) && tmp == 0) {
            break;
        }
    }
}

void helper_smovu(CPURXState *env)
{
    smov(OP_SMOVU, env);
}

void helper_smovf(CPURXState *env)
{
    smov(OP_SMOVF, env);
}

void helper_smovb(CPURXState *env)
{
    smov(OP_SMOVB, env);
}

static uint32_t (* const ld[])(CPUArchState *env,
                               target_ulong ptr,
                               uintptr_t retaddr) = {
    cpu_ldub_data_ra, cpu_lduw_data_ra, cpu_ldl_data_ra,
};

static void rx_search(uint32_t mode, int sz, CPURXState *env)
{
    uint32_t tmp;

    while (env->regs[3] != 0) {
        tmp = ld[sz](env, env->regs[1], env->pc);
        env->regs[1] += 1 << (mode % 4);
        env->regs[3]--;
        if ((mode == OP_SWHILE && tmp != env->regs[2]) ||
            (mode == OP_SUNTIL && tmp == env->regs[2])) {
            break;
        }
    }
    env->psw_z = (mode == OP_SUNTIL) ?
        (tmp - env->regs[2]) : env->regs[3];
    env->psw_c = (tmp <= env->regs[2]);
}

void helper_suntil(CPURXState *env, uint32_t sz)
{
    rx_search(OP_SUNTIL, sz, env);
}

void helper_swhile(CPURXState *env, uint32_t sz)
{
    rx_search(OP_SWHILE, sz, env);
}

/* accumlator operations */
void helper_rmpa(CPURXState *env, uint32_t sz)
{
    uint64_t result_l, prev;
    int32_t result_h;
    int64_t tmp0, tmp1;

    if (env->regs[3] == 0) {
        return;
    }
    result_l = env->regs[5];
    result_l <<= 32;
    result_l |= env->regs[4];
    result_h = env->regs[6];
    env->psw_o = 0;

    while (env->regs[3] != 0) {
        tmp0 = ld[sz](env, env->regs[1], env->pc);
        tmp1 = ld[sz](env, env->regs[2], env->pc);
        tmp0 *= tmp1;
        prev = result_l;
        result_l += tmp0;
        /* carry / bollow */
        if (tmp0 < 0) {
            if (prev > result_l) {
                result_h--;
            }
        } else {
            if (prev < result_l) {
                result_h++;
            }
        }

        env->regs[1] += 1 << sz;
        env->regs[2] += 1 << sz;
    }
    env->psw_s = result_h;
    env->psw_o = (result_h != 0 && result_h != -1) << 31;
    env->regs[6] = result_h;
    env->regs[5] = result_l >> 32;
    env->regs[4] = result_l & 0xffffffff;
}

void helper_mulhi(CPURXState *env, uint32_t regs)
{
    int rs, rs2;
    long long tmp0, tmp1;

    rs = (regs >> 4) & 15;
    rs2 = regs & 15;

    tmp0 = env->regs[rs] >> 16;
    tmp1 = env->regs[rs2] >> 16;
    env->acc = (tmp0 * tmp1) << 16;
}

void helper_mullo(CPURXState *env, uint32_t regs)
{
    int rs, rs2;
    long long tmp0, tmp1;

    rs = (regs >> 4) & 15;
    rs2 = regs & 15;

    tmp0 = env->regs[rs] & 0xffff;
    tmp1 = env->regs[rs2] & 0xffff;
    env->acc = (tmp0 * tmp1) << 16;
}

void helper_machi(CPURXState *env, uint32_t regs)
{
    int rs, rs2;
    long long tmp0, tmp1;

    rs = (regs >> 4) & 15;
    rs2 = regs & 15;

    tmp0 = env->regs[rs] >> 16;
    tmp1 = env->regs[rs2] >> 16;
    env->acc += (tmp0 * tmp1) << 16;
}

void helper_maclo(CPURXState *env, uint32_t regs)
{
    int rs, rs2;
    long long tmp0, tmp1;

    rs = (regs >> 4) & 15;
    rs2 = regs & 15;

    tmp0 = env->regs[rs] & 0xffff;
    tmp1 = env->regs[rs2] & 0xffff;
    env->acc += (tmp0 * tmp1) << 16;
}

void helper_racw(CPURXState *env, uint32_t imm)
{
    int64_t acc;
    acc = env->acc;
    acc <<= (imm + 1);
    acc += 0x0000000080000000LL;
    if (acc > 0x00007fff00000000LL) {
        acc = 0x00007fff00000000LL;
    } else if (acc < -0x800000000000LL) {
        acc = -0x800000000000LL;
    } else {
        acc &= 0xffffffff00000000LL;
    }
    env->acc = acc;
}

void helper_sat(CPURXState *env, uint32_t reg)
{
    if (env->psw_o >> 31) {
        if ((int)env->psw_s < 0) {
            env->regs[reg] = 0x7fffffff;
        } else {
            env->regs[reg] = 0x80000000;
        }
    }
}

void helper_satr(CPURXState *env)
{
    if (env->psw_o >> 31) {
        if ((int)env->psw_s < 0) {
            env->regs[4] = 0x00000000;
            env->regs[5] = 0x7fffffff;
            env->regs[6] = 0xffffffff;
        } else {
            env->regs[4] = 0xffffffff;
            env->regs[5] = 0x80000000;
            env->regs[6] = 0x00000000;
        }
    }
}

/* div */
uint32_t helper_div(CPURXState *env, uint32_t num, uint32_t den)
{
    uint32_t ret = num;
    if (den != 0) {
        ret = (int32_t)num / (int32_t)den;
    }
    env->psw_o = ((num == INT_MIN && den == -1) || den == 0) << 31;
    return ret;
}

uint32_t helper_divu(CPURXState *env, uint32_t num, uint32_t den)
{
    uint32_t ret = num;
    if (den != 0) {
        ret = num / den;
    }
    env->psw_o = (den == 0) << 31;
    return ret;
}

/* exception */
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
    raise_exception(env, 0x100 + vec, 0);
}

void QEMU_NORETURN helper_rxbrk(CPURXState *env)
{
    raise_exception(env, 0x100, 0);
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
