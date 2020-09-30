/*
 * QEMU TCG support -- s390x vector floating point instruction support
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "internal.h"
#include "vec.h"
#include "tcg_s390x.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

const float32 float32_ones = make_float32(-1u);
const float64 float64_ones = make_float64(-1ull);
const float128 float128_ones = make_float128(-1ull, -1ull);
const float32 float32_zeroes = make_float32(0);
const float64 float64_zeroes = make_float64(0);
const float128 float128_zeroes = make_float128(0, 0);

#define VIC_INVALID         0x1
#define VIC_DIVBYZERO       0x2
#define VIC_OVERFLOW        0x3
#define VIC_UNDERFLOW       0x4
#define VIC_INEXACT         0x5

/* returns the VEX. If the VEX is 0, there is no trap */
static uint8_t check_ieee_exc(CPUS390XState *env, uint8_t enr, bool XxC,
                              uint8_t *vec_exc)
{
    uint8_t vece_exc = 0, trap_exc;
    unsigned qemu_exc;

    /* Retrieve and clear the softfloat exceptions */
    qemu_exc = env->fpu_status.float_exception_flags;
    if (qemu_exc == 0) {
        return 0;
    }
    env->fpu_status.float_exception_flags = 0;

    vece_exc = s390_softfloat_exc_to_ieee(qemu_exc);

    /* Add them to the vector-wide s390x exception bits */
    *vec_exc |= vece_exc;

    /* Check for traps and construct the VXC */
    trap_exc = vece_exc & env->fpc >> 24;
    if (trap_exc) {
        if (trap_exc & S390_IEEE_MASK_INVALID) {
            return enr << 4 | VIC_INVALID;
        } else if (trap_exc & S390_IEEE_MASK_DIVBYZERO) {
            return enr << 4 | VIC_DIVBYZERO;
        } else if (trap_exc & S390_IEEE_MASK_OVERFLOW) {
            return enr << 4 | VIC_OVERFLOW;
        } else if (trap_exc & S390_IEEE_MASK_UNDERFLOW) {
            return enr << 4 | VIC_UNDERFLOW;
        } else if (!XxC) {
            g_assert(trap_exc & S390_IEEE_MASK_INEXACT);
            /* inexact has lowest priority on traps */
            return enr << 4 | VIC_INEXACT;
        }
    }
    return 0;
}

static void handle_ieee_exc(CPUS390XState *env, uint8_t vxc, uint8_t vec_exc,
                            uintptr_t retaddr)
{
    if (vxc) {
        /* on traps, the fpc flags are not updated, instruction is suppressed */
        tcg_s390_vector_exception(env, vxc, retaddr);
    }
    if (vec_exc) {
        /* indicate exceptions for all elements combined */
        env->fpc |= vec_exc << 16;
    }
}

static float32 s390_vec_read_float32(const S390Vector *v, uint8_t enr)
{
    return make_float32(s390_vec_read_element32(v, enr));
}

static float64 s390_vec_read_float64(const S390Vector *v, uint8_t enr)
{
    return make_float64(s390_vec_read_element64(v, enr));
}

static float128 s390_vec_read_float128(const S390Vector *v, uint8_t enr)
{
    g_assert(enr == 0);
    return make_float128(s390_vec_read_element64(v, 0),
                         s390_vec_read_element64(v, 1));
}

static void s390_vec_write_float32(S390Vector *v, uint8_t enr, float32 data)
{
    return s390_vec_write_element32(v, enr, data);
}

static void s390_vec_write_float64(S390Vector *v, uint8_t enr, float64 data)
{
    return s390_vec_write_element64(v, enr, data);
}

static void s390_vec_write_float128(S390Vector *v, uint8_t enr, float128 data)
{
    g_assert(enr == 0);
    s390_vec_write_element64(v, 0, data.high);
    s390_vec_write_element64(v, 1, data.low);
}

#define DEF_VOP_2(BITS)                                                        \
typedef float##BITS (*vop##BITS##_2_fn)(float##BITS a, float_status *s);       \
static void vop##BITS##_2(S390Vector *v1, const S390Vector *v2,                \
                          CPUS390XState *env, bool s, bool XxC, uint8_t erm,   \
                          vop##BITS##_2_fn fn, uintptr_t retaddr)              \
{                                                                              \
    uint8_t vxc, vec_exc = 0;                                                  \
    S390Vector tmp = {};                                                       \
    int i, old_mode;                                                           \
                                                                               \
    old_mode = s390_swap_bfp_rounding_mode(env, erm);                          \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const float##BITS a = s390_vec_read_float##BITS(v2, i);                \
                                                                               \
        s390_vec_write_float##BITS(&tmp, i, fn(a, &env->fpu_status));          \
        vxc = check_ieee_exc(env, i, XxC, &vec_exc);                           \
        if (s || vxc) {                                                        \
            break;                                                             \
        }                                                                      \
    }                                                                          \
    s390_restore_bfp_rounding_mode(env, old_mode);                             \
    handle_ieee_exc(env, vxc, vec_exc, retaddr);                               \
    *v1 = tmp;                                                                 \
}
DEF_VOP_2(32)
DEF_VOP_2(64)
DEF_VOP_2(128)

#define DEF_VOP_3(BITS)                                                        \
typedef float##BITS (*vop##BITS##_3_fn)(float##BITS a, float##BITS b,          \
                                        float_status *s);                      \
static void vop##BITS##_3(S390Vector *v1, const S390Vector *v2,                \
                          const S390Vector *v3, CPUS390XState *env, bool s,    \
                          vop##BITS##_3_fn fn, uintptr_t retaddr)              \
{                                                                              \
    uint8_t vxc, vec_exc = 0;                                                  \
    S390Vector tmp = {};                                                       \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const float##BITS a = s390_vec_read_float##BITS(v2, i);                \
        const float##BITS b = s390_vec_read_float##BITS(v3, i);                \
                                                                               \
        s390_vec_write_float##BITS(&tmp, i, fn(a, b, &env->fpu_status));       \
        vxc = check_ieee_exc(env, i, false, &vec_exc);                         \
        if (s || vxc) {                                                        \
            break;                                                             \
        }                                                                      \
    }                                                                          \
    handle_ieee_exc(env, vxc, vec_exc, retaddr);                               \
    *v1 = tmp;                                                                 \
}
DEF_VOP_3(32)
DEF_VOP_3(64)
DEF_VOP_3(128)

#define DEF_GVEC_FVA(BITS)                                                     \
void HELPER(gvec_vfa##BITS)(void *v1, const void *v2, const void *v3,          \
                            CPUS390XState *env, uint32_t desc)                 \
{                                                                              \
    vop##BITS##_3(v1, v2, v3, env, false, float##BITS##_add, GETPC());         \
}
DEF_GVEC_FVA(32)
DEF_GVEC_FVA(64)
DEF_GVEC_FVA(128)

#define DEF_GVEC_FVA_S(BITS)                                                   \
void HELPER(gvec_vfa##BITS##s)(void *v1, const void *v2, const void *v3,       \
                               CPUS390XState *env, uint32_t desc)              \
{                                                                              \
    vop##BITS##_3(v1, v2, v3, env, true, float##BITS##_add, GETPC());          \
}
DEF_GVEC_FVA_S(32)
DEF_GVEC_FVA_S(64)

#define DEF_WFC(BITS)                                                          \
static int wfc##BITS(const S390Vector *v1, const S390Vector *v2,               \
                     CPUS390XState *env, bool signal, uintptr_t retaddr)       \
{                                                                              \
    /* only the zero-indexed elements are compared */                          \
    const float##BITS a = s390_vec_read_float##BITS(v1, 0);                    \
    const float##BITS b = s390_vec_read_float##BITS(v2, 0);                    \
    uint8_t vxc, vec_exc = 0;                                                  \
    int cmp;                                                                   \
                                                                               \
    if (signal) {                                                              \
        cmp = float##BITS##_compare(a, b, &env->fpu_status);                   \
    } else {                                                                   \
        cmp = float##BITS##_compare_quiet(a, b, &env->fpu_status);             \
    }                                                                          \
    vxc = check_ieee_exc(env, 0, false, &vec_exc);                             \
    handle_ieee_exc(env, vxc, vec_exc, retaddr);                               \
                                                                               \
    return float_comp_to_cc(env, cmp);                                         \
}
DEF_WFC(32)
DEF_WFC(64)
DEF_WFC(128)

#define DEF_GVEC_WFC(BITS)                                                     \
void HELPER(gvec_wfc##BITS)(const void *v1, const void *v2, CPUS390XState *env,\
                            uint32_t desc)                                     \
{                                                                              \
    env->cc_op = wfc##BITS(v1, v2, env, false, GETPC());                       \
}
DEF_GVEC_WFC(32)
DEF_GVEC_WFC(64)
DEF_GVEC_WFC(128)

#define DEF_GVEC_WFK(BITS)                                                     \
void HELPER(gvec_wfk##BITS)(const void *v1, const void *v2, CPUS390XState *env,\
                            uint32_t desc)                                     \
{                                                                              \
    env->cc_op = wfc##BITS(v1, v2, env, true, GETPC());                        \
}
DEF_GVEC_WFK(32)
DEF_GVEC_WFK(64)
DEF_GVEC_WFK(128)

#define DEF_VFC(BITS)                                                          \
typedef bool (*vfc##BITS##_fn)(float##BITS a, float##BITS b,                   \
                               float_status *status);                          \
static int vfc##BITS(S390Vector *v1, const S390Vector *v2,                     \
                     const S390Vector *v3, CPUS390XState *env, bool s,         \
                     vfc##BITS##_fn fn, uintptr_t retaddr)                     \
{                                                                              \
    uint8_t vxc, vec_exc = 0;                                                  \
    S390Vector tmp = {};                                                       \
    int match = 0;                                                             \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const float##BITS a = s390_vec_read_float##BITS(v2, i);                \
        const float##BITS b = s390_vec_read_float##BITS(v3, i);                \
                                                                               \
        /* swap the parameters, so we can use existing functions */            \
        if (fn(b, a, &env->fpu_status)) {                                      \
            match++;                                                           \
            s390_vec_write_float##BITS(&tmp, i, float##BITS##_ones);           \
        }                                                                      \
        vxc = check_ieee_exc(env, i, false, &vec_exc);                         \
        if (s || vxc) {                                                        \
            break;                                                             \
        }                                                                      \
    }                                                                          \
                                                                               \
    handle_ieee_exc(env, vxc, vec_exc, retaddr);                               \
    *v1 = tmp;                                                                 \
    if (match) {                                                               \
        return s || match == (128 / BITS) ? 0 : 1;                             \
    }                                                                          \
    return 3;                                                                  \
}
DEF_VFC(32)
DEF_VFC(64)
DEF_VFC(128)

#define DEF_GVEC_VFCE(BITS)                                                    \
void HELPER(gvec_vfce##BITS)(void *v1, const void *v2, const void *v3,         \
                             CPUS390XState *env, uint32_t desc)                \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    vfc##BITS(v1, v2, v3, env, false,                                          \
              sq ? float##BITS##_eq : float##BITS##_eq_quiet, GETPC());        \
}
DEF_GVEC_VFCE(32)
DEF_GVEC_VFCE(64)
DEF_GVEC_VFCE(128)

#define DEF_GVEC_VFCE_S(BITS)                                                  \
void HELPER(gvec_vfce##BITS##s)(void *v1, const void *v2, const void *v3,      \
                                CPUS390XState *env, uint32_t desc)             \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    vfc##BITS(v1, v2, v3, env, true,                                           \
              sq ? float##BITS##_eq : float##BITS##_eq_quiet, GETPC());        \
}
DEF_GVEC_VFCE_S(32)
DEF_GVEC_VFCE_S(64)

#define DEF_GVEC_VFCE_CC(BITS)                                                 \
void HELPER(gvec_vfce##BITS##_cc)(void *v1, const void *v2, const void *v3,    \
                                  CPUS390XState *env, uint32_t desc)           \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    env->cc_op = vfc##BITS(v1, v2, v3, env, false,                             \
                           sq ? float##BITS##_eq : float##BITS##_eq_quiet,     \
                           GETPC());                                           \
}
DEF_GVEC_VFCE_CC(32)
DEF_GVEC_VFCE_CC(64)
DEF_GVEC_VFCE_CC(128)

#define DEF_GVEC_VFCE_S_CC(BITS)                                               \
void HELPER(gvec_vfce##BITS##s_cc)(void *v1, const void *v2, const void *v3,   \
                                   CPUS390XState *env, uint32_t desc)          \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    env->cc_op = vfc##BITS(v1, v2, v3, env, true,                              \
                           sq ? float##BITS##_eq : float##BITS##_eq_quiet,     \
                           GETPC());                                           \
}
DEF_GVEC_VFCE_S_CC(32)
DEF_GVEC_VFCE_S_CC(64)

#define DEF_GVEC_VFCH(BITS)                                                    \
void HELPER(gvec_vfch##BITS)(void *v1, const void *v2, const void *v3,         \
                             CPUS390XState *env, uint32_t desc)                \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    vfc##BITS(v1, v2, v3, env, false,                                          \
              sq ? float##BITS##_lt : float##BITS##_lt_quiet, GETPC());        \
}
DEF_GVEC_VFCH(32)
DEF_GVEC_VFCH(64)
DEF_GVEC_VFCH(128)

#define DEF_GVEC_VFCH_S(BITS)                                                  \
void HELPER(gvec_vfch##BITS##s)(void *v1, const void *v2, const void *v3,      \
                                CPUS390XState *env, uint32_t desc)             \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    vfc##BITS(v1, v2, v3, env, true,                                           \
              sq ? float##BITS##_lt : float##BITS##_lt_quiet, GETPC());        \
}
DEF_GVEC_VFCH_S(32)
DEF_GVEC_VFCH_S(64)

#define DEF_GVEC_VFCH_CC(BITS)                                                 \
void HELPER(gvec_vfch##BITS##_cc)(void *v1, const void *v2, const void *v3,    \
                                  CPUS390XState *env, uint32_t desc)           \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    env->cc_op = vfc##BITS(v1, v2, v3, env, false,                             \
                           sq ? float##BITS##_lt : float##BITS##_lt_quiet,     \
                           GETPC());                                           \
}
DEF_GVEC_VFCH_CC(32)
DEF_GVEC_VFCH_CC(64)
DEF_GVEC_VFCH_CC(128)

#define DEF_GVEC_VFCH_S_CC(BITS)                                               \
void HELPER(gvec_vfch##BITS##s_cc)(void *v1, const void *v2, const void *v3,   \
                                   CPUS390XState *env, uint32_t desc)          \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    env->cc_op = vfc##BITS(v1, v2, v3, env, true,                              \
                           sq ? float##BITS##_lt : float##BITS##_lt_quiet,     \
                           GETPC());                                           \
}
DEF_GVEC_VFCH_S_CC(32)
DEF_GVEC_VFCH_S_CC(64)

#define DEF_GVEC_VFCHE(BITS)                                                   \
void HELPER(gvec_vfche##BITS)(void *v1, const void *v2, const void *v3,        \
                              CPUS390XState *env, uint32_t desc)               \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    vfc##BITS(v1, v2, v3, env, false,                                          \
              sq ? float##BITS##_le : float##BITS##_le_quiet, GETPC());        \
}
DEF_GVEC_VFCHE(32)
DEF_GVEC_VFCHE(64)
DEF_GVEC_VFCHE(128)

#define DEF_GVEC_VFCHE_S(BITS)                                                 \
void HELPER(gvec_vfche##BITS##s)(void *v1, const void *v2, const void *v3,     \
                                 CPUS390XState *env, uint32_t desc)            \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    vfc##BITS(v1, v2, v3, env, true,                                           \
              sq ? float##BITS##_le : float##BITS##_le_quiet, GETPC());        \
}
DEF_GVEC_VFCHE_S(32)
DEF_GVEC_VFCHE_S(64)

#define DEF_GVEC_VFCHE_CC(BITS)                                                \
void HELPER(gvec_vfche##BITS##_cc)(void *v1, const void *v2, const void *v3,   \
                                   CPUS390XState *env, uint32_t desc)          \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    env->cc_op = vfc##BITS(v1, v2, v3, env, false,                             \
                           sq ? float##BITS##_le : float##BITS##_le_quiet,     \
                           GETPC());                                           \
}
DEF_GVEC_VFCHE_CC(32)
DEF_GVEC_VFCHE_CC(64)
DEF_GVEC_VFCHE_CC(128)

#define DEF_GVEC_VFCHE_S_CC(BITS)                                              \
void HELPER(gvec_vfche##BITS##s_cc)(void *v1, const void *v2, const void *v3,  \
                                    CPUS390XState *env, uint32_t desc)         \
{                                                                              \
    const bool sq = simd_data(desc);                                           \
                                                                               \
    env->cc_op = vfc##BITS(v1, v2, v3, env, true,                              \
                           sq ? float##BITS##_le : float##BITS##_le_quiet,     \
                           GETPC());                                           \
}
DEF_GVEC_VFCHE_S_CC(32)
DEF_GVEC_VFCHE_S_CC(64)

static uint64_t vcdg64(uint64_t a, float_status *s)
{
    return int64_to_float64(a, s);
}

void HELPER(gvec_vcdg64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vop64_2(v1, v2, env, false, XxC, erm, vcdg64, GETPC());
}

void HELPER(gvec_vcdg64s)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vop64_2(v1, v2, env, true, XxC, erm, vcdg64, GETPC());
}

static uint64_t vcdlg64(uint64_t a, float_status *s)
{
    return uint64_to_float64(a, s);
}

void HELPER(gvec_vcdlg64)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vop64_2(v1, v2, env, false, XxC, erm, vcdlg64, GETPC());
}

void HELPER(gvec_vcdlg64s)(void *v1, const void *v2, CPUS390XState *env,
                           uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vop64_2(v1, v2, env, true, XxC, erm, vcdlg64, GETPC());
}

static uint64_t vcgd64(uint64_t a, float_status *s)
{
    return float64_to_int64(a, s);
}

void HELPER(gvec_vcgd64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vop64_2(v1, v2, env, false, XxC, erm, vcgd64, GETPC());
}

void HELPER(gvec_vcgd64s)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vop64_2(v1, v2, env, true, XxC, erm, vcgd64, GETPC());
}

static uint64_t vclgd64(uint64_t a, float_status *s)
{
    return float64_to_uint64(a, s);
}

void HELPER(gvec_vclgd64)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vop64_2(v1, v2, env, false, XxC, erm, vclgd64, GETPC());
}

void HELPER(gvec_vclgd64s)(void *v1, const void *v2, CPUS390XState *env,
                           uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vop64_2(v1, v2, env, true, XxC, erm, vclgd64, GETPC());
}

#define DEF_GVEC_FVD(BITS)                                                     \
void HELPER(gvec_vfd##BITS)(void *v1, const void *v2, const void *v3,          \
                            CPUS390XState *env, uint32_t desc)                 \
{                                                                              \
    vop##BITS##_3(v1, v2, v3, env, false, float##BITS##_div, GETPC());         \
}
DEF_GVEC_FVD(32)
DEF_GVEC_FVD(64)
DEF_GVEC_FVD(128)

#define DEF_GVEC_FVD_S(BITS)                                                   \
void HELPER(gvec_vfd##BITS##s)(void *v1, const void *v2, const void *v3,       \
                               CPUS390XState *env, uint32_t desc)              \
{                                                                              \
    vop##BITS##_3(v1, v2, v3, env, true, float##BITS##_div, GETPC());          \
}
DEF_GVEC_FVD_S(32)
DEF_GVEC_FVD_S(64)

#define DEF_GVEC_VFI(BITS)                                                     \
void HELPER(gvec_vfi##BITS)(void *v1, const void *v2, CPUS390XState *env,      \
                            uint32_t desc)                                     \
{                                                                              \
    const uint8_t erm = extract32(simd_data(desc), 4, 4);                      \
    const bool XxC = extract32(simd_data(desc), 2, 1);                         \
                                                                               \
    vop##BITS##_2(v1, v2, env, false, XxC, erm, float##BITS##_round_to_int,    \
                  GETPC());                                                    \
}
DEF_GVEC_VFI(32)
DEF_GVEC_VFI(64)
DEF_GVEC_VFI(128)

#define DEF_GVEC_VFI_S(BITS)                                                   \
void HELPER(gvec_vfi##BITS##s)(void *v1, const void *v2, CPUS390XState *env,   \
                               uint32_t desc)                                  \
{                                                                              \
    const uint8_t erm = extract32(simd_data(desc), 4, 4);                      \
    const bool XxC = extract32(simd_data(desc), 2, 1);                         \
                                                                               \
    vop##BITS##_2(v1, v2, env, true, XxC, erm, float##BITS##_round_to_int,     \
                  GETPC());                                                    \
}
DEF_GVEC_VFI_S(32)
DEF_GVEC_VFI_S(64)

static void vfll32(S390Vector *v1, const S390Vector *v2, CPUS390XState *env,
                   bool s, uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        /* load from even element */
        const float32 a = s390_vec_read_element32(v2, i * 2);
        const uint64_t ret = float32_to_float64(a, &env->fpu_status);

        s390_vec_write_element64(&tmp, i, ret);
        /* indicate the source element */
        vxc = check_ieee_exc(env, i * 2, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

void HELPER(gvec_vfll32)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    vfll32(v1, v2, env, false, GETPC());
}

void HELPER(gvec_vfll32s)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    vfll32(v1, v2, env, true, GETPC());
}

void HELPER(gvec_vfll64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    /* load from even element */
    float128 ret = float64_to_float128(s390_vec_read_float64(v2, 0),
                                       &env->fpu_status);
    uint8_t vxc, vec_exc = 0;

    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    s390_vec_write_float128(v1, 0, ret);
}

static void vflr64(S390Vector *v1, const S390Vector *v2, CPUS390XState *env,
                   bool s, bool XxC, uint8_t erm, uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i, old_mode;

    old_mode = s390_swap_bfp_rounding_mode(env, erm);
    for (i = 0; i < 2; i++) {
        float64 a = s390_vec_read_element64(v2, i);
        uint32_t ret = float64_to_float32(a, &env->fpu_status);

        /* place at even element */
        s390_vec_write_element32(&tmp, i * 2, ret);
        /* indicate the source element */
        vxc = check_ieee_exc(env, i, XxC, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

void HELPER(gvec_vflr64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vflr64(v1, v2, env, false, XxC, erm, GETPC());
}

void HELPER(gvec_vflr64s)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);

    vflr64(v1, v2, env, true, XxC, erm, GETPC());
}

void HELPER(gvec_vflr128)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);
    uint8_t vxc, vec_exc = 0;
    int old_mode;
    float64 ret;

    old_mode = s390_swap_bfp_rounding_mode(env, erm);
    ret = float128_to_float64(s390_vec_read_float128(v2, 0), &env->fpu_status);
    vxc = check_ieee_exc(env, 0, XxC, &vec_exc);
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());

    /* place at even element, odd element is unpredictable */
    s390_vec_write_float64(v1, 0, ret);
}

#define DEF_GVEC_FVM(BITS)                                                     \
void HELPER(gvec_vfm##BITS)(void *v1, const void *v2, const void *v3,          \
                            CPUS390XState *env, uint32_t desc)                 \
{                                                                              \
    vop##BITS##_3(v1, v2, v3, env, false, float##BITS##_mul, GETPC());         \
}
DEF_GVEC_FVM(32)
DEF_GVEC_FVM(64)
DEF_GVEC_FVM(128)

#define DEF_GVEC_FVM_S(BITS)                                                   \
void HELPER(gvec_vfm##BITS##s)(void *v1, const void *v2, const void *v3,       \
                               CPUS390XState *env, uint32_t desc)              \
{                                                                              \
    vop##BITS##_3(v1, v2, v3, env, true, float##BITS##_mul, GETPC());          \
}
DEF_GVEC_FVM_S(32)
DEF_GVEC_FVM_S(64)

#define DEF_VFMA(BITS)                                                         \
static void vfma##BITS(S390Vector *v1, const S390Vector *v2,                   \
                       const S390Vector *v3, const S390Vector *v4,             \
                       CPUS390XState *env, bool s, int flags,                  \
                       uintptr_t retaddr)                                      \
{                                                                              \
    uint8_t vxc, vec_exc = 0;                                                  \
    S390Vector tmp = {};                                                       \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const float##BITS a = s390_vec_read_float##BITS(v2, i);                \
        const float##BITS b = s390_vec_read_float##BITS(v3, i);                \
        const float##BITS c = s390_vec_read_float##BITS(v4, i);                \
        float##BITS ret = float##BITS##_muladd(a, b, c, flags,                 \
                                               &env->fpu_status);              \
                                                                               \
        s390_vec_write_float##BITS(&tmp, i, ret);                              \
        vxc = check_ieee_exc(env, i, false, &vec_exc);                         \
        if (s || vxc) {                                                        \
            break;                                                             \
        }                                                                      \
    }                                                                          \
    handle_ieee_exc(env, vxc, vec_exc, retaddr);                               \
    *v1 = tmp;                                                                 \
}
DEF_VFMA(32)
DEF_VFMA(64)
DEF_VFMA(128)

#define DEF_GVEC_VFMA(BITS)                                                    \
void HELPER(gvec_vfma##BITS)(void *v1, const void *v2, const void *v3,         \
                             const void *v4, CPUS390XState *env, uint32_t desc)\
{                                                                              \
    vfma##BITS(v1, v2, v3, v4, env, false, 0, GETPC());                        \
}
DEF_GVEC_VFMA(32)
DEF_GVEC_VFMA(64)
DEF_GVEC_VFMA(128)

#define DEF_GVEC_VFMA_S(BITS)                                                  \
void HELPER(gvec_vfma##BITS##s)(void *v1, const void *v2, const void *v3,      \
                                const void *v4, CPUS390XState *env,            \
                                uint32_t desc)                                 \
{                                                                              \
    vfma##BITS(v1, v2, v3, v4, env, true, 0, GETPC());                         \
}
DEF_GVEC_VFMA_S(32)
DEF_GVEC_VFMA_S(64)

#define DEF_GVEC_VFMS(BITS)                                                    \
void HELPER(gvec_vfms##BITS)(void *v1, const void *v2, const void *v3,         \
                             const void *v4, CPUS390XState *env, uint32_t desc)\
{                                                                              \
    vfma##BITS(v1, v2, v3, v4, env, false, float_muladd_negate_c, GETPC());    \
}
DEF_GVEC_VFMS(32)
DEF_GVEC_VFMS(64)
DEF_GVEC_VFMS(128)

#define DEF_GVEC_VFMS_S(BITS)                                                  \
void HELPER(gvec_vfms##BITS##s)(void *v1, const void *v2, const void *v3,      \
                                const void *v4, CPUS390XState *env,            \
                                uint32_t desc)                                 \
{                                                                              \
    vfma##BITS(v1, v2, v3, v4, env, true, float_muladd_negate_c, GETPC());     \
}
DEF_GVEC_VFMS_S(32)
DEF_GVEC_VFMS_S(64)

#define DEF_GVEC_VFNMA(BITS)                                                   \
void HELPER(gvec_vfnma##BITS)(void *v1, const void *v2, const void *v3,        \
                              const void *v4, CPUS390XState *env,              \
                              uint32_t desc)                                   \
{                                                                              \
    vfma##BITS(v1, v2, v3, v4, env, false, float_muladd_negate_result,         \
               GETPC());                                                       \
}
DEF_GVEC_VFNMA(32)
DEF_GVEC_VFNMA(64)
DEF_GVEC_VFNMA(128)

#define DEF_GVEC_VFNMA_S(BITS)                                                 \
void HELPER(gvec_vfnma##BITS##s)(void *v1, const void *v2, const void *v3,     \
                                 const void *v4, CPUS390XState *env,           \
                                 uint32_t desc)                                \
{                                                                              \
    vfma##BITS(v1, v2, v3, v4, env, true, float_muladd_negate_result, GETPC());\
}
DEF_GVEC_VFNMA_S(32)
DEF_GVEC_VFNMA_S(64)

#define DEF_GVEC_VFNMS(BITS)                                                   \
void HELPER(gvec_vfnms##BITS)(void *v1, const void *v2, const void *v3,        \
                              const void *v4, CPUS390XState *env,              \
                              uint32_t desc)                                   \
{                                                                              \
    vfma##BITS(v1, v2, v3, v4, env, false,                                     \
               float_muladd_negate_c | float_muladd_negate_result, GETPC());   \
}
DEF_GVEC_VFNMS(32)
DEF_GVEC_VFNMS(64)
DEF_GVEC_VFNMS(128)

#define DEF_GVEC_VFNMS_S(BITS)                                                 \
void HELPER(gvec_vfnms##BITS##s)(void *v1, const void *v2, const void *v3,     \
                                 const void *v4, CPUS390XState *env,           \
                                 uint32_t desc)                                \
{                                                                              \
    vfma##BITS(v1, v2, v3, v4, env, true,                                      \
               float_muladd_negate_c | float_muladd_negate_result, GETPC());   \
}
DEF_GVEC_VFNMS_S(32)
DEF_GVEC_VFNMS_S(64)

#define DEF_GVEC_VFSQ(BITS)                                                    \
void HELPER(gvec_vfsq##BITS)(void *v1, const void *v2, CPUS390XState *env,     \
                             uint32_t desc)                                    \
{                                                                              \
    vop##BITS##_2(v1, v2, env, false, false, 0, float##BITS##_sqrt, GETPC());  \
}
DEF_GVEC_VFSQ(32)
DEF_GVEC_VFSQ(64)
DEF_GVEC_VFSQ(128)

#define DEF_GVEC_VFSQ_S(BITS)                                                  \
void HELPER(gvec_vfsq##BITS##s)(void *v1, const void *v2, CPUS390XState *env,  \
                                uint32_t desc)                                 \
{                                                                              \
    vop##BITS##_2(v1, v2, env, true, false, 0, float##BITS##_sqrt, GETPC());   \
}
DEF_GVEC_VFSQ_S(32)
DEF_GVEC_VFSQ_S(64)

#define DEF_GVEC_FVS(BITS)                                                     \
void HELPER(gvec_vfs##BITS)(void *v1, const void *v2, const void *v3,          \
                            CPUS390XState *env, uint32_t desc)                 \
{                                                                              \
    vop##BITS##_3(v1, v2, v3, env, false, float##BITS##_sub, GETPC());         \
}
DEF_GVEC_FVS(32)
DEF_GVEC_FVS(64)
DEF_GVEC_FVS(128)

#define DEF_GVEC_FVS_S(BITS)                                                   \
void HELPER(gvec_vfs##BITS##s)(void *v1, const void *v2, const void *v3,       \
                               CPUS390XState *env, uint32_t desc)              \
{                                                                              \
    vop##BITS##_3(v1, v2, v3, env, true, float##BITS##_sub, GETPC());          \
}
DEF_GVEC_FVS_S(32)
DEF_GVEC_FVS_S(64)

#define DEF_VFTCI(BITS)                                                        \
static int vftci##BITS(S390Vector *v1, const S390Vector *v2,                   \
                       CPUS390XState *env, bool s, uint16_t i3)                \
{                                                                              \
    int i, match = 0;                                                          \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        float##BITS a = s390_vec_read_float##BITS(v2, i);                      \
                                                                               \
        if (float##BITS##_dcmask(env, a) & i3) {                               \
            match++;                                                           \
            s390_vec_write_float##BITS(v1, i, float##BITS##_ones);             \
        } else {                                                               \
            s390_vec_write_float##BITS(v1, i, float##BITS##_zeroes);           \
        }                                                                      \
        if (s) {                                                               \
            break;                                                             \
        }                                                                      \
    }                                                                          \
                                                                               \
    if (match) {                                                               \
        return s || match == (128 / BITS) ? 0 : 1;                             \
    }                                                                          \
    return 3;                                                                  \
}
DEF_VFTCI(32)
DEF_VFTCI(64)
DEF_VFTCI(128)

#define DEF_GVEC_VFTCI(BITS)                                                   \
void HELPER(gvec_vftci##BITS)(void *v1, const void *v2, CPUS390XState *env,    \
                              uint32_t desc)                                   \
{                                                                              \
    env->cc_op = vftci##BITS(v1, v2, env, false, simd_data(desc));             \
}
DEF_GVEC_VFTCI(32)
DEF_GVEC_VFTCI(64)
DEF_GVEC_VFTCI(128)

#define DEF_GVEC_VFTCI_S(BITS)                                                 \
void HELPER(gvec_vftci##BITS##s)(void *v1, const void *v2, CPUS390XState *env, \
                                 uint32_t desc)                                \
{                                                                              \
    env->cc_op = vftci##BITS(v1, v2, env, true, simd_data(desc));              \
}
DEF_GVEC_VFTCI_S(32)
DEF_GVEC_VFTCI_S(64)

typedef enum S390MinMaxType {
    s390_minmax_java_math_min,
    s390_minmax_java_math_max,
    s390_minmax_c_macro_min,
    s390_minmax_c_macro_max,
    s390_minmax_fmin,
    s390_minmax_fmax,
    s390_minmax_cpp_alg_min,
    s390_minmax_cpp_alg_max,
} S390MinMaxType;

#define S390_MINMAX(BITS, TYPE)                                                \
static float##BITS TYPE##BITS(float##BITS a, float##BITS b, float_status *s)   \
{                                                                              \
    const bool zero_a = float##BITS##_is_infinity(a);                          \
    const bool zero_b = float##BITS##_is_infinity(b);                          \
    const bool inf_a = float##BITS##_is_infinity(a);                           \
    const bool inf_b = float##BITS##_is_infinity(b);                           \
    const bool nan_a = float##BITS##_is_infinity(a);                           \
    const bool nan_b = float##BITS##_is_infinity(b);                           \
    const bool neg_a = float##BITS##_is_neg(a);                                \
    const bool neg_b = float##BITS##_is_neg(b);                                \
                                                                               \
    if (unlikely(nan_a || nan_b)) {                                            \
        const bool sig_a = float##BITS##_is_signaling_nan(a, s);               \
        const bool sig_b = float##BITS##_is_signaling_nan(b, s);               \
                                                                               \
        if (sig_a || sig_b) {                                                  \
            s->float_exception_flags |= float_flag_invalid;                    \
        }                                                                      \
        switch (TYPE) {                                                        \
        case s390_minmax_java_math_min:                                        \
        case s390_minmax_java_math_max:                                        \
            if (sig_a) {                                                       \
                return float##BITS##_silence_nan(a, s);                        \
            } else if (sig_b) {                                                \
                return float##BITS##_silence_nan(b, s);                        \
            }                                                                  \
            /* fall through */                                                 \
        case s390_minmax_fmin:                                                 \
        case s390_minmax_fmax:                                                 \
            return nan_a ? a : b;                                              \
        case s390_minmax_c_macro_min:                                          \
        case s390_minmax_c_macro_max:                                          \
            s->float_exception_flags |= float_flag_invalid;                    \
            return b;                                                          \
        case s390_minmax_cpp_alg_min:                                          \
        case s390_minmax_cpp_alg_max:                                          \
            s->float_exception_flags |= float_flag_invalid;                    \
            return a;                                                          \
        default:                                                               \
            g_assert_not_reached();                                            \
        }                                                                      \
    } else if (unlikely(inf_a && inf_b)) {                                     \
        switch (TYPE) {                                                        \
        case s390_minmax_java_math_min:                                        \
            return neg_a && !neg_b ? a : b;                                    \
        case s390_minmax_java_math_max:                                        \
        case s390_minmax_fmax:                                                 \
        case s390_minmax_cpp_alg_max:                                          \
            return neg_a && !neg_b ? b : a;                                    \
        case s390_minmax_c_macro_min:                                          \
        case s390_minmax_cpp_alg_min:                                          \
            return neg_b ? b : a;                                              \
        case s390_minmax_c_macro_max:                                          \
            return !neg_a && neg_b ? a : b;                                    \
        case s390_minmax_fmin:                                                 \
            return !neg_a && neg_b ? b : a;                                    \
        default:                                                               \
            g_assert_not_reached();                                            \
        }                                                                      \
    } else if (unlikely(zero_a && zero_b)) {                                   \
        switch (TYPE) {                                                        \
        case s390_minmax_java_math_min:                                        \
            return neg_a && !neg_b ? a : b;                                    \
        case s390_minmax_java_math_max:                                        \
        case s390_minmax_fmax:                                                 \
            return neg_a && !neg_b ? b : a;                                    \
        case s390_minmax_c_macro_min:                                          \
        case s390_minmax_c_macro_max:                                          \
            return b;                                                          \
        case s390_minmax_fmin:                                                 \
            return !neg_a && neg_b ? b : a;                                    \
        case s390_minmax_cpp_alg_min:                                          \
        case s390_minmax_cpp_alg_max:                                          \
            return a;                                                          \
        default:                                                               \
            g_assert_not_reached();                                            \
        }                                                                      \
    }                                                                          \
                                                                               \
    /* We can process all remaining cases using simple comparison. */          \
    switch (TYPE) {                                                            \
    case s390_minmax_java_math_min:                                            \
    case s390_minmax_c_macro_min:                                              \
    case s390_minmax_fmin:                                                     \
    case s390_minmax_cpp_alg_min:                                              \
        if (float##BITS##_le_quiet(a, b, s)) {                                 \
            return a;                                                          \
        }                                                                      \
        return b;                                                              \
    case s390_minmax_java_math_max:                                            \
    case s390_minmax_c_macro_max:                                              \
    case s390_minmax_fmax:                                                     \
    case s390_minmax_cpp_alg_max:                                              \
        if (float##BITS##_le_quiet(a, b, s)) {                                 \
            return b;                                                          \
        }                                                                      \
        return a;                                                              \
    default:                                                                   \
        g_assert_not_reached();                                                \
    }                                                                          \
}

#define S390_MINMAX_ABS(BITS, TYPE)                                            \
static float##BITS TYPE##_abs##BITS(float##BITS a, float##BITS b,              \
                                    float_status *s)                           \
{                                                                              \
    return TYPE##BITS(float##BITS##_abs(a), float##BITS##_abs(b), s);          \
}

S390_MINMAX(32, s390_minmax_java_math_min)
S390_MINMAX(32, s390_minmax_java_math_max)
S390_MINMAX(32, s390_minmax_c_macro_min)
S390_MINMAX(32, s390_minmax_c_macro_max)
S390_MINMAX(32, s390_minmax_fmin)
S390_MINMAX(32, s390_minmax_fmax)
S390_MINMAX(32, s390_minmax_cpp_alg_min)
S390_MINMAX(32, s390_minmax_cpp_alg_max)
S390_MINMAX_ABS(32, s390_minmax_java_math_min)
S390_MINMAX_ABS(32, s390_minmax_java_math_max)
S390_MINMAX_ABS(32, s390_minmax_c_macro_min)
S390_MINMAX_ABS(32, s390_minmax_c_macro_max)
S390_MINMAX_ABS(32, s390_minmax_fmin)
S390_MINMAX_ABS(32, s390_minmax_fmax)
S390_MINMAX_ABS(32, s390_minmax_cpp_alg_min)
S390_MINMAX_ABS(32, s390_minmax_cpp_alg_max)

S390_MINMAX(64, s390_minmax_java_math_min)
S390_MINMAX(64, s390_minmax_java_math_max)
S390_MINMAX(64, s390_minmax_c_macro_min)
S390_MINMAX(64, s390_minmax_c_macro_max)
S390_MINMAX(64, s390_minmax_fmin)
S390_MINMAX(64, s390_minmax_fmax)
S390_MINMAX(64, s390_minmax_cpp_alg_min)
S390_MINMAX(64, s390_minmax_cpp_alg_max)
S390_MINMAX_ABS(64, s390_minmax_java_math_min)
S390_MINMAX_ABS(64, s390_minmax_java_math_max)
S390_MINMAX_ABS(64, s390_minmax_c_macro_min)
S390_MINMAX_ABS(64, s390_minmax_c_macro_max)
S390_MINMAX_ABS(64, s390_minmax_fmin)
S390_MINMAX_ABS(64, s390_minmax_fmax)
S390_MINMAX_ABS(64, s390_minmax_cpp_alg_min)
S390_MINMAX_ABS(64, s390_minmax_cpp_alg_max)

S390_MINMAX(128, s390_minmax_java_math_min)
S390_MINMAX(128, s390_minmax_java_math_max)
S390_MINMAX(128, s390_minmax_c_macro_min)
S390_MINMAX(128, s390_minmax_c_macro_max)
S390_MINMAX(128, s390_minmax_fmin)
S390_MINMAX(128, s390_minmax_fmax)
S390_MINMAX(128, s390_minmax_cpp_alg_min)
S390_MINMAX(128, s390_minmax_cpp_alg_max)
S390_MINMAX_ABS(128, s390_minmax_java_math_min)
S390_MINMAX_ABS(128, s390_minmax_java_math_max)
S390_MINMAX_ABS(128, s390_minmax_c_macro_min)
S390_MINMAX_ABS(128, s390_minmax_c_macro_max)
S390_MINMAX_ABS(128, s390_minmax_fmin)
S390_MINMAX_ABS(128, s390_minmax_fmax)
S390_MINMAX_ABS(128, s390_minmax_cpp_alg_min)
S390_MINMAX_ABS(128, s390_minmax_cpp_alg_max)

static vop32_3_fn const vfmax_fns32[16] = {
    [0] = float32_maxnum,
    [1] = s390_minmax_java_math_max32,
    [2] = s390_minmax_c_macro_max32,
    [3] = s390_minmax_cpp_alg_max32,
    [4] = s390_minmax_fmax32,
    [8] = float32_maxnummag,
    [9] = s390_minmax_java_math_max_abs32,
    [10] = s390_minmax_c_macro_max_abs32,
    [11] = s390_minmax_cpp_alg_max_abs32,
    [12] = s390_minmax_fmax_abs32,
};

static vop64_3_fn const vfmax_fns64[16] = {
    [0] = float64_maxnum,
    [1] = s390_minmax_java_math_max64,
    [2] = s390_minmax_c_macro_max64,
    [3] = s390_minmax_cpp_alg_max64,
    [4] = s390_minmax_fmax64,
    [8] = float64_maxnummag,
    [9] = s390_minmax_java_math_max_abs64,
    [10] = s390_minmax_c_macro_max_abs64,
    [11] = s390_minmax_cpp_alg_max_abs64,
    [12] = s390_minmax_fmax_abs64,
};

static vop128_3_fn const vfmax_fns128[16] = {
    [0] = float128_maxnum,
    [1] = s390_minmax_java_math_max128,
    [2] = s390_minmax_c_macro_max128,
    [3] = s390_minmax_cpp_alg_max128,
    [4] = s390_minmax_fmax128,
    [8] = float128_maxnummag,
    [9] = s390_minmax_java_math_max_abs128,
    [10] = s390_minmax_c_macro_max_abs128,
    [11] = s390_minmax_cpp_alg_max_abs128,
    [12] = s390_minmax_fmax_abs128,
};

#define DEF_GVEC_VFMAX(BITS)                                                   \
void HELPER(gvec_vfmax##BITS)(void *v1, const void *v2, const void *v3,        \
                              CPUS390XState *env, uint32_t desc)               \
{                                                                              \
    vop##BITS##_3_fn fn = vfmax_fns##BITS[simd_data(desc)];                    \
                                                                               \
    g_assert(fn);                                                              \
    vop##BITS##_3(v1, v2, v3, env, false, fn, GETPC());                        \
}
DEF_GVEC_VFMAX(32)
DEF_GVEC_VFMAX(64)
DEF_GVEC_VFMAX(128)

#define DEF_GVEC_VFMAX_S(BITS)                                                 \
void HELPER(gvec_vfmax##BITS##s)(void *v1, const void *v2, const void *v3,     \
                                 CPUS390XState *env, uint32_t desc)            \
{                                                                              \
    vop##BITS##_3_fn fn = vfmax_fns##BITS[simd_data(desc)];                    \
                                                                               \
    g_assert(fn);                                                              \
    vop##BITS##_3(v1, v2, v3, env, true, fn, GETPC());                         \
}
DEF_GVEC_VFMAX_S(32)
DEF_GVEC_VFMAX_S(64)

static vop32_3_fn const vfmin_fns32[16] = {
    [0] = float32_minnum,
    [1] = s390_minmax_java_math_min32,
    [2] = s390_minmax_c_macro_min32,
    [3] = s390_minmax_cpp_alg_min32,
    [4] = s390_minmax_fmin32,
    [8] = float32_minnummag,
    [9] = s390_minmax_java_math_min_abs32,
    [10] = s390_minmax_c_macro_min_abs32,
    [11] = s390_minmax_cpp_alg_min_abs32,
    [12] = s390_minmax_fmin_abs32,
};

static vop64_3_fn const vfmin_fns64[16] = {
    [0] = float64_minnum,
    [1] = s390_minmax_java_math_min64,
    [2] = s390_minmax_c_macro_min64,
    [3] = s390_minmax_cpp_alg_min64,
    [4] = s390_minmax_fmin64,
    [8] = float64_minnummag,
    [9] = s390_minmax_java_math_min_abs64,
    [10] = s390_minmax_c_macro_min_abs64,
    [11] = s390_minmax_cpp_alg_min_abs64,
    [12] = s390_minmax_fmin_abs64,
};

static vop128_3_fn const vfmin_fns128[16] = {
    [0] = float128_minnum,
    [1] = s390_minmax_java_math_min128,
    [2] = s390_minmax_c_macro_min128,
    [3] = s390_minmax_cpp_alg_min128,
    [4] = s390_minmax_fmin128,
    [8] = float128_minnummag,
    [9] = s390_minmax_java_math_min_abs128,
    [10] = s390_minmax_c_macro_min_abs128,
    [11] = s390_minmax_cpp_alg_min_abs128,
    [12] = s390_minmax_fmin_abs128,
};

#define DEF_GVEC_VFMIN(BITS)                                                   \
void HELPER(gvec_vfmin##BITS)(void *v1, const void *v2, const void *v3,        \
                              CPUS390XState *env, uint32_t desc)               \
{                                                                              \
    vop##BITS##_3_fn fn = vfmin_fns##BITS[simd_data(desc)];                    \
                                                                               \
    g_assert(fn);                                                              \
    vop##BITS##_3(v1, v2, v3, env, false, fn, GETPC());                        \
}
DEF_GVEC_VFMIN(32)
DEF_GVEC_VFMIN(64)
DEF_GVEC_VFMIN(128)

#define DEF_GVEC_VFMIN_S(BITS)                                                 \
void HELPER(gvec_vfmin##BITS##s)(void *v1, const void *v2, const void *v3,     \
                                 CPUS390XState *env, uint32_t desc)            \
{                                                                              \
    vop##BITS##_3_fn fn = vfmin_fns##BITS[simd_data(desc)];                    \
                                                                               \
    g_assert(fn);                                                              \
    vop##BITS##_3(v1, v2, v3, env, true, fn, GETPC());                         \
}
DEF_GVEC_VFMIN_S(32)
DEF_GVEC_VFMIN_S(64)
