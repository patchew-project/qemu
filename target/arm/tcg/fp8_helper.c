/*
 * AArch64 FP8 Operations
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"
#include "fpu/softfloat-parts.h"
#include "helper-fp8.h"
#include "vec_internal.h"

#define HELPER_H "tcg/helper-fp8-defs.h"
#include "exec/helper-info.c.inc"

typedef enum FPMRType {
    OFP8_E5M2 = 0,
    OFP8_E4M3 = 1,
    Unsupp2 = 2,
    Unsupp3 = 3,
    Unsupp4 = 4,
    Unsupp5 = 5,
    Unsupp6 = 6,
    Unsupp7 = 7,
} FPMRType;

typedef struct FP8Context {
    float_status stat;
    ARMFPStatusFlavour fpst;
    FPMRType f8fmt;
    int scale;
    bool high;
} FP8Context;

static FP8Context fp8_start(CPUARMState *env, uint32_t desc,
                            FPMRType f8fmt, int scale)
{
    ARMFPStatusFlavour fpst = extract32(desc, SIMD_DATA_SHIFT + 2, 4);

    FP8Context ret = {
        .stat = env->vfp.fp_status[fpst],
        .fpst = fpst,
        .f8fmt = f8fmt,
        .scale = scale,
        .high = extract32(desc, SIMD_DATA_SHIFT + 1, 1),
    };

    set_flush_to_zero(0, &ret.stat);
    set_flush_inputs_to_zero(0, &ret.stat);
    set_default_nan_mode(true, &ret.stat);
    set_float_rounding_mode(float_round_nearest_even, &ret.stat);

    return ret;
}

static void fp8_finish(CPUARMState *env, FP8Context *c)
{
    int new_flags = get_float_exception_flags(&c->stat);

    new_flags &= ~float_flag_input_denormal_used;
    float_raise(new_flags, &env->vfp.fp_status[c->fpst]);
}

static FP8Context fp8_src_start(CPUARMState *env, uint32_t desc, int scale_mask)
{
    bool issrc2 = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t fpmr = env->vfp.fpmr;
    FPMRType f8fmt = (issrc2
                      ? FIELD_EX64(fpmr, FPMR, F8S2)
                      : FIELD_EX64(fpmr, FPMR, F8S1));
    int scale;

    scale = fpmr >> (issrc2 ? R_FPMR_LSCALE2_SHIFT : R_FPMR_LSCALE_SHIFT);
    scale = -(scale & scale_mask);

    return fp8_start(env, desc, f8fmt, scale);
}

static FP8Context fp8_dst_start(CPUARMState *env, uint32_t desc)
{
    uint64_t fpmr = env->vfp.fpmr;
    FPMRType f8fmt = FIELD_EX64(fpmr, FPMR, F8D);
    int scale = FIELD_SEX64(fpmr, FPMR, NSCALE);

    return fp8_start(env, desc, f8fmt, scale);
}

/*
 * Invalid input format is treated as snan, then the conversion operation
 * converts to default nan and raises invalid.
 */
static void bfloat16_invalid_input(bfloat16 *d, size_t nelem, float_status *s)
{
    bfloat16 dnan = bfloat16_default_nan(s);

    for (size_t i = 0; i < nelem; ++i) {
        d[i] = dnan;
    }
    float_raise(float_flag_invalid | float_flag_invalid_snan, s);
}

static void float16_invalid_input(float16 *d, size_t nelem, float_status *s)
{
    float16 dnan = float16_default_nan(s);

    for (size_t i = 0; i < nelem; ++i) {
        d[i] = dnan;
    }
    float_raise(float_flag_invalid | float_flag_invalid_snan, s);
}

/* Invalid output format writes -1 and raises invalid.  */
static void float8_invalid_output(uint8_t *d, size_t nelem, float_status *s)
{
    memset(d, -1, nelem);
    float_raise(float_flag_invalid, s);
}

static bfloat16 fcvt_fp8e4m3_to_b16(float8_e4m3 x, int scale, float_status *s)
{
    FloatParts64 p = float8_e4m3_unpack_canonical(x, s);
    p = parts64_scalbn(&p, scale, s);
    return bfloat16_round_pack_canonical(&p, s);
}

static bfloat16 fcvt_fp8e5m2_to_b16(float8_e5m2 x, int scale, float_status *s)
{
    FloatParts64 p = float8_e5m2_unpack_canonical(x, s);
    p = parts64_scalbn(&p, scale, s);
    return bfloat16_round_pack_canonical(&p, s);
}

static float16 fcvt_fp8e4m3_to_f16(float8_e4m3 x, int scale, float_status *s)
{
    FloatParts64 p = float8_e4m3_unpack_canonical(x, s);
    p = parts64_scalbn(&p, scale, s);
    return float16_round_pack_canonical(&p, s);
}

static float16 fcvt_fp8e5m2_to_f16(float8_e5m2 x, int scale, float_status *s)
{
    FloatParts64 p = float8_e5m2_unpack_canonical(x, s);
    p = parts64_scalbn(&p, scale, s);
    return float16_round_pack_canonical(&p, s);
}

static float8_e4m3 fcvt_b16_to_fp8e4m3(bfloat16 x, int scale,
                                       bool saturate, float_status *s)
{
    FloatParts64 p = bfloat16_unpack_canonical(x, s);

    p = parts64_scalbn(&p, scale, s);
    /*
     * Saturating Inf -> Max handled in uncanon_e4m3_overflow
     * because there is no infinity encoding.
     */
    return float8_e4m3_round_pack_canonical(&p, s, saturate);
}

/*
 * Because e5m2 has an infinity encoding, we need to handle
 * conversion of Inf -> Max manually.  This will be converted
 * to the actual maximum value during rounding.
 */
static void scalbn_to_fp8e5m2(FloatParts64 *p, int scale,
                              bool saturate, float_status *s)
{
    if (unlikely(p->cls == float_class_inf)) {
        if (saturate) {
            p->cls = float_class_normal;
            p->exp = INT_MAX;
            p->frac = -1;
        }
    } else {
        *p = parts64_scalbn(p, scale, s);
    }
}

static float8_e5m2 fcvt_b16_to_fp8e5m2(bfloat16 x, int scale,
                                       bool saturate, float_status *s)
{
    FloatParts64 p = bfloat16_unpack_canonical(x, s);

    scalbn_to_fp8e5m2(&p, scale, saturate, s);
    return float8_e5m2_round_pack_canonical(&p, s, saturate);
}

static float8_e4m3 fcvt_f16_to_fp8e4m3(float16 x, int scale,
                                       bool saturate, float_status *s)
{
    FloatParts64 p = float16_unpack_canonical(x, s);

    p = parts64_scalbn(&p, scale, s);
    return float8_e4m3_round_pack_canonical(&p, s, saturate);
}

static float8_e5m2 fcvt_f16_to_fp8e5m2(float16 x, int scale,
                                       bool saturate, float_status *s)
{
    FloatParts64 p = float16_unpack_canonical(x, s);

    scalbn_to_fp8e5m2(&p, scale, saturate, s);
    return float8_e5m2_round_pack_canonical(&p, s, saturate);
}

static float8_e4m3 fcvt_f32_to_fp8e4m3(float32 x, int scale,
                                       bool saturate, float_status *s)
{
    FloatParts64 p = float32_unpack_canonical(x, s);

    p = parts64_scalbn(&p, scale, s);
    return float8_e4m3_round_pack_canonical(&p, s, saturate);
}

static float8_e5m2 fcvt_f32_to_fp8e5m2(float32 x, int scale,
                                       bool saturate, float_status *s)
{
    FloatParts64 p = float32_unpack_canonical(x, s);

    scalbn_to_fp8e5m2(&p, scale, saturate, s);
    return float8_e5m2_round_pack_canonical(&p, s, saturate);
}

void HELPER(advsimd_bfcvtl)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0x3f);
    uint8_t *n = vn, scratch[16];
    bfloat16 *d = vd;

    if (vd == vn) {
        n = memcpy(scratch, vn, 16);
    }
    n += ctx.high * 8;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (int i = 0; i < 8; ++i) {
            d[H2(i)] = fcvt_fp8e5m2_to_b16(n[H1(i)], ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (int i = 0; i < 8; ++i) {
            d[H2(i)] = fcvt_fp8e4m3_to_b16(n[H1(i)], ctx.scale, &ctx.stat);
        }
        break;
    default:
        bfloat16_invalid_input(d, 8, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
    clear_tail(vd, 16, simd_maxsz(desc));
}

void HELPER(advsimd_fcvtl_hb)(void *vd, void *vn,
                              CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0xf);
    uint8_t *n = vn, scratch[16];
    float16 *d = vd;

    if (vd == vn) {
        n = memcpy(scratch, vn, 16);
    }
    n += ctx.high * 8;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (int i = 0; i < 8; ++i) {
            d[H2(i)] = fcvt_fp8e5m2_to_f16(n[H1(i)], ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (int i = 0; i < 8; ++i) {
            d[H2(i)] = fcvt_fp8e4m3_to_f16(n[H1(i)], ctx.scale, &ctx.stat);
        }
        break;
    default:
        float16_invalid_input(d, 8, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
    clear_tail(vd, 16, simd_maxsz(desc));
}

void HELPER(sve2_bfcvt)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0x3f);
    uint8_t *n = vn;
    uint16_t *d = vd;
    size_t nelem = simd_oprsz(desc) / 2;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e = n[H1(2 * i + ctx.high)];
            d[H2(i)] = fcvt_fp8e5m2_to_b16(e, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(2 * i + ctx.high)];
            d[H2(i)] = fcvt_fp8e4m3_to_b16(e, ctx.scale, &ctx.stat);
        }
        break;
    default:
        bfloat16_invalid_input(d, nelem, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sve2_fcvt_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0xf);
    uint8_t *n = vn;
    uint16_t *d = vd;
    size_t nelem = simd_oprsz(desc) / 2;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e = n[H1(2 * i + ctx.high)];
            d[H2(i)] = fcvt_fp8e5m2_to_f16(e, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(2 * i + ctx.high)];
            d[H2(i)] = fcvt_fp8e4m3_to_f16(e, ctx.scale, &ctx.stat);
        }
        break;
    default:
        float16_invalid_input(d, nelem, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sme2_bfcvt_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0x3f);
    uint8_t *n = vn;
    uint16_t *d0 = vd;
    uint16_t *d1 = vd + sizeof(ARMVectorReg);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    ARMVectorReg scratch;

    if (vectors_overlap(vd, 2, vn, 1)) {
        n = memcpy(&scratch, vn, oprsz);
    }

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e = n[H1(i)];
            d0[H2(i)] = fcvt_fp8e5m2_to_b16(e, ctx.scale, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e = n[H1(i) + nelem];
            d1[H2(i)] = fcvt_fp8e5m2_to_b16(e, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(i)];
            d0[H2(i)] = fcvt_fp8e4m3_to_b16(e, ctx.scale, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(i) + nelem];
            d1[H2(i)] = fcvt_fp8e4m3_to_b16(e, ctx.scale, &ctx.stat);
        }
        break;
    default:
        bfloat16_invalid_input(d0, nelem, &ctx.stat);
        memcpy(d1, d0, oprsz);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sme2_fcvt_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0xf);
    uint8_t *n = vn;
    uint16_t *d0 = vd;
    uint16_t *d1 = vd + sizeof(ARMVectorReg);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    ARMVectorReg scratch;

    if (vectors_overlap(vd, 2, vn, 1)) {
        n = memcpy(&scratch, vn, oprsz);
    }

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e = n[H1(i)];
            d0[H2(i)] = fcvt_fp8e5m2_to_f16(e, ctx.scale, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e = n[H1(i) + nelem];
            d1[H2(i)] = fcvt_fp8e5m2_to_f16(e, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(i)];
            d0[H2(i)] = fcvt_fp8e4m3_to_f16(e, ctx.scale, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(i) + nelem];
            d1[H2(i)] = fcvt_fp8e4m3_to_f16(e, ctx.scale, &ctx.stat);
        }
        break;
    default:
        float16_invalid_input(d0, nelem, &ctx.stat);
        memcpy(d1, d0, oprsz);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sme2_bfcvtl_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0x3f);
    uint8_t *n = vn;
    uint16_t *d0 = vd;
    uint16_t *d1 = vd + sizeof(ARMVectorReg);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e0 = n[H1(2 * i + 0)];
            float8_e5m2 e1 = n[H1(2 * i + 1)];
            d0[H2(i)] = fcvt_fp8e5m2_to_b16(e0, ctx.scale, &ctx.stat);
            d1[H2(i)] = fcvt_fp8e5m2_to_b16(e1, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e0 = n[H1(2 * i + 0)];
            float8_e4m3 e1 = n[H1(2 * i + 1)];
            d0[H2(i)] = fcvt_fp8e4m3_to_b16(e0, ctx.scale, &ctx.stat);
            d1[H2(i)] = fcvt_fp8e4m3_to_b16(e1, ctx.scale, &ctx.stat);
        }
        break;
    default:
        bfloat16_invalid_input(d0, nelem, &ctx.stat);
        memcpy(d1, d0, oprsz);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sme2_fcvtl_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0xf);
    uint8_t *n = vn;
    uint16_t *d0 = vd;
    uint16_t *d1 = vd + sizeof(ARMVectorReg);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e0 = n[H1(2 * i + 0)];
            float8_e5m2 e1 = n[H1(2 * i + 1)];
            d0[H2(i)] = fcvt_fp8e5m2_to_f16(e0, ctx.scale, &ctx.stat);
            d1[H2(i)] = fcvt_fp8e5m2_to_f16(e1, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e0 = n[H1(2 * i + 0)];
            float8_e4m3 e1 = n[H1(2 * i + 1)];
            d0[H2(i)] = fcvt_fp8e4m3_to_f16(e0, ctx.scale, &ctx.stat);
            d1[H2(i)] = fcvt_fp8e4m3_to_f16(e1, ctx.scale, &ctx.stat);
        }
        break;
    default:
        float16_invalid_input(d0, nelem, &ctx.stat);
        memcpy(d1, d0, oprsz);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sve2_bfcvtn_bh)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc);
    uint16_t *n0 = vn;
    uint16_t *n1 = vn + sizeof(ARMVectorReg);
    uint8_t *d = vd;
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            bfloat16 e0 = n0[H2(i)];
            bfloat16 e1 = n1[H2(i)];
            d[H1(2 * i + 0)] =
                fcvt_b16_to_fp8e5m2(e0, ctx.scale, osc, &ctx.stat);
            d[H1(2 * i + 1)] =
                fcvt_b16_to_fp8e5m2(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            bfloat16 e0 = n0[H2(i)];
            bfloat16 e1 = n1[H2(i)];
            d[H1(2 * i + 0)] =
                fcvt_b16_to_fp8e4m3(e0, ctx.scale, osc, &ctx.stat);
            d[H1(2 * i + 1)] =
                fcvt_b16_to_fp8e4m3(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        float8_invalid_output(d, oprsz, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(gvec_fcvt_bh)(void *vd, void *vn, void *vm,
                          CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc);
    uint16_t *n = vn;
    uint16_t *m = vm;
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    ARMVectorReg scratch;

    if (vd == vm) {
        m = memcpy(&scratch, vm, oprsz);
    }

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float16 e = n[H2(i)];
            d[H1(i)] = fcvt_f16_to_fp8e5m2(e, ctx.scale, osc, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float16 e = m[H2(i)];
            d[H1(i) + nelem] =
                fcvt_f16_to_fp8e5m2(e, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float16 e = n[H2(i)];
            d[H1(i)] = fcvt_f16_to_fp8e4m3(e, ctx.scale, osc, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float16 e = m[H2(i)];
            d[H1(i) + nelem] =
                fcvt_f16_to_fp8e4m3(e, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        float8_invalid_output(d, oprsz, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
    clear_tail(vd, oprsz, simd_maxsz(desc));
}

void HELPER(sve2_fcvtn_bh)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc);
    uint16_t *n0 = vn;
    uint16_t *n1 = vn + sizeof(ARMVectorReg);
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float16 e0 = n0[H2(i)];
            float16 e1 = n1[H2(i)];
            d[H1(2 * i + 0)] =
                fcvt_f16_to_fp8e5m2(e0, ctx.scale, osc, &ctx.stat);
            d[H1(2 * i + 1)] =
                fcvt_f16_to_fp8e5m2(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float16 e0 = n0[H2(i)];
            float16 e1 = n1[H2(i)];
            d[H1(2 * i + 0)] =
                fcvt_f16_to_fp8e4m3(e0, ctx.scale, osc, &ctx.stat);
            d[H1(2 * i + 1)] =
                fcvt_f16_to_fp8e4m3(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        float8_invalid_output(d, oprsz, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(advsimd_fcvt_bs)(void *vd, void *vn, void *vm,
                             CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc);
    uint32_t *n = vn, *m = vm, scratch[4];
    uint8_t *d = vd + 8 * ctx.high;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);

    if (vd == vm) {
        m = memcpy(scratch, vm, 16);
    }

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < 4; ++i) {
            float32 e = n[H4(i)];
            d[H1(i + 0)] = fcvt_f32_to_fp8e5m2(e, ctx.scale, osc, &ctx.stat);
        }
        for (size_t i = 0; i < 4; ++i) {
            float32 e = m[H4(i)];
            d[H1(i + 4)] = fcvt_f32_to_fp8e5m2(e, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < 4; ++i) {
            float32 e = n[H4(i)];
            d[H1(i + 0)] = fcvt_f32_to_fp8e4m3(e, ctx.scale, osc, &ctx.stat);
        }
        for (size_t i = 0; i < 4; ++i) {
            float32 e = m[H4(i)];
            d[H1(i + 4)] = fcvt_f32_to_fp8e4m3(e, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        float8_invalid_output(d, 8, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
    clear_tail(vd, ctx.high ? 16 : 8, simd_maxsz(desc));
}

void HELPER(sve2_fcvtnb_bs)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc);
    uint32_t *n0 = vn;
    uint32_t *n1 = vn + sizeof(ARMVectorReg);
    uint16_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 4;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float32 e0 = n0[H2(i)];
            float32 e1 = n1[H2(i)];
            d[H2(2 * i + 0)] =
                fcvt_f32_to_fp8e5m2(e0, ctx.scale, osc, &ctx.stat);
            d[H2(2 * i + 1)] =
                fcvt_f32_to_fp8e5m2(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float32 e0 = n0[H2(i)];
            float32 e1 = n1[H2(i)];
            d[H2(2 * i + 0)] =
                fcvt_f32_to_fp8e4m3(e0, ctx.scale, osc, &ctx.stat);
            d[H2(2 * i + 1)] =
                fcvt_f32_to_fp8e4m3(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        /* -1 in each even field, 0 in each odd field. */
        for (size_t i = 0; i < oprsz; i += 8) {
            *(uint64_t *)(vd + i) = 0x00ff00ff00ff00ffull;
        }
        float_raise(float_flag_invalid, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sve2_fcvtnt_bs)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc);
    uint32_t *n0 = vn;
    uint32_t *n1 = vn + sizeof(ARMVectorReg);
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 4;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float32 e0 = n0[H2(i)];
            float32 e1 = n1[H2(i)];
            d[H1(4 * i + 1)] =
                fcvt_f32_to_fp8e5m2(e0, ctx.scale, osc, &ctx.stat);
            d[H1(4 * i + 3)] =
                fcvt_f32_to_fp8e5m2(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float32 e0 = n0[H2(i)];
            float32 e1 = n1[H2(i)];
            d[H1(4 * i + 1)] =
                fcvt_f32_to_fp8e4m3(e0, ctx.scale, osc, &ctx.stat);
            d[H1(4 * i + 3)] =
                fcvt_f32_to_fp8e4m3(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        /* -1 in each odd field, even fields unchanged. */
        for (size_t i = 0; i < oprsz; i += 8) {
            *(uint64_t *)(vd + i) |= 0xff00ff00ff00ff00ull;
        }
        float_raise(float_flag_invalid, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sme2_fcvt_bs)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    ARMVectorReg scratch[4];
    FP8Context ctx = fp8_dst_start(env, desc);
    uint32_t *n = vn;
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 4;
    size_t stride = sizeof(ARMVectorReg) / 4;

    if (vectors_overlap(vd, 1, vn, 4)) {
        n = memcpy(scratch, vn, sizeof(scratch));
    }

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; i++) {
            for (size_t j = 0; j < 4; j++) {
                float32 e = n[H4(i) + stride * j];
                d[H1(i + nelem * j)] =
                    fcvt_f32_to_fp8e5m2(e, ctx.scale, osc, &ctx.stat);
            }
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; i++) {
            for (size_t j = 0; j < 4; j++) {
                float32 e = n[H4(i) + stride * j];
                d[H1(i + nelem * j)] =
                    fcvt_f32_to_fp8e4m3(e, ctx.scale, osc, &ctx.stat);
            }
        }
        break;
    default:
        float8_invalid_output(d, oprsz, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
}

void HELPER(sme2_fcvtn_bs)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc);
    uint32_t *n0 = vn;
    uint32_t *n1 = vn + sizeof(ARMVectorReg);
    uint32_t *n2 = vn + sizeof(ARMVectorReg) * 2;
    uint32_t *n3 = vn + sizeof(ARMVectorReg) * 3;
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 4;

    switch (ctx.f8fmt) {
    case OFP8_E5M2:
        for (size_t i = 0; i < nelem; ++i) {
            float32 e0 = n0[H2(i)];
            float32 e1 = n1[H2(i)];
            float32 e2 = n2[H2(i)];
            float32 e3 = n3[H2(i)];
            d[H1(4 * i + 0)] =
                fcvt_f32_to_fp8e5m2(e0, ctx.scale, osc, &ctx.stat);
            d[H1(4 * i + 1)] =
                fcvt_f32_to_fp8e5m2(e1, ctx.scale, osc, &ctx.stat);
            d[H1(4 * i + 2)] =
                fcvt_f32_to_fp8e5m2(e2, ctx.scale, osc, &ctx.stat);
            d[H1(4 * i + 3)] =
                fcvt_f32_to_fp8e5m2(e3, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float32 e0 = n0[H2(i)];
            float32 e1 = n1[H2(i)];
            float32 e2 = n2[H2(i)];
            float32 e3 = n3[H2(i)];
            d[H1(4 * i + 0)] =
                fcvt_f32_to_fp8e4m3(e0, ctx.scale, osc, &ctx.stat);
            d[H1(4 * i + 1)] =
                fcvt_f32_to_fp8e4m3(e1, ctx.scale, osc, &ctx.stat);
            d[H1(4 * i + 2)] =
                fcvt_f32_to_fp8e4m3(e2, ctx.scale, osc, &ctx.stat);
            d[H1(4 * i + 3)] =
                fcvt_f32_to_fp8e4m3(e3, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        float8_invalid_output(d, oprsz, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
}

static FloatParts64 unpack_fp8(uint8_t x, FPMRType type, float_status *s)
{
    switch (type) {
    case OFP8_E5M2:
        return float8_e5m2_unpack_canonical(x, s);
    case OFP8_E4M3:
        return float8_e4m3_unpack_canonical(x, s);
    default:
        return parts64_default_nan(s);
    }
}

static void f8muladd(FloatParts64 *a, const FloatParts64 *b,
                     const FloatParts64 *c, int scale, float_status *s)
{
    /*
     * Because of default_nan_mode, NaNs need no special handling.
     * We'll simply get the default NaN out at the end of the sequence.
     */
    *a = parts64_mul(a, b, s);
    *a = parts64_scalbn(a, scale, s);
    *a = parts64_addsub(a, c, s, false);
}

void HELPER(gvec_fmla_hb)(void *vd, void *vn, void *vm,
                          CPUARMState *env, uint32_t desc)
{
    float_status stat = env->vfp.fp_status[FPST_A64];
    bool high = extract32(desc, SIMD_DATA_SHIFT, 1);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    uint8_t *n = vn;
    uint8_t *m = vm;
    float16 *d = vd;

    uint64_t fpmr = env->vfp.fpmr;
    FPMRType fmt_n = FIELD_EX64(fpmr, FPMR, F8S1);
    FPMRType fmt_m = FIELD_EX64(fpmr, FPMR, F8S2);
    int scale = -((fpmr >> R_FPMR_LSCALE_SHIFT) & 0xf);

    set_flush_to_zero(0, &stat);
    set_flush_inputs_to_zero(0, &stat);
    set_default_nan_mode(true, &stat);
    set_float_rounding_mode(FIELD_EX64(fpmr, FPMR, OSM)
                            ? float_round_nearest_even_max
                            : float_round_nearest_even, &stat);

    for (size_t i = 0; i < nelem; i++) {
        FloatParts64 p0 = unpack_fp8(n[H1(2 * i + high)], fmt_n, &stat);
        FloatParts64 p1 = unpack_fp8(m[H1(2 * i + high)], fmt_m, &stat);
        FloatParts64 p2 = float16_unpack_canonical(d[H2(i)], &stat);

        f8muladd(&p0, &p1, &p2, scale, &stat);
        d[H2(i)] = float16_round_pack_canonical(&p0, &stat);
    }

    float_raise(get_float_exception_flags(&stat)
                & ~float_flag_input_denormal_used,
                &env->vfp.fp_status[FPST_A64]);
    clear_tail(vd, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_fmla_idx_hb)(void *vd, void *vn, void *vm,
                              CPUARMState *env, uint32_t desc)
{
    float_status stat = env->vfp.fp_status[FPST_A64];
    bool idx_n = extract32(desc, SIMD_DATA_SHIFT, 1);
    size_t idx_m = extract32(desc, SIMD_DATA_SHIFT + 2, 4);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    uint8_t *n = vn;
    uint8_t *m = vm;
    float16 *d = vd;

    uint64_t fpmr = env->vfp.fpmr;
    FPMRType fmt_n = FIELD_EX64(fpmr, FPMR, F8S1);
    FPMRType fmt_m = FIELD_EX64(fpmr, FPMR, F8S2);
    int scale = -((fpmr >> R_FPMR_LSCALE_SHIFT) & 0xf);

    set_flush_to_zero(0, &stat);
    set_flush_inputs_to_zero(0, &stat);
    set_default_nan_mode(true, &stat);
    set_float_rounding_mode(FIELD_EX64(fpmr, FPMR, OSM)
                            ? float_round_nearest_even_max
                            : float_round_nearest_even, &stat);

    for (size_t seg = 0; seg < nelem; seg += 8) {
        FloatParts64 p1 = unpack_fp8(m[H1(2 * seg + idx_m)], fmt_m, &stat);

        for (size_t j = 0; j < 8; j++) {
            size_t i = seg + j;
            FloatParts64 p0 = unpack_fp8(n[H1(2 * i + idx_n)], fmt_n, &stat);
            FloatParts64 p2 = float16_unpack_canonical(d[H2(i)], &stat);

            f8muladd(&p0, &p1, &p2, scale, &stat);
            d[H2(i)] = float16_round_pack_canonical(&p0, &stat);
        }
    }

    float_raise(get_float_exception_flags(&stat)
                & ~float_flag_input_denormal_used,
                &env->vfp.fp_status[FPST_A64]);
    clear_tail(vd, oprsz, simd_maxsz(desc));
}
