/*
 *  m68k FPU helpers
 *
 *  Copyright (c) 2006-2007 CodeSourcery
 *  Written by Paul Brook
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "softfloat.h"

/*
 * Undefined offsets may be different on various FPU.
 * On 68040 they return 0.0 (floatx80_zero)
 */

static const floatx80 fpu_rom[128] = {
    [0x00] = make_floatx80_init(0x4000, 0xc90fdaa22168c235ULL),  /* Pi       */
    [0x0b] = make_floatx80_init(0x3ffd, 0x9a209a84fbcff798ULL),  /* Log10(2) */
    [0x0c] = make_floatx80_init(0x4000, 0xadf85458a2bb4a9aULL),  /* e        */
    [0x0d] = make_floatx80_init(0x3fff, 0xb8aa3b295c17f0bcULL),  /* Log2(e)  */
    [0x0e] = make_floatx80_init(0x3ffd, 0xde5bd8a937287195ULL),  /* Log10(e) */
    [0x0f] = make_floatx80_init(0x0000, 0x0000000000000000ULL),  /* Zero     */
    [0x30] = make_floatx80_init(0x3ffe, 0xb17217f7d1cf79acULL),  /* ln(2)    */
    [0x31] = make_floatx80_init(0x4000, 0x935d8dddaaa8ac17ULL),  /* ln(10)   */
    [0x32] = make_floatx80_init(0x3fff, 0x8000000000000000ULL),  /* 10^0     */
    [0x33] = make_floatx80_init(0x4002, 0xa000000000000000ULL),  /* 10^1     */
    [0x34] = make_floatx80_init(0x4005, 0xc800000000000000ULL),  /* 10^2     */
    [0x35] = make_floatx80_init(0x400c, 0x9c40000000000000ULL),  /* 10^4     */
    [0x36] = make_floatx80_init(0x4019, 0xbebc200000000000ULL),  /* 10^8     */
    [0x37] = make_floatx80_init(0x4034, 0x8e1bc9bf04000000ULL),  /* 10^16    */
    [0x38] = make_floatx80_init(0x4069, 0x9dc5ada82b70b59eULL),  /* 10^32    */
    [0x39] = make_floatx80_init(0x40d3, 0xc2781f49ffcfa6d5ULL),  /* 10^64    */
    [0x3a] = make_floatx80_init(0x41a8, 0x93ba47c980e98ce0ULL),  /* 10^128   */
    [0x3b] = make_floatx80_init(0x4351, 0xaa7eebfb9df9de8eULL),  /* 10^256   */
    [0x3c] = make_floatx80_init(0x46a3, 0xe319a0aea60e91c7ULL),  /* 10^512   */
    [0x3d] = make_floatx80_init(0x4d48, 0xc976758681750c17ULL),  /* 10^1024  */
    [0x3e] = make_floatx80_init(0x5a92, 0x9e8b3b5dc53d5de5ULL),  /* 10^2048  */
    [0x3f] = make_floatx80_init(0x7525, 0xc46052028a20979bULL),  /* 10^4096  */
};

int32_t HELPER(reds32)(CPUM68KState *env, FPReg *val)
{
    return floatx80_to_int32(val->d, &env->fp_status);
}

float32 HELPER(redf32)(CPUM68KState *env, FPReg *val)
{
    return floatx80_to_float32(val->d, &env->fp_status);
}

void HELPER(exts32)(CPUM68KState *env, FPReg *res, int32_t val)
{
    res->d = int32_to_floatx80(val, &env->fp_status);
}

void HELPER(extf32)(CPUM68KState *env, FPReg *res, float32 val)
{
    res->d = float32_to_floatx80(val, &env->fp_status);
}

void HELPER(extf64)(CPUM68KState *env, FPReg *res, float64 val)
{
    res->d = float64_to_floatx80(val, &env->fp_status);
}

float64 HELPER(redf64)(CPUM68KState *env, FPReg *val)
{
    return floatx80_to_float64(val->d, &env->fp_status);
}

void HELPER(firound)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_round_to_int(val->d, &env->fp_status);
}

static void m68k_restore_precision_mode(CPUM68KState *env)
{
    switch (env->fpcr & FPCR_PREC_MASK) {
    case FPCR_PREC_X: /* extended */
        set_floatx80_rounding_precision(floatx80_precision_x, &env->fp_status);
        break;
    case FPCR_PREC_S: /* single */
        set_floatx80_rounding_precision(floatx80_precision_s, &env->fp_status);
        break;
    case FPCR_PREC_D: /* double */
        set_floatx80_rounding_precision(floatx80_precision_d, &env->fp_status);
        break;
    case FPCR_PREC_U: /* undefined */
    default:
        break;
    }
}

static void cf_restore_precision_mode(CPUM68KState *env)
{
    if (env->fpcr & FPCR_PREC_S) { /* single */
        set_floatx80_rounding_precision(floatx80_precision_s, &env->fp_status);
    } else { /* double */
        set_floatx80_rounding_precision(floatx80_precision_d, &env->fp_status);
    }
}

static void restore_rounding_mode(CPUM68KState *env)
{
    switch (env->fpcr & FPCR_RND_MASK) {
    case FPCR_RND_N: /* round to nearest */
        set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
        break;
    case FPCR_RND_Z: /* round to zero */
        set_float_rounding_mode(float_round_to_zero, &env->fp_status);
        break;
    case FPCR_RND_M: /* round toward minus infinity */
        set_float_rounding_mode(float_round_down, &env->fp_status);
        break;
    case FPCR_RND_P: /* round toward positive infinity */
        set_float_rounding_mode(float_round_up, &env->fp_status);
        break;
    }
}

void cpu_m68k_restore_fp_status(CPUM68KState *env)
{
    if (m68k_feature(env, M68K_FEATURE_CF_FPU)) {
        cf_restore_precision_mode(env);
    } else {
        m68k_restore_precision_mode(env);
    }
    restore_rounding_mode(env);
}

void cpu_m68k_set_fpcr(CPUM68KState *env, uint32_t val)
{
    env->fpcr = val & 0xffff;
    cpu_m68k_restore_fp_status(env);
}

void HELPER(fitrunc)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    FloatRoundMode rounding_mode = get_float_rounding_mode(&env->fp_status);
    set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    res->d = floatx80_round_to_int(val->d, &env->fp_status);
    set_float_rounding_mode(rounding_mode, &env->fp_status);
}

void HELPER(set_fpcr)(CPUM68KState *env, uint32_t val)
{
    cpu_m68k_set_fpcr(env, val);
}

static void update_fpsr(CPUM68KState *env, int cc)
{
    uint32_t fpsr = env->fpsr;
    int flags = get_float_exception_flags(&env->fp_status);

    fpsr &= ~(FPSR_CC_MASK | FPSR_EXC_MASK);
    fpsr |= cc;

    if (flags) {
        set_float_exception_flags(0, &env->fp_status);

        if (flags & float_flag_invalid_snan) {
            fpsr |= FPSR_EXC_SNAN | FPSR_AEXP_IOP;
        } else if (flags & float_flag_invalid) {
            fpsr |= FPSR_EXC_OPERR | FPSR_AEXP_IOP;
        }
        if (flags & float_flag_overflow) {
            fpsr |= FPSR_EXC_OVFL | FPSR_AEXP_OVFL;
        }
        if (flags & (float_flag_underflow | float_flag_output_denormal_flushed)) {
            fpsr |= FPSR_EXC_UNFL | FPSR_AEXP_UNFL;
        }
        if (flags & float_flag_divbyzero) {
            fpsr |= FPSR_EXC_DZ | FPSR_AEXP_DZ;
        }
        if (flags & float_flag_inexact) {
            fpsr |= FPSR_EXC_INEX2 | FPSR_AEXC_INEX;
        }
    }

    /* Incorporate packed decimal real inexact conversion. */
    if (env->fpsr_inex1) {
        env->fpsr_inex1 = false;
        fpsr |= FPSR_EXC_INEX1 | FPSR_AEXC_INEX;
    }

    env->fpsr = fpsr;
}

#define PREC_BEGIN(prec)                                        \
    do {                                                        \
        FloatX80RoundPrec old =                                 \
            get_floatx80_rounding_precision(&env->fp_status);   \
        set_floatx80_rounding_precision(prec, &env->fp_status)  \

#define PREC_END()                                              \
        set_floatx80_rounding_precision(old, &env->fp_status);  \
    } while (0)

void HELPER(fsround)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    PREC_BEGIN(floatx80_precision_s);
    res->d = floatx80_round(val->d, &env->fp_status);
    PREC_END();
}

void HELPER(fdround)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    PREC_BEGIN(floatx80_precision_d);
    res->d = floatx80_round(val->d, &env->fp_status);
    PREC_END();
}

void HELPER(fsqrt)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_sqrt(val->d, &env->fp_status);
}

void HELPER(fssqrt)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    PREC_BEGIN(floatx80_precision_s);
    res->d = floatx80_sqrt(val->d, &env->fp_status);
    PREC_END();
}

void HELPER(fdsqrt)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    PREC_BEGIN(floatx80_precision_d);
    res->d = floatx80_sqrt(val->d, &env->fp_status);
    PREC_END();
}

void HELPER(fabs)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_round(floatx80_abs(val->d), &env->fp_status);
}

void HELPER(fsabs)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    PREC_BEGIN(floatx80_precision_s);
    res->d = floatx80_round(floatx80_abs(val->d), &env->fp_status);
    PREC_END();
}

void HELPER(fdabs)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    PREC_BEGIN(floatx80_precision_d);
    res->d = floatx80_round(floatx80_abs(val->d), &env->fp_status);
    PREC_END();
}

void HELPER(fneg)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_round(floatx80_chs(val->d), &env->fp_status);
}

void HELPER(fsneg)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    PREC_BEGIN(floatx80_precision_s);
    res->d = floatx80_round(floatx80_chs(val->d), &env->fp_status);
    PREC_END();
}

void HELPER(fdneg)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    PREC_BEGIN(floatx80_precision_d);
    res->d = floatx80_round(floatx80_chs(val->d), &env->fp_status);
    PREC_END();
}

void HELPER(fadd)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_add(val0->d, val1->d, &env->fp_status);
}

void HELPER(fsadd)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    PREC_BEGIN(floatx80_precision_s);
    res->d = floatx80_add(val0->d, val1->d, &env->fp_status);
    PREC_END();
}

void HELPER(fdadd)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    PREC_BEGIN(floatx80_precision_d);
    res->d = floatx80_add(val0->d, val1->d, &env->fp_status);
    PREC_END();
}

void HELPER(fsub)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_sub(val1->d, val0->d, &env->fp_status);
}

void HELPER(fssub)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    PREC_BEGIN(floatx80_precision_s);
    res->d = floatx80_sub(val1->d, val0->d, &env->fp_status);
    PREC_END();
}

void HELPER(fdsub)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    PREC_BEGIN(floatx80_precision_d);
    res->d = floatx80_sub(val1->d, val0->d, &env->fp_status);
    PREC_END();
}

void HELPER(fmul)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_mul(val0->d, val1->d, &env->fp_status);
}

void HELPER(fsmul)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    PREC_BEGIN(floatx80_precision_s);
    res->d = floatx80_mul(val0->d, val1->d, &env->fp_status);
    PREC_END();
}

void HELPER(fdmul)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    PREC_BEGIN(floatx80_precision_d);
    res->d = floatx80_mul(val0->d, val1->d, &env->fp_status);
    PREC_END();
}

void HELPER(fsglmul)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    FloatRoundMode rounding_mode = get_float_rounding_mode(&env->fp_status);
    floatx80 a, b;

    PREC_BEGIN(floatx80_precision_s);
    set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    a = floatx80_round(val0->d, &env->fp_status);
    b = floatx80_round(val1->d, &env->fp_status);
    set_float_rounding_mode(rounding_mode, &env->fp_status);
    res->d = floatx80_mul(a, b, &env->fp_status);
    PREC_END();
}

void HELPER(fdiv)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_div(val1->d, val0->d, &env->fp_status);
}

void HELPER(fsdiv)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    PREC_BEGIN(floatx80_precision_s);
    res->d = floatx80_div(val1->d, val0->d, &env->fp_status);
    PREC_END();
}

void HELPER(fddiv)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    PREC_BEGIN(floatx80_precision_d);
    res->d = floatx80_div(val1->d, val0->d, &env->fp_status);
    PREC_END();
}

void HELPER(fsgldiv)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    FloatRoundMode rounding_mode = get_float_rounding_mode(&env->fp_status);
    floatx80 a, b;

    PREC_BEGIN(floatx80_precision_s);
    set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    a = floatx80_round(val1->d, &env->fp_status);
    b = floatx80_round(val0->d, &env->fp_status);
    set_float_rounding_mode(rounding_mode, &env->fp_status);
    res->d = floatx80_div(a, b, &env->fp_status);
    PREC_END();
}

static int float_comp_to_cc(FloatRelation float_compare)
{
    switch (float_compare) {
    case float_relation_equal:
        return FPSR_CC_Z;
    case float_relation_less:
        return FPSR_CC_N;
    case float_relation_unordered:
        return FPSR_CC_A;
    case float_relation_greater:
        return 0;
    default:
        g_assert_not_reached();
    }
}

void HELPER(fcmp)(CPUM68KState *env, FPReg *val0, FPReg *val1)
{
    FloatRelation float_compare;

    float_compare = floatx80_compare(val1->d, val0->d, &env->fp_status);
    update_fpsr(env, float_comp_to_cc(float_compare));
}

void HELPER(ftst)(CPUM68KState *env, FPReg *val)
{
    int cc = 0;

    if (floatx80_is_neg(val->d)) {
        cc |= FPSR_CC_N;
    }

    if (floatx80_is_any_nan(val->d)) {
        cc |= FPSR_CC_A;
    } else if (floatx80_is_infinity(val->d, &env->fp_status)) {
        cc |= FPSR_CC_I;
    } else if (floatx80_is_zero(val->d)) {
        cc |= FPSR_CC_Z;
    }
    update_fpsr(env, cc);
}

void HELPER(fconst)(CPUM68KState *env, FPReg *val, uint32_t offset)
{
    val->d = fpu_rom[offset];
    HELPER(ftst)(env, val);
}

typedef int (*float_access)(CPUM68KState *env, uint32_t addr, FPReg *fp,
                            uintptr_t ra);

static uint32_t fmovem_predec(CPUM68KState *env, uint32_t addr, uint32_t mask,
                              float_access access_fn)
{
    uintptr_t ra = GETPC();
    int i, size;

    for (i = 7; i >= 0; i--, mask <<= 1) {
        if (mask & 0x80) {
            size = access_fn(env, addr, &env->fregs[i], ra);
            if ((mask & 0xff) != 0x80) {
                addr -= size;
            }
        }
    }

    return addr;
}

static uint32_t fmovem_postinc(CPUM68KState *env, uint32_t addr, uint32_t mask,
                               float_access access_fn)
{
    uintptr_t ra = GETPC();
    int i, size;

    for (i = 0; i < 8; i++, mask <<= 1) {
        if (mask & 0x80) {
            size = access_fn(env, addr, &env->fregs[i], ra);
            addr += size;
        }
    }

    return addr;
}

static int cpu_ld_floatx80_ra(CPUM68KState *env, uint32_t addr, FPReg *fp,
                              uintptr_t ra)
{
    uint32_t high;
    uint64_t low;

    high = cpu_ldl_data_ra(env, addr, ra);
    low = cpu_ldq_data_ra(env, addr + 4, ra);

    fp->l.upper = high >> 16;
    fp->l.lower = low;

    return 12;
}

static int cpu_st_floatx80_ra(CPUM68KState *env, uint32_t addr, FPReg *fp,
                               uintptr_t ra)
{
    cpu_stl_data_ra(env, addr, fp->l.upper << 16, ra);
    cpu_stq_data_ra(env, addr + 4, fp->l.lower, ra);

    return 12;
}

static int cpu_ld_float64_ra(CPUM68KState *env, uint32_t addr, FPReg *fp,
                             uintptr_t ra)
{
    uint64_t val;

    val = cpu_ldq_data_ra(env, addr, ra);
    fp->d = float64_to_floatx80(*(float64 *)&val, &env->fp_status);

    return 8;
}

static int cpu_st_float64_ra(CPUM68KState *env, uint32_t addr, FPReg *fp,
                             uintptr_t ra)
{
    float64 val;

    val = floatx80_to_float64(fp->d, &env->fp_status);
    cpu_stq_data_ra(env, addr, *(uint64_t *)&val, ra);

    return 8;
}

uint32_t HELPER(fmovemx_st_predec)(CPUM68KState *env, uint32_t addr,
                                   uint32_t mask)
{
    return fmovem_predec(env, addr, mask, cpu_st_floatx80_ra);
}

uint32_t HELPER(fmovemx_st_postinc)(CPUM68KState *env, uint32_t addr,
                                    uint32_t mask)
{
    return fmovem_postinc(env, addr, mask, cpu_st_floatx80_ra);
}

uint32_t HELPER(fmovemx_ld_postinc)(CPUM68KState *env, uint32_t addr,
                                    uint32_t mask)
{
    return fmovem_postinc(env, addr, mask, cpu_ld_floatx80_ra);
}

uint32_t HELPER(fmovemd_st_predec)(CPUM68KState *env, uint32_t addr,
                                   uint32_t mask)
{
    return fmovem_predec(env, addr, mask, cpu_st_float64_ra);
}

uint32_t HELPER(fmovemd_st_postinc)(CPUM68KState *env, uint32_t addr,
                                    uint32_t mask)
{
    return fmovem_postinc(env, addr, mask, cpu_st_float64_ra);
}

uint32_t HELPER(fmovemd_ld_postinc)(CPUM68KState *env, uint32_t addr,
                                    uint32_t mask)
{
    return fmovem_postinc(env, addr, mask, cpu_ld_float64_ra);
}

static void make_quotient(CPUM68KState *env, int sign, uint32_t quotient)
{
    quotient = (sign << 7) | (quotient & 0x7f);
    env->fpsr = (env->fpsr & ~FPSR_QT_MASK) | (quotient << FPSR_QT_SHIFT);
}

void HELPER(fmod)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    uint64_t quotient;
    int sign = extractFloatx80Sign(val1->d) ^ extractFloatx80Sign(val0->d);

    res->d = floatx80_modrem(val1->d, val0->d, true, &quotient,
                             &env->fp_status);

    if (floatx80_is_any_nan(res->d)) {
        return;
    }

    make_quotient(env, sign, quotient);
}

void HELPER(frem)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    FPReg fp_quot;
    floatx80 fp_rem;

    fp_rem = floatx80_rem(val1->d, val0->d, &env->fp_status);
    if (!floatx80_is_any_nan(fp_rem)) {
        /* Use local temporary fp_status to set different rounding mode */
        float_status fp_status = env->fp_status;
        uint32_t quotient;
        int sign;

        /* Calculate quotient directly using round to nearest mode */
        set_float_rounding_mode(float_round_nearest_even, &fp_status);
        fp_quot.d = floatx80_div(val1->d, val0->d, &fp_status);

        sign = extractFloatx80Sign(fp_quot.d);
        quotient = floatx80_to_int32(floatx80_abs(fp_quot.d), &env->fp_status);
        make_quotient(env, sign, quotient);
    }

    res->d = fp_rem;
}

void HELPER(fgetexp)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_getexp(val->d, &env->fp_status);
}

void HELPER(fgetman)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_getman(val->d, &env->fp_status);
}

void HELPER(fscale)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_scale(val1->d, val0->d, &env->fp_status);
}

void HELPER(flognp1)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_lognp1(val->d, &env->fp_status);
}

void HELPER(flogn)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_logn(val->d, &env->fp_status);
}

void HELPER(flog10)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_log10(val->d, &env->fp_status);
}

void HELPER(flog2)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_log2(val->d, &env->fp_status);
}

void HELPER(fetox)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_etox(val->d, &env->fp_status);
}

void HELPER(ftwotox)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_twotox(val->d, &env->fp_status);
}

void HELPER(ftentox)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_tentox(val->d, &env->fp_status);
}

void HELPER(ftan)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_tan(val->d, &env->fp_status);
}

void HELPER(fsin)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_sin(val->d, &env->fp_status);
}

void HELPER(fcos)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_cos(val->d, &env->fp_status);
}

void HELPER(fsincos)(CPUM68KState *env, FPReg *res0, FPReg *res1, FPReg *val)
{
    floatx80 a = val->d;
    /*
     * If res0 and res1 specify the same floating-point data register,
     * the sine result is stored in the register, and the cosine
     * result is discarded.
     */
    res1->d = floatx80_cos(a, &env->fp_status);
    res0->d = floatx80_sin(a, &env->fp_status);
}

void HELPER(fatan)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_atan(val->d, &env->fp_status);
}

void HELPER(fasin)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_asin(val->d, &env->fp_status);
}

void HELPER(facos)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_acos(val->d, &env->fp_status);
}

void HELPER(fatanh)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_atanh(val->d, &env->fp_status);
}

void HELPER(fetoxm1)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_etoxm1(val->d, &env->fp_status);
}

void HELPER(ftanh)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_tanh(val->d, &env->fp_status);
}

void HELPER(fsinh)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_sinh(val->d, &env->fp_status);
}

void HELPER(fcosh)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_cosh(val->d, &env->fp_status);
}

static const floatx80 floatx80_pow10[] = {
#include "floatx80-pow10.c.inc"
};

static floatx80 floatx80_scale10i(floatx80 x, int e, float_status *status)
{
    if (e == 0) {
        return x;
    }
    if (e < 0) {
        e = -e;
        assert(e < ARRAY_SIZE(floatx80_pow10));
        return floatx80_div(x, floatx80_pow10[e], status);
    } else if (e < ARRAY_SIZE(floatx80_pow10)) {
        return floatx80_mul(x, floatx80_pow10[e], status);
    } else {
        /*
         * Because of denormals, we may need to scale up more than
         * is possible with one multiplication.  Do the best we can.
         */
        int e0 = ARRAY_SIZE(floatx80_pow10) - 1;
        int e1 = e - e0;
        x = floatx80_mul(x, floatx80_pow10[e0], status);
        return floatx80_mul(x, floatx80_pow10[e1], status);
    }
}

void HELPER(load_pdr_to_fx80)(CPUM68KState *env, FPReg *res, target_ulong addr)
{
    float_status status;
    uint64_t lo;
    uint32_t hi;
    int64_t mant;
    int exp;
    floatx80 t;

    hi = cpu_ldl_be_data_ra(env, addr, GETPC());
    lo = cpu_ldq_be_data_ra(env, addr + 4, GETPC());

    if (unlikely((hi & 0x7fff0000) == 0x7fff0000)) {
        /* NaN or Inf */
        res->l.lower = lo;
        res->l.upper = hi >> 16;
        return;
    }

    /* Initialize mant with the integer digit. */
    mant = hi & 0xf;
    if (!mant && !lo) {
        /* +/- 0, regardless of exponent. */
        res->l.lower = 0;
        res->l.upper = (hi >> 16) & 0x8000;
        return;
    }

    /*
     * Accumulate the 16 decimal fraction digits into mant.
     * With 17 decimal digits, the maximum value is 10**17 - 1,
     * which is less than 2**57.
     */
    for (int i = 60; i >= 0; i -= 4) {
        /*
         * From 1.6.6 Data Format and Type Summary:
         * The fpu does not detect non-decimal digits in any of the exponent,
         * integer, or fraction digits.  These non-decimal digits are converted
         * in the same manner as decimal digits; the result is probably useless
         * although it is repeatable.
         */
        mant = mant * 10 + ((lo >> i) & 0xf);
    }

    /* Apply the mantissa sign. */
    if (hi & 0x80000000) {
        mant = -mant;
    }

    /* Convert the 3 digit decimal exponent to binary. */
    exp = ((hi >> 24) & 0xf)
        + ((hi >> 20) & 0xf) * 10
        + ((hi >> 16) & 0xf) * 100;

    /* Apply the exponent sign. */
    if (hi & 0x40000000) {
        exp = -exp;
    }

    /*
     * Our representation of mant is integral, whereas the decimal point
     * belongs between the integer and fractional components.
     * Adjust the exponent to compensate.
     */
    exp -= 16;

    status = env->fp_status;
    set_floatx80_rounding_precision(floatx80_precision_x, &status);
    set_float_exception_flags(0, &status);

    /* Convert mantissa and apply exponent. */
    t = int64_to_floatx80(mant, &status),
    res->d = floatx80_scale10i(t, exp, &status);

    /*
     * The only exception bit that is relevant is inexact.
     * All of the rest will be collected from the result.
     */
    env->fpsr_inex1 = get_float_exception_flags(&status) & float_flag_inexact;
}

#define KFACTOR_MIN  1
#define KFACTOR_MAX  17

void HELPER(store_fx80_to_pdr)(CPUM68KState *env, target_ulong addr,
                               FPReg *srcp, int kfactor)
{
    /* 10**0 through 10**17 */
    static const int64_t i64_pow10[KFACTOR_MAX + 1] = {
        1ll,
        10ll,
        100ll,
        1000ll,
        10000ll,
        100000ll,
        1000000ll,
        10000000ll,
        100000000ll,
        1000000000ll,
        10000000000ll,
        100000000000ll,
        1000000000000ll,
        10000000000000ll,
        100000000000000ll,
        1000000000000000ll,
        10000000000000000ll,
        100000000000000000ll,
    };

    float_status status;
    floatx80 x = srcp->d;
    int len, exp2, exp10;
    uint64_t res_lo;
    uint32_t res_hi;
    int64_t y;

    res_lo = x.low;
    exp2 = x.high & 0x7fff;
    if (unlikely(exp2 == 0x7fff)) {
        /* NaN and Inf */
        res_hi = (uint32_t)x.high << 16;
        goto done;
    }

    /* Copy the sign bit to the output, and then x = abs(x). */
    res_hi = (x.high & 0x8000u) << 16;
    x.high &= 0x7fff;

    if (exp2 == 0) {
        if (res_lo == 0) {
            /* +/- 0 */
            goto done;
        }
        /* denormal */
        exp2 = -0x3fff - clz64(res_lo);
    } else {
        exp2 -= 0x3fff;
    }

    status = env->fp_status;
    set_floatx80_rounding_precision(floatx80_precision_x, &status);

    /*
     * Begin with an approximation of log2(x) via the base 2 exponent.
     * Scale, such that the value is integral in the number of digits
     * we wish to extract.
     */
    exp10 = (exp2 * 30102) / 100000;
    while (1) {
        floatx80 t;

        /* kfactor controls the number of output digits */
        if (kfactor <= 0) {
            /* kfactor is number of digits right of the decimal point. */
            len = exp10 - kfactor;
        } else {
            /* kfactor is number of significant digits */
            len = kfactor;
        }
        len = MIN(MAX(len, KFACTOR_MIN), KFACTOR_MAX);

        /*
         * Scale, so that we have the requested number of digits
         * left of the decimal point.  Convert to integer, which
         * handles the rounding (and may force adjustment of exp10).
         */
        set_float_exception_flags(0, &status);
        t = floatx80_scale10i(x, len - 1 - exp10, &status);
        y = floatx80_to_int64(t, &status);
        if (y < i64_pow10[len - 1]) {
            exp10--;
        } else if (y < i64_pow10[len]) {
            break;
        } else {
            exp10++;
        }
    }

    /* The only exception bit that is relevant is inexact. */
    env->fpsr_inex1 = get_float_exception_flags(&status) & float_flag_inexact;

    /* Output the mantissa. */
    res_hi |= y / i64_pow10[len - 1];
    res_lo = 0;
    for (int i = 1; i < len; ++i) {
        int64_t d = (y / i64_pow10[len - 1 - i]) % 10;
        res_lo |= d << (64 - i * 4);
    }

    /* Output the exponent. */
    if (exp10 < 0) {
        res_hi |= 0x40000000;
        exp10 = -exp10;
    }
    for (int i = 24; exp10; i -= 4, exp10 /= 10) {
        res_hi |= (exp10 % 10) << i;
    }

 done:
    cpu_stl_be_data_ra(env, addr, res_hi, GETPC());
    cpu_stq_be_data_ra(env, addr + 4, res_lo, GETPC());
}
