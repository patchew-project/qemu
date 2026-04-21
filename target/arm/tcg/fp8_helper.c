/*
 * AArch64 FP8 Operations
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"
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
            d[H2(i)] = float8_e5m2_to_bfloat16(n[H1(i)], ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (int i = 0; i < 8; ++i) {
            d[H2(i)] = float8_e4m3_to_bfloat16(n[H1(i)], ctx.scale, &ctx.stat);
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
            d[H2(i)] = float8_e5m2_to_float16(n[H1(i)], ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (int i = 0; i < 8; ++i) {
            d[H2(i)] = float8_e4m3_to_float16(n[H1(i)], ctx.scale, &ctx.stat);
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
            d[H2(i)] = float8_e5m2_to_bfloat16(e, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(2 * i + ctx.high)];
            d[H2(i)] = float8_e4m3_to_bfloat16(e, ctx.scale, &ctx.stat);
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
            d[H2(i)] = float8_e5m2_to_float16(e, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(2 * i + ctx.high)];
            d[H2(i)] = float8_e4m3_to_float16(e, ctx.scale, &ctx.stat);
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
            d0[H2(i)] = float8_e5m2_to_bfloat16(e, ctx.scale, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e = n[H1(i) + nelem];
            d1[H2(i)] = float8_e5m2_to_bfloat16(e, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(i)];
            d0[H2(i)] = float8_e4m3_to_bfloat16(e, ctx.scale, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(i) + nelem];
            d1[H2(i)] = float8_e4m3_to_bfloat16(e, ctx.scale, &ctx.stat);
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
            d0[H2(i)] = float8_e5m2_to_float16(e, ctx.scale, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float8_e5m2 e = n[H1(i) + nelem];
            d1[H2(i)] = float8_e5m2_to_float16(e, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(i)];
            d0[H2(i)] = float8_e4m3_to_float16(e, ctx.scale, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e = n[H1(i) + nelem];
            d1[H2(i)] = float8_e4m3_to_float16(e, ctx.scale, &ctx.stat);
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
            d0[H2(i)] = float8_e5m2_to_bfloat16(e0, ctx.scale, &ctx.stat);
            d1[H2(i)] = float8_e5m2_to_bfloat16(e1, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e0 = n[H1(2 * i + 0)];
            float8_e4m3 e1 = n[H1(2 * i + 1)];
            d0[H2(i)] = float8_e4m3_to_bfloat16(e0, ctx.scale, &ctx.stat);
            d1[H2(i)] = float8_e4m3_to_bfloat16(e1, ctx.scale, &ctx.stat);
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
            d0[H2(i)] = float8_e5m2_to_float16(e0, ctx.scale, &ctx.stat);
            d1[H2(i)] = float8_e5m2_to_float16(e1, ctx.scale, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float8_e4m3 e0 = n[H1(2 * i + 0)];
            float8_e4m3 e1 = n[H1(2 * i + 1)];
            d0[H2(i)] = float8_e4m3_to_float16(e0, ctx.scale, &ctx.stat);
            d1[H2(i)] = float8_e4m3_to_float16(e1, ctx.scale, &ctx.stat);
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
                bfloat16_to_float8_e5m2(e0, ctx.scale, osc, &ctx.stat);
            d[H1(2 * i + 1)] =
                bfloat16_to_float8_e5m2(e1, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            bfloat16 e0 = n0[H2(i)];
            bfloat16 e1 = n1[H2(i)];
            d[H1(2 * i + 0)] =
                bfloat16_to_float8_e4m3(e0, ctx.scale, osc, &ctx.stat);
            d[H1(2 * i + 1)] =
                bfloat16_to_float8_e4m3(e1, ctx.scale, osc, &ctx.stat);
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
            d[H1(i)] = float16_to_float8_e5m2(e, ctx.scale, osc, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float16 e = m[H2(i)];
            d[H1(i) + nelem] =
                float16_to_float8_e5m2(e, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < nelem; ++i) {
            float16 e = n[H2(i)];
            d[H1(i)] = float16_to_float8_e4m3(e, ctx.scale, osc, &ctx.stat);
        }
        for (size_t i = 0; i < nelem; ++i) {
            float16 e = m[H2(i)];
            d[H1(i) + nelem] =
                float16_to_float8_e4m3(e, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        float8_invalid_output(d, oprsz, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
    clear_tail(vd, oprsz, simd_maxsz(desc));
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
            float32 e = n[H2(i)];
            d[H1(i + 0)] = float32_to_float8_e5m2(e, ctx.scale, osc, &ctx.stat);
        }
        for (size_t i = 0; i < 4; ++i) {
            float16 e = m[H2(i)];
            d[H1(i + 4)] = float16_to_float8_e5m2(e, ctx.scale, osc, &ctx.stat);
        }
        break;
    case OFP8_E4M3:
        for (size_t i = 0; i < 4; ++i) {
            float16 e = n[H2(i)];
            d[H1(i + 0)] = float16_to_float8_e4m3(e, ctx.scale, osc, &ctx.stat);
        }
        for (size_t i = 0; i < 4; ++i) {
            float16 e = m[H2(i)];
            d[H1(i + 4)] = float16_to_float8_e4m3(e, ctx.scale, osc, &ctx.stat);
        }
        break;
    default:
        float8_invalid_output(d, 8, &ctx.stat);
        break;
    }

    fp8_finish(env, &ctx);
    clear_tail(vd, ctx.high ? 16 : 8, simd_maxsz(desc));
}
