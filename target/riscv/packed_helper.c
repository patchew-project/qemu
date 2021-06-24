/*
 * RISC-V P Extension Helpers for QEMU.
 *
 * Copyright (c) 2021 T-Head Semiconductor Co., Ltd. All rights reserved.
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "fpu/softfloat.h"
#include <math.h>
#include "internals.h"

/*
 *** SIMD Data Processing Instructions
 */

/* 16-bit Addition & Subtraction Instructions */
typedef void PackedFn3i(CPURISCVState *, void *, void *, void *, uint8_t);

/* Define a common function to loop elements in packed register */
static inline target_ulong
rvpr(CPURISCVState *env, target_ulong a, target_ulong b,
     uint8_t step, uint8_t size, PackedFn3i *fn)
{
    int i, passes = sizeof(target_ulong) / size;
    target_ulong result = 0;

    for (i = 0; i < passes; i += step) {
        fn(env, &result, &a, &b, i);
    }
    return result;
}

#define RVPR(NAME, STEP, SIZE)                                  \
target_ulong HELPER(NAME)(CPURISCVState *env, target_ulong a,   \
                          target_ulong b)                       \
{                                                               \
    return rvpr(env, a, b, STEP, SIZE, (PackedFn3i *)do_##NAME);\
}

static inline int32_t hadd32(int32_t a, int32_t b)
{
    return ((int64_t)a + b) >> 1;
}

static inline void do_radd16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = hadd32(a[i], b[i]);
}

RVPR(radd16, 1, 2);

static inline uint32_t haddu32(uint32_t a, uint32_t b)
{
    return ((uint64_t)a + b) >> 1;
}

static inline void do_uradd16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = haddu32(a[i], b[i]);
}

RVPR(uradd16, 1, 2);

static inline void do_kadd16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = sadd16(env, 0, a[i], b[i]);
}

RVPR(kadd16, 1, 2);

static inline void do_ukadd16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = saddu16(env, 0, a[i], b[i]);
}

RVPR(ukadd16, 1, 2);

static inline int32_t hsub32(int32_t a, int32_t b)
{
    return ((int64_t)a - b) >> 1;
}

static inline int64_t hsub64(int64_t a, int64_t b)
{
    int64_t res = a - b;
    int64_t over = (res ^ a) & (a ^ b) & INT64_MIN;

    /* With signed overflow, bit 64 is inverse of bit 63. */
    return (res >> 1) ^ over;
}

static inline void do_rsub16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = hsub32(a[i], b[i]);
}

RVPR(rsub16, 1, 2);

static inline uint64_t hsubu64(uint64_t a, uint64_t b)
{
    return (a - b) >> 1;
}

static inline void do_ursub16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = hsubu64(a[i], b[i]);
}

RVPR(ursub16, 1, 2);

static inline void do_ksub16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[i] = ssub16(env, 0, a[i], b[i]);
}

RVPR(ksub16, 1, 2);

static inline void do_uksub16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[i] = ssubu16(env, 0, a[i], b[i]);
}

RVPR(uksub16, 1, 2);

static inline void do_cras16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = a[H2(i)] - b[H2(i + 1)];
    d[H2(i + 1)] = a[H2(i + 1)] + b[H2(i)];
}

RVPR(cras16, 2, 2);

static inline void do_rcras16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hsub32(a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = hadd32(a[H2(i + 1)], b[H2(i)]);
}

RVPR(rcras16, 2, 2);

static inline void do_urcras16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hsubu64(a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = haddu32(a[H2(i + 1)], b[H2(i)]);
}

RVPR(urcras16, 2, 2);

static inline void do_kcras16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = ssub16(env, 0, a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = sadd16(env, 0, a[H2(i + 1)], b[H2(i)]);
}

RVPR(kcras16, 2, 2);

static inline void do_ukcras16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = ssubu16(env, 0, a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = saddu16(env, 0, a[H2(i + 1)], b[H2(i)]);
}

RVPR(ukcras16, 2, 2);

static inline void do_crsa16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = a[H2(i)] + b[H2(i + 1)];
    d[H2(i + 1)] = a[H2(i + 1)] - b[H2(i)];
}

RVPR(crsa16, 2, 2);

static inline void do_rcrsa16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hadd32(a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = hsub32(a[H2(i + 1)], b[H2(i)]);
}

RVPR(rcrsa16, 2, 2);

static inline void do_urcrsa16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = haddu32(a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = hsubu64(a[H2(i + 1)], b[H2(i)]);
}

RVPR(urcrsa16, 2, 2);

static inline void do_kcrsa16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = sadd16(env, 0, a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = ssub16(env, 0, a[H2(i + 1)], b[H2(i)]);
}

RVPR(kcrsa16, 2, 2);

static inline void do_ukcrsa16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = saddu16(env, 0, a[H2(i)], b[H2(i + 1)]);
    d[H2(i + 1)] = ssubu16(env, 0, a[H2(i + 1)], b[H2(i)]);
}

RVPR(ukcrsa16, 2, 2);

static inline void do_stas16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = a[H2(i)] - b[H2(i)];
    d[H2(i + 1)] = a[H2(i + 1)] + b[H2(i + 1)];
}

RVPR(stas16, 2, 2);

static inline void do_rstas16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hsub32(a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = hadd32(a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(rstas16, 2, 2);

static inline void do_urstas16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hsubu64(a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = haddu32(a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(urstas16, 2, 2);

static inline void do_kstas16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = ssub16(env, 0, a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = sadd16(env, 0, a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(kstas16, 2, 2);

static inline void do_ukstas16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = ssubu16(env, 0, a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = saddu16(env, 0, a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(ukstas16, 2, 2);

static inline void do_stsa16(CPURISCVState *env, void *vd, void *va,
                             void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = a[H2(i)] + b[H2(i)];
    d[H2(i + 1)] = a[H2(i + 1)] - b[H2(i + 1)];
}

RVPR(stsa16, 2, 2);

static inline void do_rstsa16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = hadd32(a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = hsub32(a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(rstsa16, 2, 2);

static inline void do_urstsa16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = haddu32(a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = hsubu64(a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(urstsa16, 2, 2);

static inline void do_kstsa16(CPURISCVState *env, void *vd, void *va,
                              void *vb, uint8_t i)
{
    int16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = sadd16(env, 0, a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = ssub16(env, 0, a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(kstsa16, 2, 2);

static inline void do_ukstsa16(CPURISCVState *env, void *vd, void *va,
                               void *vb, uint8_t i)
{
    uint16_t *d = vd, *a = va, *b = vb;
    d[H2(i)] = saddu16(env, 0, a[H2(i)], b[H2(i)]);
    d[H2(i + 1)] = ssubu16(env, 0, a[H2(i + 1)], b[H2(i + 1)]);
}

RVPR(ukstsa16, 2, 2);
