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
#include "qemu/log.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/cpu-loop.h"
#include "fpu/softfloat.h"
#include "tcg/debug-assert.h"

static inline G_NORETURN
void raise_exception(CPURXState *env, int index,
                     uintptr_t retaddr);

static void _set_psw(CPURXState *env, uint32_t psw, uint32_t rte)
{
    uint32_t prev_u;
    prev_u = env->psw_u;
    rx_cpu_unpack_psw(env, psw, rte);
    if (prev_u != env->psw_u) {
        /* switch r0  */
        if (env->psw_u) {
            env->isp = env->regs[0];
            env->regs[0] = env->usp;
        } else {
            env->usp = env->regs[0];
            env->regs[0] = env->isp;
        }
    }
}

void helper_set_psw(CPURXState *env, uint32_t psw)
{
    _set_psw(env, psw, 0);
}

void helper_set_psw_rte(CPURXState *env, uint32_t psw)
{
    _set_psw(env, psw, 1);
}

uint32_t helper_pack_psw(CPURXState *env)
{
    return rx_cpu_pack_psw(env);
}

#define SET_FPSW(b)                                             \
    do {                                                        \
        env->fpsw = FIELD_DP32(env->fpsw, FPSW, C ## b, 1);     \
        if (!FIELD_EX32(env->fpsw, FPSW, E ## b)) {             \
            env->fpsw = FIELD_DP32(env->fpsw, FPSW, F ## b, 1); \
        }                                                       \
    } while (0)

/* fp operations */
static void update_fpsw(CPURXState *env, float32 ret, uintptr_t retaddr)
{
    int xcpt, cause, enable;

    env->psw_z = ret & ~(1 << 31); /* mask sign bit */
    env->psw_s = ret;

    xcpt = get_float_exception_flags(&env->fp_status);

    /* Clear the cause entries */
    env->fpsw = FIELD_DP32(env->fpsw, FPSW, CAUSE, 0);

    /* set FPSW */
    if (unlikely(xcpt)) {
        if (xcpt & float_flag_invalid) {
            SET_FPSW(V);
        }
        if (xcpt & float_flag_divbyzero) {
            SET_FPSW(Z);
        }
        if (xcpt & float_flag_overflow) {
            SET_FPSW(O);
        }
        if (xcpt & float_flag_underflow) {
            SET_FPSW(U);
        }
        if (xcpt & float_flag_inexact) {
            SET_FPSW(X);
        }
        if ((xcpt & (float_flag_input_denormal_flushed
                     | float_flag_output_denormal_flushed))
            && !FIELD_EX32(env->fpsw, FPSW, DN)) {
            env->fpsw = FIELD_DP32(env->fpsw, FPSW, CE, 1);
        }

        /* update FPSW_FLAG_S */
        if (FIELD_EX32(env->fpsw, FPSW, FLAGS) != 0) {
            env->fpsw = FIELD_DP32(env->fpsw, FPSW, FS, 1);
        }

        /* Generate an exception if enabled */
        cause = FIELD_EX32(env->fpsw, FPSW, CAUSE);
        enable = FIELD_EX32(env->fpsw, FPSW, ENABLE);
        enable |= 1 << 5; /* CE always enabled */
        if (cause & enable) {
            raise_exception(env, 21, retaddr);
        }
    }
}

/*
 * RXv3 register bank save function. SAVE copies R1-R15, the USP, the FPSW
 * and the accumulator into the selected save register bank; RSTR copies them
 * back. R0 is deliberately excluded: the stack pointer is not banked.
 *
 * The banks are internal CPU state that only these two instructions can
 * reach, so there is no memory layout to model. How many banks a part
 * provides is implementation defined and comes from the CPU model.
 */
static RXSaveBank *rx_save_bank(CPURXState *env, uint32_t bank,
                                const char *insn)
{
    uint32_t n = RX_CPU_GET_CLASS(env_cpu(env))->num_save_banks;

    if (bank >= n) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rx: %s with out of range bank %u (this CPU has %u)\n",
                      insn, bank, n);
        return NULL;
    }
    return &env->bank[bank];
}

void helper_save(CPURXState *env, uint32_t bank)
{
    RXSaveBank *b = rx_save_bank(env, bank, "save");
    int i;

    if (!b) {
        return;
    }
    for (i = 1; i < NUM_REGS; i++) {
        b->regs[i] = env->regs[i];
    }
    b->usp = env->psw_u ? env->regs[0] : env->usp;
    b->fpsw = env->fpsw;
    b->acc = env->acc;
}

void helper_rstr(CPURXState *env, uint32_t bank)
{
    RXSaveBank *b = rx_save_bank(env, bank, "rstr");
    int i;

    if (!b) {
        return;
    }
    for (i = 1; i < NUM_REGS; i++) {
        env->regs[i] = b->regs[i];
    }
    if (env->psw_u) {
        env->regs[0] = b->usp;
    } else {
        env->usp = b->usp;
    }
    env->fpsw = b->fpsw;
    env->acc = b->acc;
}

void helper_set_fpsw(CPURXState *env, uint32_t val)
{
    static const int roundmode[] = {
        float_round_nearest_even,
        float_round_to_zero,
        float_round_up,
        float_round_down,
    };
    uint32_t fpsw = env->fpsw;
    fpsw |= 0x7fffff03;
    val &= ~0x80000000;
    fpsw &= val;
    FIELD_DP32(fpsw, FPSW, FS, FIELD_EX32(fpsw, FPSW, FLAGS) != 0);
    env->fpsw = fpsw;
    set_float_rounding_mode(roundmode[FIELD_EX32(env->fpsw, FPSW, RM)],
                            &env->fp_status);
}

#define FLOATOP(op, func)                                           \
    float32 helper_##op(CPURXState *env, float32 t0, float32 t1)    \
    {                                                               \
        float32 ret;                                                \
        ret = func(t0, t1, &env->fp_status);                        \
        update_fpsw(env, *(uint32_t *)&ret, GETPC());               \
        return ret;                                                 \
    }

FLOATOP(fadd, float32_add)
FLOATOP(fsub, float32_sub)
FLOATOP(fmul, float32_mul)
FLOATOP(fdiv, float32_div)

/*
 * RXv3 double-precision FPU helpers.
 *
 * The DPFPU has its own status word: "The single-precision floating-point
 * status word (FPSW) is neither referred to nor updated in double-precision
 * floating-point arithmetic operations." DPSW carries its own rounding mode
 * (DRM) and denormal handling (DDN), so double precision runs on a separate
 * softfloat state rather than sharing fp_status. The PSW is not touched
 * either; DCMP records its answer in DCMR and MVFDR moves it to PSW.Z.
 */
#define SET_DPSW(b)                                                     \
    do {                                                                \
        dpsw = FIELD_DP32(dpsw, DPSW, DC ## b, 1);                      \
        if (!FIELD_EX32(dpsw, DPSW, DE ## b)) {                         \
            dpsw = FIELD_DP32(dpsw, DPSW, DF ## b, 1);                  \
        }                                                               \
    } while (0)

/* Apply DPSW's rounding mode and clear the sticky softfloat flags. */
static void dp_begin(CPURXState *env)
{
    static const int roundmode[] = {
        float_round_nearest_even,
        float_round_to_zero,
        float_round_up,
        float_round_down,
    };
    uint32_t dpsw = env->dcr[RX_DCR_DPSW];

    set_float_rounding_mode(roundmode[FIELD_EX32(dpsw, DPSW, DRM)],
                            &env->dp_status);
    set_float_exception_flags(0, &env->dp_status);
}

/*
 * Record the outcome of a DP arithmetic instruction in DPSW. Every such
 * instruction except DABS and DNEG recomputes the DC* cause bits, so they
 * are cleared first; the DF* flags accumulate and are only set while the
 * matching DE* enable is clear.
 */
static void update_dpsw(CPURXState *env)
{
    uint32_t decnt = env->dcr[RX_DCR_DECNT];
    uint32_t dpsw = env->dcr[RX_DCR_DPSW];
    int xcpt = get_float_exception_flags(&env->dp_status);
    int cause, enable;

    /* DPSW and DEPC stop updating while exception info is preserved. */
    if (FIELD_EX32(decnt, DECNT, EHM) && FIELD_EX32(decnt, DECNT, EHS)) {
        return;
    }

    dpsw = FIELD_DP32(dpsw, DPSW, CAUSE, 0);

    if (xcpt & float_flag_invalid) {
        SET_DPSW(V);
    }
    if (xcpt & float_flag_divbyzero) {
        SET_DPSW(Z);
    }
    if (xcpt & float_flag_overflow) {
        SET_DPSW(O);
    }
    if (xcpt & float_flag_underflow) {
        SET_DPSW(U);
    }
    if (xcpt & float_flag_inexact) {
        SET_DPSW(X);
    }
    /*
     * A denormal the DPFPU cannot handle is an unimplemented processing
     * exception, which is reported only while DDN says denormals are to be
     * treated as denormals.
     */
    if ((xcpt & (float_flag_input_denormal_flushed
                 | float_flag_output_denormal_flushed))
        && !FIELD_EX32(dpsw, DPSW, DDN)) {
        dpsw = FIELD_DP32(dpsw, DPSW, DCE, 1);
    }

    dpsw = FIELD_DP32(dpsw, DPSW, DFS,
                      FIELD_EX32(dpsw, DPSW, FLAGS) != 0);
    env->dcr[RX_DCR_DPSW] = dpsw;

    /*
     * An enabled DP exception is delivered as an interrupt request to the
     * interrupt controller rather than as a CPU exception, and suppresses
     * the write to the destination register. Neither is modelled: the cause
     * and flag bits above are correct, but the interrupt is not raised and
     * the destination is written regardless.
     */
    cause = FIELD_EX32(dpsw, DPSW, CAUSE);
    enable = FIELD_EX32(dpsw, DPSW, ENABLE);
    if (cause & enable) {
        qemu_log_mask(LOG_UNIMP,
                      "rx: enabled DPFPU exception (DPSW=0x%08x) does not "
                      "raise an interrupt\n", dpsw);
    }
}

#define FLOATOP64(op, func)                                             \
    float64 helper_##op(CPURXState *env, float64 t0, float64 t1)        \
    {                                                                   \
        float64 ret;                                                    \
        dp_begin(env);                                                  \
        ret = func(t0, t1, &env->dp_status);                            \
        update_dpsw(env);                                               \
        return ret;                                                     \
    }

FLOATOP64(dadd, float64_add)
FLOATOP64(dsub, float64_sub)
FLOATOP64(dmul, float64_mul)
FLOATOP64(ddiv, float64_div)

/*
 * dcmp<cm> src, src2 -- compare src2 against src as directed by cm and
 * record the answer in DCMR.RES.
 *
 * Mind the operand order: the manual defines every relation as src2 REL
 * src, so LT is true when the *second* operand is the smaller one. src is
 * the first encoded register (drs1) and src2 the second (drs2).
 *
 * The PSW is left alone -- C, Z, S and O are all listed as unchanged --
 * which is why MVFDR exists to move DCMR.RES into PSW.Z.
 *
 * The condition field is a mask of the relations that make RES true, which
 * is why LE (6) is LT (4) | EQ (2); it agrees with the manual on all four
 * documented conditions. UN is defined as isNaN(src) || isNaN(src2), which
 * is exactly what an unordered comparison reports. The comparison is quiet
 * because testing for unordered is one of the conditions on offer.
 */
void helper_dcmp(CPURXState *env, uint32_t cm, float64 src, float64 src2)
{
    int st;
    bool res = false;

    dp_begin(env);
    st = float64_compare_quiet(src2, src, &env->dp_status);

    if ((cm & RX_DCMP_UN) && st == float_relation_unordered) {
        res = true;
    }
    if ((cm & RX_DCMP_EQ) && st == float_relation_equal) {
        res = true;
    }
    if ((cm & RX_DCMP_LT) && st == float_relation_less) {
        res = true;
    }
    env->dcr[RX_DCR_DCMR] = deposit32(env->dcr[RX_DCR_DCMR],
                                      RX_DCMR_RES_BIT, 1, res);
    update_dpsw(env);
}

/*
 * DABS and DNEG only change the sign bit. They cannot raise an exception,
 * and are the two DP arithmetic instructions that leave the DC* cause bits
 * as they were rather than recomputing them.
 */
float64 helper_dabs(CPURXState *env, float64 t0)
{
    return float64_abs(t0);
}

float64 helper_dneg(CPURXState *env, float64 t0)
{
    return float64_chs(t0);
}

float64 helper_dsqrt(CPURXState *env, float64 t0)
{
    float64 ret;

    dp_begin(env);
    ret = float64_sqrt(t0, &env->dp_status);
    update_dpsw(env);
    return ret;
}

float64 helper_dround(CPURXState *env, float64 t0)
{
    float64 ret;

    dp_begin(env);
    ret = float64_round_to_int(t0, &env->dp_status);
    update_dpsw(env);
    return ret;
}

uint32_t helper_dtoi(CPURXState *env, float64 t0)
{
    uint32_t ret;

    dp_begin(env);
    ret = float64_to_int32_round_to_zero(t0, &env->dp_status);
    update_dpsw(env);
    return ret;
}

uint32_t helper_dtou(CPURXState *env, float64 t0)
{
    uint32_t ret;

    dp_begin(env);
    ret = float64_to_uint32_round_to_zero(t0, &env->dp_status);
    update_dpsw(env);
    return ret;
}

/* dtof narrows to single precision but is a DPFPU instruction: DPSW. */
float32 helper_dtof(CPURXState *env, float64 t0)
{
    float32 ret;

    dp_begin(env);
    ret = float64_to_float32(t0, &env->dp_status);
    update_dpsw(env);
    return ret;
}

float64 helper_itod(CPURXState *env, uint32_t t0)
{
    float64 ret;

    dp_begin(env);
    ret = int32_to_float64((int32_t)t0, &env->dp_status);
    update_dpsw(env);
    return ret;
}

float64 helper_utod(CPURXState *env, uint32_t t0)
{
    float64 ret;

    dp_begin(env);
    ret = uint32_to_float64(t0, &env->dp_status);
    update_dpsw(env);
    return ret;
}

float64 helper_ftod(CPURXState *env, float32 t0)
{
    float64 ret;

    dp_begin(env);
    ret = float32_to_float64(t0, &env->dp_status);
    update_dpsw(env);
    return ret;
}

/*
 * mvtdc rs, DCRd. Not a plain register write: the DC* cause bits in DPSW
 * clear on a written 0 and keep their value on a written 1, DFS is a
 * read-only summary, DCMR holds only RES, and DEPC is read-only.
 */
void helper_mvtdc(CPURXState *env, uint32_t reg, uint32_t val)
{
    uint32_t old, dpsw;

    switch (reg) {
    case RX_DCR_DPSW:
        old = env->dcr[RX_DCR_DPSW];
        dpsw = val & RX_DPSW_WRITE_MASK;
        dpsw = (dpsw & ~RX_DPSW_CAUSE_MASK)
             | (old & val & RX_DPSW_CAUSE_MASK);
        dpsw = FIELD_DP32(dpsw, DPSW, DFS,
                          FIELD_EX32(dpsw, DPSW, FLAGS) != 0);
        env->dcr[RX_DCR_DPSW] = dpsw;
        break;
    case RX_DCR_DCMR:
        env->dcr[RX_DCR_DCMR] = val & RX_DCMR_WRITE_MASK;
        break;
    case RX_DCR_DECNT:
        env->dcr[RX_DCR_DECNT] = val & (R_DECNT_EHM_MASK | R_DECNT_EHS_MASK);
        break;
    default:
        /* DEPC is read-only. */
        break;
    }
}

void helper_fcmp(CPURXState *env, float32 t0, float32 t1)
{
    int st;
    st = float32_compare(t0, t1, &env->fp_status);
    update_fpsw(env, 0, GETPC());
    env->psw_z = 1;
    env->psw_s = env->psw_o = 0;
    switch (st) {
    case float_relation_equal:
        env->psw_z = 0;
        break;
    case float_relation_less:
        env->psw_s = -1;
        break;
    case float_relation_unordered:
        env->psw_o = -1;
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

uint32_t helper_ftou(CPURXState *env, float32 t0)
{
    uint32_t ret;
    ret = float32_to_uint32_round_to_zero(t0, &env->fp_status);
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
    update_fpsw(env, ret, GETPC());
    return ret;
}

float32 helper_utof(CPURXState *env, uint32_t t0)
{
    float32 ret;
    ret = uint32_to_float32(t0, &env->fp_status);
    update_fpsw(env, ret, GETPC());
    return ret;
}

/* string operations */
void helper_scmpu(CPURXState *env)
{
    uint8_t tmp0, tmp1;
    if (env->regs[3] == 0) {
        return;
    }
    do {
        tmp0 = cpu_ldub_data_ra(env, env->regs[1]++, GETPC());
        tmp1 = cpu_ldub_data_ra(env, env->regs[2]++, GETPC());
        env->regs[3]--;
        if (tmp0 != tmp1 || tmp0 == '\0') {
            break;
        }
    } while (env->regs[3] != 0);
    env->psw_z = tmp0 - tmp1;
    env->psw_c = (tmp0 >= tmp1);
}

static uint32_t (* const cpu_ldufn[])(CPUArchState *env,
                                     abi_ptr ptr,
                                     uintptr_t retaddr) = {
    cpu_ldub_data_ra, cpu_lduw_le_data_ra, cpu_ldl_le_data_ra,
};

static uint32_t (* const cpu_ldfn[])(CPUArchState *env,
                                     abi_ptr ptr,
                                     uintptr_t retaddr) = {
    cpu_ldub_data_ra, cpu_lduw_le_data_ra, cpu_ldl_le_data_ra,
};

static void (* const cpu_stfn[])(CPUArchState *env,
                                 abi_ptr ptr,
                                 uint32_t val,
                                 uintptr_t retaddr) = {
    cpu_stb_data_ra, cpu_stw_le_data_ra, cpu_stl_le_data_ra,
};

void helper_sstr(CPURXState *env, uint32_t sz)
{
    tcg_debug_assert(sz < 3);
    while (env->regs[3] != 0) {
        cpu_stfn[sz](env, env->regs[1], env->regs[2], GETPC());
        env->regs[1] += 1 << sz;
        env->regs[3]--;
    }
}

#define OP_SMOVU 1
#define OP_SMOVF 0
#define OP_SMOVB 2

static void smov(uint32_t mode, CPURXState *env)
{
    uint8_t tmp;
    int dir;

    dir = (mode & OP_SMOVB) ? -1 : 1;
    while (env->regs[3] != 0) {
        tmp = cpu_ldub_data_ra(env, env->regs[2], GETPC());
        cpu_stb_data_ra(env, env->regs[1], tmp, GETPC());
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


void helper_suntil(CPURXState *env, uint32_t sz)
{
    uint32_t tmp;
    tcg_debug_assert(sz < 3);
    if (env->regs[3] == 0) {
        return;
    }
    do {
        tmp = cpu_ldufn[sz](env, env->regs[1], GETPC());
        env->regs[1] += 1 << sz;
        env->regs[3]--;
        if (tmp == env->regs[2]) {
            break;
        }
    } while (env->regs[3] != 0);
    env->psw_z = tmp - env->regs[2];
    env->psw_c = (tmp <= env->regs[2]);
}

void helper_swhile(CPURXState *env, uint32_t sz)
{
    uint32_t tmp;
    tcg_debug_assert(sz < 3);
    if (env->regs[3] == 0) {
        return;
    }
    do {
        tmp = cpu_ldufn[sz](env, env->regs[1], GETPC());
        env->regs[1] += 1 << sz;
        env->regs[3]--;
        if (tmp != env->regs[2]) {
            break;
        }
    } while (env->regs[3] != 0);
    env->psw_z = env->regs[3];
    env->psw_c = (tmp <= env->regs[2]);
}

/* accumulator operations */
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
        tmp0 = cpu_ldfn[sz](env, env->regs[1], GETPC());
        tmp1 = cpu_ldfn[sz](env, env->regs[2], GETPC());
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

void helper_satr(CPURXState *env)
{
    if (env->psw_o >> 31) {
        if ((int)env->psw_s < 0) {
            env->regs[6] = 0x00000000;
            env->regs[5] = 0x7fffffff;
            env->regs[4] = 0xffffffff;
        } else {
            env->regs[6] = 0xffffffff;
            env->regs[5] = 0x80000000;
            env->regs[4] = 0x00000000;
        }
    }
}

/* div */
uint32_t helper_div(CPURXState *env, uint32_t num, uint32_t den)
{
    uint32_t ret = num;
    if (!((num == INT_MIN && den == -1) || den == 0)) {
        ret = (int32_t)num / (int32_t)den;
        env->psw_o = 0;
    } else {
        env->psw_o = -1;
    }
    return ret;
}

uint32_t helper_divu(CPURXState *env, uint32_t num, uint32_t den)
{
    uint32_t ret = num;
    if (den != 0) {
        ret = num / den;
        env->psw_o = 0;
    } else {
        env->psw_o = -1;
    }
    return ret;
}

/* exception */
static inline G_NORETURN
void raise_exception(CPURXState *env, int index,
                     uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = index;
    cpu_loop_exit_restore(cs, retaddr);
}

G_NORETURN void helper_raise_privilege_violation(CPURXState *env)
{
    raise_exception(env, 20, GETPC());
}

G_NORETURN void helper_raise_access_fault(CPURXState *env)
{
    raise_exception(env, 21, GETPC());
}

G_NORETURN void helper_raise_illegal_instruction(CPURXState *env)
{
    raise_exception(env, 23, GETPC());
}

G_NORETURN void helper_wait(CPURXState *env)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;
    env->in_sleep = 1;
    env->psw_i = 1;
    raise_exception(env, EXCP_HLT, 0);
}

G_NORETURN void helper_rxint(CPURXState *env, uint32_t vec)
{
    raise_exception(env, 0x100 + vec, 0);
}

G_NORETURN void helper_rxbrk(CPURXState *env)
{
    raise_exception(env, 0x100, 0);
}
