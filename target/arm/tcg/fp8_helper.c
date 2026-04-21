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
