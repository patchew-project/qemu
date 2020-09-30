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

static void vfma64(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                   const S390Vector *v4, CPUS390XState *env, bool s, int flags,
                   uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        const uint64_t a = s390_vec_read_element64(v2, i);
        const uint64_t b = s390_vec_read_element64(v3, i);
        const uint64_t c = s390_vec_read_element64(v4, i);
        uint64_t ret = float64_muladd(a, b, c, flags, &env->fpu_status);

        s390_vec_write_element64(&tmp, i, ret);
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

void HELPER(gvec_vfma64)(void *v1, const void *v2, const void *v3,
                         const void *v4, CPUS390XState *env, uint32_t desc)
{
    vfma64(v1, v2, v3, v4, env, false, 0, GETPC());
}

void HELPER(gvec_vfma64s)(void *v1, const void *v2, const void *v3,
                         const void *v4, CPUS390XState *env, uint32_t desc)
{
    vfma64(v1, v2, v3, v4, env, true, 0, GETPC());
}

void HELPER(gvec_vfms64)(void *v1, const void *v2, const void *v3,
                         const void *v4, CPUS390XState *env, uint32_t desc)
{
    vfma64(v1, v2, v3, v4, env, false, float_muladd_negate_c, GETPC());
}

void HELPER(gvec_vfms64s)(void *v1, const void *v2, const void *v3,
                         const void *v4, CPUS390XState *env, uint32_t desc)
{
    vfma64(v1, v2, v3, v4, env, true, float_muladd_negate_c, GETPC());
}

static uint64_t vfsq64(uint64_t a, float_status *s)
{
    return float64_sqrt(a, s);
}

void HELPER(gvec_vfsq64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    vop64_2(v1, v2, env, false, false, 0, vfsq64, GETPC());
}

void HELPER(gvec_vfsq64s)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    vop64_2(v1, v2, env, true, false, 0, vfsq64, GETPC());
}

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

static int vftci64(S390Vector *v1, const S390Vector *v2, CPUS390XState *env,
                   bool s, uint16_t i3)
{
    int i, match = 0;

    for (i = 0; i < 2; i++) {
        float64 a = s390_vec_read_element64(v2, i);

        if (float64_dcmask(env, a) & i3) {
            match++;
            s390_vec_write_element64(v1, i, -1ull);
        } else {
            s390_vec_write_element64(v1, i, 0);
        }
        if (s) {
            break;
        }
    }

    if (match) {
        return s || match == 2 ? 0 : 1;
    }
    return 3;
}

void HELPER(gvec_vftci64)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    env->cc_op = vftci64(v1, v2, env, false, simd_data(desc));
}

void HELPER(gvec_vftci64s)(void *v1, const void *v2, CPUS390XState *env,
                           uint32_t desc)
{
    env->cc_op = vftci64(v1, v2, env, true, simd_data(desc));
}
