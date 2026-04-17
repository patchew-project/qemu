/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RISC-V Packed SIMD Extension Helpers for QEMU. */
/* Copyright (C) 2026 ISRC ISCAS. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "internals.h"


/* Helper macros */

/* Element count calculations */
#define ELEMS_B(target) (sizeof(target) * 8 / 8)    /* byte elements count */
#define ELEMS_H(target) (sizeof(target) * 8 / 16)
#define ELEMS_W(target) (sizeof(target) * 8 / 32)   /* word elements count */

/* Element extraction macros - unsigned to avoid sign extension */
#define EXTRACT8(val, idx)  (((val) >> ((idx) * 8)) & 0xFF)
#define EXTRACT16(val, idx) (((val) >> ((idx) * 16)) & 0xFFFF)
#define EXTRACT32(val, idx) (((val) >> ((idx) * 32)) & 0xFFFFFFFF)

/* Element insertion macros */
#define INSERT8(val, res, idx) \
    ((val) | ((target_ulong)(uint8_t)(res) << ((idx) * 8)))
#define INSERT16(val, res, idx) \
    ((val) | ((target_ulong)(uint16_t)(res) << ((idx) * 16)))
#define INSERT32(val, res, idx) \
    ((val) | ((target_ulong)(uint32_t)(res) << ((idx) * 32)))

/* Saturation constants */
static const int8_t   SAT_MAX_B = 127;
static const int8_t   SAT_MIN_B = -128;
static const int16_t  SAT_MAX_H = 32767;
static const int16_t  SAT_MIN_H = -32768;
static const int32_t  SAT_MAX_W = 2147483647;
static const int32_t  SAT_MIN_W = -2147483648LL;
static const uint8_t  USAT_MAX_B = 255;
static const uint16_t USAT_MAX_H = 65535;
static const uint32_t USAT_MAX_W = 4294967295U;


/* Saturation helper functions */

/**
 * Signed saturation for 8-bit elements
 * Returns saturated value and sets *sat if saturation occurred
 */
static inline int8_t signed_saturate_b(int32_t val, int *sat)
{
    if (val > SAT_MAX_B) {
        *sat = 1;
        return SAT_MAX_B;
    }
    if (val < SAT_MIN_B) {
        *sat = 1;
        return SAT_MIN_B;
    }
    return (int8_t)val;
}

/**
 * Signed saturation for 16-bit elements
 */
static inline int16_t signed_saturate_h(int32_t val, int *sat)
{
    if (val > SAT_MAX_H) {
        *sat = 1;
        return SAT_MAX_H;
    }
    if (val < SAT_MIN_H) {
        *sat = 1;
        return SAT_MIN_H;
    }
    return (int16_t)val;
}

/**
 * Signed saturation for 32-bit elements
 */
static inline int32_t signed_saturate_w(int64_t val, int *sat)
{
    if (val > SAT_MAX_W) {
        *sat = 1;
        return SAT_MAX_W;
    }
    if (val < SAT_MIN_W) {
        *sat = 1;
        return SAT_MIN_W;
    }
    return (int32_t)val;
}

/**
 * Unsigned saturation for 8-bit elements
 */
static inline uint8_t unsigned_saturate_b(uint32_t val, int *sat)
{
    if (val > USAT_MAX_B) {
        *sat = 1;
        return USAT_MAX_B;
    }
    return (uint8_t)val;
}

/**
 * Unsigned saturation for 16-bit elements
 */
static inline uint16_t unsigned_saturate_h(uint32_t val, int *sat)
{
    if (val > USAT_MAX_H) {
        *sat = 1;
        return USAT_MAX_H;
    }
    return (uint16_t)val;
}

/**
 * Unsigned saturation for 32-bit elements
 */
static inline uint32_t unsigned_saturate_w(uint64_t val, int *sat)
{
    if (val > USAT_MAX_W) {
        *sat = 1;
        return USAT_MAX_W;
    }
    return (uint32_t)val;
}

/* Basic addition operations (non-saturating) */

/**
 * PADD.B - Packed 8-bit addition
 * For each byte: rd[i] = rs1[i] + rs2[i] (modular)
 */
target_ulong HELPER(padd_b)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t res = e1 + e2;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PADD.H - Packed 16-bit addition
 * For each halfword: rd[i] = rs1[i] + rs2[i] (modular)
 */
target_ulong HELPER(padd_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = e1 + e2;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PADD.W - Packed 32-bit addition (RV64 only)
 * For each word: rd[i] = rs1[i] + rs2[i] (modular)
 */
uint64_t HELPER(padd_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;  /* 2 words in 64-bit */

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = e1 + e2;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PADD.BS - Packed 8-bit addition with scalar second operand
 * For each byte: rd[i] = rs1[i] + rs2[0] (modular)
 */
target_ulong HELPER(padd_bs)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    uint8_t e2 = EXTRACT8(rs2, 0);  /* Scalar, take least significant byte */

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t res = e1 + e2;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PADD.HS - Packed 16-bit addition with scalar second operand
 * For each halfword: rd[i] = rs1[i] + rs2[0] (modular)
 */
target_ulong HELPER(padd_hs)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint16_t e2 = EXTRACT16(rs2, 0);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t res = e1 + e2;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PADD.WS - Packed 32-bit addition with scalar second operand (RV64 only)
 * For each word: rd[i] = rs1[i] + rs2[0] (modular)
 */
uint64_t HELPER(padd_ws)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    uint32_t e2 = EXTRACT32(rs2, 0);

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t res = e1 + e2;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}


/* Basic subtraction operations (non-saturating) */

/**
 * PSUB.B - Packed 8-bit subtraction
 * For each byte: rd[i] = rs1[i] - rs2[i] (modular)
 */
target_ulong HELPER(psub_b)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t res = e1 - e2;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PSUB.H - Packed 16-bit subtraction
 * For each halfword: rd[i] = rs1[i] - rs2[i] (modular)
 */
target_ulong HELPER(psub_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = e1 - e2;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PSUB.W - Packed 32-bit subtraction (RV64 only)
 * For each word: rd[i] = rs1[i] - rs2[i] (modular)
 */
uint64_t HELPER(psub_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = e1 - e2;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/* Shift-left-by-one and add operations */

/**
 * PSH1ADD.H - Shift left by 1 and add (16-bit)
 * For each halfword: rd[i] = (rs1[i] << 1) + rs2[i]
 */
target_ulong HELPER(psh1add_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = (e1 << 1) + e2;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PSH1ADD.W - Shift left by 1 and add (32-bit, RV64 only)
 * For each word: rd[i] = (rs1[i] << 1) + rs2[i]
 */
uint64_t HELPER(psh1add_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = (e1 << 1) + e2;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PSSH1SADD.H - Saturating shift left by 1 and saturating add (16-bit)
 * For each halfword: rd[i] = sat16(sat16(rs1[i] << 1) + rs2[i])
 */
target_ulong HELPER(pssh1sadd_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int32_t shifted;

        /* Check if shift-left-1 would overflow */
        if (e1 > 0x3FFF || e1 < -0x4000) {
            shifted = (e1 < 0) ? 0xFFFF8000LL : 0x7FFF;
            sat = 1;
        } else {
            shifted = e1 << 1;
        }

        int32_t sum = shifted + e2;
        int16_t res = signed_saturate_h(sum, &sat);
        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSH1SADD.W - Saturating shift left by 1 and add
 * with saturation (32-bit, RV64 only)
 * For each word: rd[i] = sat32(sat32(rs1[i] << 1) + rs2[i])
 */
uint64_t HELPER(pssh1sadd_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int64_t shifted;

        /* Check if shift-left-1 would overflow */
        if (e1 > 0x3FFFFFFF || e1 < -0x40000000) {
            shifted = (e1 < 0) ? 0xFFFFFFFF80000000LL : 0x7FFFFFFF;
            sat = 1;
        } else {
            shifted = (int64_t)e1 << 1;
        }

        int64_t sum = shifted + e2;
        int32_t res = signed_saturate_w(sum, &sat);
        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * SSH1SADD - 32-bit scalar saturating shift left by 1 and saturating add
 */
uint32_t HELPER(ssh1sadd)(CPURISCVState *env,
                     uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int32_t b = (int32_t)rs2;
    int64_t shifted;
    int sat = 0;

    /* Check if shift-left-1 would overflow */
    if (a > 0x3FFFFFFF || a < -0x40000000) {
        shifted = (a < 0) ? 0xFFFFFFFF80000000LL : 0x7FFFFFFF;
        sat = 1;
    } else {
        shifted = (int64_t)a << 1;
    }

    int64_t sum = shifted + b;
    int32_t res = signed_saturate_w(sum, &sat);

    if (sat) {
        env->vxsat = 1;
    }
    return (uint32_t)res;
}

/* Saturating addition operations */

/**
 * PSADD.B - Packed 8-bit signed saturating addition
 * For each byte: rd[i] = sat8(rs1[i] + rs2[i])
 */
target_ulong HELPER(psadd_b)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i);
        int32_t sum = (int32_t)e1 + (int32_t)e2;
        int8_t res = signed_saturate_b(sum, &sat);
        rd = INSERT8(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSADD.H - Packed 16-bit signed saturating addition
 * For each halfword: rd[i] = sat16(rs1[i] + rs2[i])
 */
target_ulong HELPER(psadd_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int32_t sum = (int32_t)e1 + (int32_t)e2;
        int16_t res = signed_saturate_h(sum, &sat);
        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSADD.W - Packed 32-bit signed saturating addition (RV64 only)
 * For each word: rd[i] = sat32(rs1[i] + rs2[i])
 */
uint64_t HELPER(psadd_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int64_t sum = (int64_t)e1 + (int64_t)e2;
        int32_t res = signed_saturate_w(sum, &sat);
        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSADDU.B - Packed 8-bit unsigned saturating addition
 * For each byte: rd[i] = usat8(rs1[i] + rs2[i])
 */
target_ulong HELPER(psaddu_b)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint32_t sum = (uint32_t)e1 + (uint32_t)e2;
        uint8_t res = unsigned_saturate_b(sum, &sat);
        rd = INSERT8(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSADDU.H - Packed 16-bit unsigned saturating addition
 * For each halfword: rd[i] = usat16(rs1[i] + rs2[i])
 */
target_ulong HELPER(psaddu_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint32_t sum = (uint32_t)e1 + (uint32_t)e2;
        uint16_t res = unsigned_saturate_h(sum, &sat);
        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSADDU.W - Packed 32-bit unsigned saturating addition (RV64 only)
 * For each word: rd[i] = usat32(rs1[i] + rs2[i])
 */
uint64_t HELPER(psaddu_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint64_t sum = (uint64_t)e1 + (uint64_t)e2;
        uint32_t res = unsigned_saturate_w(sum, &sat);
        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * SADD - 32-bit signed saturating addition
 */
uint32_t HELPER(sadd)(CPURISCVState *env,
                     uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int32_t b = (int32_t)rs2;
    int64_t sum = (int64_t)a + (int64_t)b;
    int sat = 0;
    int32_t res = signed_saturate_w(sum, &sat);

    if (sat) {
        env->vxsat = 1;
    }
    return (uint32_t)res;
}

/**
 * SADDU - 32-bit unsigned saturating addition
 */
uint32_t HELPER(saddu)(CPURISCVState *env,
                     uint32_t rs1, uint32_t rs2)
{
    uint32_t a = rs1;
    uint32_t b = rs2;
    uint64_t sum = (uint64_t)a + (uint64_t)b;
    int sat = 0;
    uint32_t res = unsigned_saturate_w(sum, &sat);

    if (sat) {
        env->vxsat = 1;
    }
    return res;
}

/* Saturating subtraction operations */

/**
 * PSSUB.B - Packed 8-bit signed saturating subtraction
 * For each byte: rd[i] = sat8(rs1[i] - rs2[i])
 */
target_ulong HELPER(pssub_b)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i);
        int32_t diff = (int32_t)e1 - (int32_t)e2;
        int8_t res = signed_saturate_b(diff, &sat);
        rd = INSERT8(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSUB.H - Packed 16-bit signed saturating subtraction
 * For each halfword: rd[i] = sat16(rs1[i] - rs2[i])
 */
target_ulong HELPER(pssub_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int32_t diff = (int32_t)e1 - (int32_t)e2;
        int16_t res = signed_saturate_h(diff, &sat);
        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSUB.W - Packed 32-bit signed saturating subtraction (RV64 only)
 * For each word: rd[i] = sat32(rs1[i] - rs2[i])
 */
uint64_t HELPER(pssub_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int64_t diff = (int64_t)e1 - (int64_t)e2;
        int32_t res = signed_saturate_w(diff, &sat);
        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSUBU.B - Packed 8-bit unsigned saturating subtraction
 * For each byte: rd[i] = usat8(rs1[i] - rs2[i])
 */
target_ulong HELPER(pssubu_b)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint32_t diff = e1 - e2;  /* Unsigned subtraction may underflow */
        uint8_t res = unsigned_saturate_b(diff, &sat);
        rd = INSERT8(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSUBU.H - Packed 16-bit unsigned saturating subtraction
 * For each halfword: rd[i] = usat16(rs1[i] - rs2[i])
 */
target_ulong HELPER(pssubu_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint32_t diff = e1 - e2;
        uint16_t res = unsigned_saturate_h(diff, &sat);
        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSUBU.W - Packed 32-bit unsigned saturating subtraction (RV64 only)
 * For each word: rd[i] = usat32(rs1[i] - rs2[i])
 */
uint64_t HELPER(pssubu_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = (e1 >= e2) ? (e1 - e2) : 0;
        if (e1 < e2) {
            sat = 1;
        }
        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * SSUB - 32-bit signed saturating subtraction
 */
uint32_t HELPER(ssub)(CPURISCVState *env,
                     uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int32_t b = (int32_t)rs2;
    int64_t diff = (int64_t)a - (int64_t)b;
    int sat = 0;
    int32_t res = signed_saturate_w(diff, &sat);

    if (sat) {
        env->vxsat = 1;
    }
    return (uint32_t)res;
}

/**
 * SSUBU - 32-bit unsigned saturating subtraction
 */
uint32_t HELPER(ssubu)(CPURISCVState *env,
                     uint32_t rs1, uint32_t rs2)
{
    uint32_t a = rs1;
    uint32_t b = rs2;
    uint64_t diff = (uint64_t)a - (uint64_t)b;
    int sat = 0;
    uint32_t res = unsigned_saturate_w(diff, &sat);

    if (sat) {
        env->vxsat = 1;
    }
    return res;
}

/* Saturation instructions (SAT, USAT) */

/**
 * PSATI.H - Packed 16-bit signed saturate to immediate bit-width
 * For each halfword: rd[i] = sat(rs1[i], imm+1 bits)
 */
target_ulong HELPER(psati_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int range = (imm & 0x0F) + 1;  /* imm specifies bits-1 */
    int64_t max = (1LL << (range - 1)) - 1;
    int64_t min = -(1LL << (range - 1));
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t res;

        if (e1 > max) {
            res = max;
            sat = 1;
        } else if (e1 < min) {
            res = min;
            sat = 1;
        } else {
            res = e1;
        }

        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PUSATI.H - Packed 16-bit unsigned saturate to immediate bit-width
 * For each halfword: rd[i] = usat(rs1[i], imm bits)
 */
target_ulong HELPER(pusati_h)(CPURISCVState *env,
                          target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint32_t max = (1U << imm) - 1;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t res;

        if (e1 < 0) {
            res = 0;
            sat = 1;
        } else if ((uint16_t)e1 > max) {
            res = max;
            sat = 1;
        } else {
            res = e1;
        }

        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSATI.W - Packed 32-bit signed saturate to immediate bit-width (RV64 only)
 * For each word: rd[i] = sat(rs1[i], imm+1 bits)
 */
uint64_t HELPER(psati_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t imm)
{
    uint64_t rd = 0;
    int elems = 2;
    int range = (imm & 0x1F) + 1;
    int64_t max = (1LL << (range - 1)) - 1;
    int64_t min = -(1LL << (range - 1));
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t res;

        if (e1 > max) {
            res = max;
            sat = 1;
        } else if (e1 < min) {
            res = min;
            sat = 1;
        } else {
            res = e1;
        }

        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PUSATI.W - Packed 32-bit unsigned saturate to immediate bit-width (RV64 only)
 * For each word: rd[i] = usat(rs1[i], imm bits)
 */
uint64_t HELPER(pusati_w)(CPURISCVState *env,
                     uint64_t rs1, uint64_t imm)
{
    uint64_t rd = 0;
    int elems = 2;
    uint64_t max = (1ULL << imm) - 1;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t res;

        if (e1 < 0) {
            res = 0;
            sat = 1;
        } else if ((uint32_t)e1 > max) {
            res = max;
            sat = 1;
        } else {
            res = e1;
        }

        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * SATI_32 - 32-bit scalar signed saturation with immediate range
 */
uint32_t HELPER(sati_32)(CPURISCVState *env,
                         uint32_t rs1, uint32_t imm)
{
    int32_t a = (int32_t)rs1;
    int range = (imm & 0x1F) + 1;  /* imm specifies bits-1 */
    int64_t max = (1LL << (range - 1)) - 1;
    int64_t min = -(1LL << (range - 1));
    int sat = 0;

    if (a > max) {
        a = max;
        sat = 1;
    } else if (a < min) {
        a = min;
        sat = 1;
    }

    if (sat) {
        env->vxsat = 1;
    }
    return (uint32_t)a;
}

/**
 * USATI_32 - 32-bit scalar unsigned saturation with immediate range
 */
uint32_t HELPER(usati_32)(CPURISCVState *env,
                          uint32_t rs1, uint32_t imm)
{
    int32_t a = (int32_t)rs1;
    uint32_t max = (1U << imm) - 1;
    int sat = 0;

    if (a < 0) {
        a = 0;
        sat = 1;
    } else if ((uint32_t)a > max) {
        a = max;
        sat = 1;
    }

    if (sat) {
        env->vxsat = 1;
    }
    return (uint32_t)a;
}

/**
 * SATI_64 - 64-bit scalar signed saturation with immediate range
 */
uint64_t HELPER(sati_64)(CPURISCVState *env,
                     uint64_t rs1, uint64_t imm)
{
    int64_t a = (int64_t)rs1;
    int range = (imm & 0x3F) + 1;
    int64_t max = (1LL << (range - 1)) - 1;
    int64_t min = -(1LL << (range - 1));
    int sat = 0;

    if (a > max) {
        a = max;
        sat = 1;
    } else if (a < min) {
        a = min;
        sat = 1;
    }

    if (sat) {
        env->vxsat = 1;
    }
    return (uint64_t)a;
}

/**
 * USATI_64 - 64-bit scalar unsigned saturation with immediate range
 */
uint64_t HELPER(usati_64)(CPURISCVState *env,
                     uint64_t rs1, uint64_t imm)
{
    int64_t a = (int64_t)rs1;
    uint64_t max = (1ULL << imm) - 1;
    int sat = 0;

    if (a < 0) {
        a = 0;
        sat = 1;
    } else if ((uint64_t)a > max) {
        a = max;
        sat = 1;
    }

    if (sat) {
        env->vxsat = 1;
    }
    return (uint64_t)a;
}

/* Averaging Operations (non-saturating) */

/**
 * PAADD.B - Packed 8-bit signed averaging addition
 * For each byte: rd[i] = (rs1[i] + rs2[i]) >> 1
 */
target_ulong HELPER(paadd_b)(CPURISCVState *env, target_ulong rs1,
                             target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int8_t)EXTRACT8(rs1, i);
        int16_t e2 = (int8_t)EXTRACT8(rs2, i);
        int16_t avg = (e1 + e2) >> 1;
        rd = INSERT8(rd, (int8_t)avg, i);
    }
    return rd;
}

/**
 * PAADD.H - Packed 16-bit signed averaging addition
 * For each halfword: rd[i] = (rs1[i] + rs2[i]) >> 1
 */
target_ulong HELPER(paadd_h)(CPURISCVState *env, target_ulong rs1,
                             target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int16_t)EXTRACT16(rs1, i);
        int32_t e2 = (int16_t)EXTRACT16(rs2, i);
        int32_t avg = (e1 + e2) >> 1;
        rd = INSERT16(rd, (int16_t)avg, i);
    }
    return rd;
}

/**
 * PAADD.W - Packed 32-bit signed averaging addition (RV64 only)
 * For each word: rd[i] = (rs1[i] + rs2[i]) >> 1
 */
uint64_t HELPER(paadd_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int64_t e1 = (int32_t)EXTRACT32(rs1, i);
        int64_t e2 = (int32_t)EXTRACT32(rs2, i);
        int64_t avg = (e1 + e2) >> 1;
        rd = INSERT32(rd, (int32_t)avg, i);
    }
    return rd;
}

/**
 * PAADDU.B - Packed 8-bit unsigned averaging addition
 * For each byte: rd[i] = (rs1[i] + rs2[i]) >> 1
 */
target_ulong HELPER(paaddu_b)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT8(rs1, i);
        uint16_t e2 = EXTRACT8(rs2, i);
        uint16_t avg = (e1 + e2) >> 1;
        rd = INSERT8(rd, (uint8_t)avg, i);
    }
    return rd;
}

/**
 * PAADDU.H - Packed 16-bit unsigned averaging addition
 * For each halfword: rd[i] = (rs1[i] + rs2[i]) >> 1
 */
target_ulong HELPER(paaddu_h)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT16(rs1, i);
        uint32_t e2 = EXTRACT16(rs2, i);
        uint32_t avg = (e1 + e2) >> 1;
        rd = INSERT16(rd, (uint16_t)avg, i);
    }
    return rd;
}

/**
 * PAADDU.W - Packed 32-bit unsigned averaging addition (RV64 only)
 * For each word: rd[i] = (rs1[i] + rs2[i]) >> 1
 */
uint64_t HELPER(paaddu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint64_t e1 = EXTRACT32(rs1, i);
        uint64_t e2 = EXTRACT32(rs2, i);
        uint64_t avg = (e1 + e2) >> 1;
        rd = INSERT32(rd, (uint32_t)avg, i);
    }
    return rd;
}

/**
 * AADD - 32-bit signed averaging addition
 */
uint32_t HELPER(aadd)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int64_t a = (int32_t)rs1;
    int64_t b = (int32_t)rs2;
    return (uint32_t)((a + b) >> 1);
}

/**
 * AADDU - 32-bit unsigned averaging addition
 */
uint32_t HELPER(aaddu)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t a = rs1;
    uint64_t b = rs2;
    return (uint32_t)((a + b) >> 1);
}

/**
 * PASUB.B - Packed 8-bit signed averaging subtraction
 * For each byte: rd[i] = (rs1[i] - rs2[i]) >> 1
 */
target_ulong HELPER(pasub_b)(CPURISCVState *env, target_ulong rs1,
                             target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int8_t)EXTRACT8(rs1, i);
        int16_t e2 = (int8_t)EXTRACT8(rs2, i);
        int16_t avg = (e1 - e2) >> 1;
        rd = INSERT8(rd, (int8_t)avg, i);
    }
    return rd;
}

/**
 * PASUB.H - Packed 16-bit signed averaging subtraction
 * For each halfword: rd[i] = (rs1[i] - rs2[i]) >> 1
 */
target_ulong HELPER(pasub_h)(CPURISCVState *env, target_ulong rs1,
                             target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int16_t)EXTRACT16(rs1, i);
        int32_t e2 = (int16_t)EXTRACT16(rs2, i);
        int32_t avg = (e1 - e2) >> 1;
        rd = INSERT16(rd, (int16_t)avg, i);
    }
    return rd;
}

/**
 * PASUB.W - Packed 32-bit signed averaging subtraction (RV64 only)
 * For each word: rd[i] = (rs1[i] - rs2[i]) >> 1
 */
uint64_t HELPER(pasub_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int64_t e1 = (int32_t)EXTRACT32(rs1, i);
        int64_t e2 = (int32_t)EXTRACT32(rs2, i);
        int64_t avg = (e1 - e2) >> 1;
        rd = INSERT32(rd, (int32_t)avg, i);
    }
    return rd;
}

/**
 * PASUBU.B - Packed 8-bit unsigned averaging subtraction
 * For each byte: rd[i] = (rs1[i] - rs2[i]) >> 1
 */
target_ulong HELPER(pasubu_b)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT8(rs1, i);
        uint16_t e2 = EXTRACT8(rs2, i);
        uint16_t avg = (e1 - e2) >> 1;
        rd = INSERT8(rd, (uint8_t)avg, i);
    }
    return rd;
}

/**
 * PASUBU.H - Packed 16-bit unsigned averaging subtraction
 * For each halfword: rd[i] = (rs1[i] - rs2[i]) >> 1
 */
target_ulong HELPER(pasubu_h)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT16(rs1, i);
        uint32_t e2 = EXTRACT16(rs2, i);
        uint32_t avg = (e1 - e2) >> 1;
        rd = INSERT16(rd, (uint16_t)avg, i);
    }
    return rd;
}

/**
 * PASUBU.W - Packed 32-bit unsigned averaging subtraction (RV64 only)
 * For each word: rd[i] = (rs1[i] - rs2[i]) >> 1
 */
uint64_t HELPER(pasubu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint64_t e1 = EXTRACT32(rs1, i);
        uint64_t e2 = EXTRACT32(rs2, i);
        uint64_t avg = (e1 - e2) >> 1;
        rd = INSERT32(rd, (uint32_t)avg, i);
    }
    return rd;
}

/**
 * ASUB - 32-bit signed averaging subtraction
 */
uint32_t HELPER(asub)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int64_t a = (int32_t)rs1;
    int64_t b = (int32_t)rs2;
    return (uint32_t)((a - b) >> 1);
}

/**
 * ASUBU - 32-bit unsigned averaging subtraction
 */
uint32_t HELPER(asubu)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t a = rs1;
    uint64_t b = rs2;
    return (uint32_t)((a - b) >> 1);
}

/* Absolute value operations */

/**
 * PSABS.B - Packed 8-bit absolute value
 * For each byte: rd[i] = abs(rs1[i]), saturate if MIN
 */
target_ulong HELPER(psabs_b)(CPURISCVState *env, target_ulong rs1)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t res;

        if (e1 == INT8_MIN) {
            res = INT8_MAX;
            sat = 1;
        } else if (e1 < 0) {
            res = -e1;
        } else {
            res = e1;
        }

        rd = INSERT8(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSABS.H - Packed 16-bit absolute value
 * For each halfword: rd[i] = abs(rs1[i]), saturate if MIN
 */
target_ulong HELPER(psabs_h)(CPURISCVState *env, target_ulong rs1)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t res;

        if (e1 == INT16_MIN) {
            res = INT16_MAX;
            sat = 1;
        } else if (e1 < 0) {
            res = -e1;
        } else {
            res = e1;
        }

        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * ABS - 32/64-bit scalar absolute value
 */
target_ulong HELPER(abs)(CPURISCVState *env, target_ulong rs1)
{
    target_long a = (target_long)rs1;
    return (a < 0) ? (target_ulong)(-a) : rs1;
}

/**
 * ABSW - Absolute value of low 32 bits (RV64)
 */
uint64_t HELPER(absw)(CPURISCVState *env, uint64_t rs1)
{
    int32_t a = (int32_t)EXTRACT32(rs1, 0);
    uint32_t res;

    if (a == INT32_MIN) {
        res = 0x80000000;
    } else if (a < 0) {
        res = (uint32_t)(-a);
    } else {
        res = (uint32_t)a;
    }

    return (uint64_t)res;
}


/* Absolute difference operations */

/**
 * PABD.B - Packed 8-bit signed absolute difference
 * For each byte: rd[i] = |rs1[i] - rs2[i]|
 */
target_ulong HELPER(pabd_b)(CPURISCVState *env,
                            target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i);
        int16_t diff = (int16_t)e1 - (int16_t)e2;
        uint8_t res = (diff >= 0) ? (uint8_t)diff : (uint8_t)(-diff);
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PABDU.B - Packed 8-bit unsigned absolute difference
 * For each byte: rd[i] = |rs1[i] - rs2[i]|
 */
target_ulong HELPER(pabdu_b)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t res = (e1 > e2) ? (e1 - e2) : (e2 - e1);
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PABD.H - Packed 16-bit signed absolute difference
 * For each halfword: rd[i] = |rs1[i] - rs2[i]|
 */
target_ulong HELPER(pabd_h)(CPURISCVState *env,
                            target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int32_t diff = (int32_t)e1 - (int32_t)e2;
        uint16_t res = (diff >= 0) ? (uint16_t)diff : (uint16_t)(-diff);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PABDU.H - Packed 16-bit unsigned absolute difference
 * For each halfword: rd[i] = |rs1[i] - rs2[i]|
 */
target_ulong HELPER(pabdu_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = (e1 > e2) ? (e1 - e2) : (e2 - e1);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PABDSUMU.B - Sum of unsigned absolute differences
 * Returns sum(|rs1[i] - rs2[i]|) for all bytes
 */
target_ulong HELPER(pabdsumu_b)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    target_ulong sum = 0;
    int elems = ELEMS_B(rs1);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t diff = (e1 > e2) ? (e1 - e2) : (e2 - e1);
        sum += diff;
    }

    return sum;
}

/**
 * PABDSUMAU.B - Accumulated sum of unsigned absolute differences
 * rd = rd + sum(|rs1[i] - rs2[i]|)
 */
target_ulong HELPER(pabdsumau_b)(CPURISCVState *env, target_ulong rs1,
                                 target_ulong rs2, target_ulong rd)
{
    target_ulong sum = rd;
    int elems = ELEMS_B(rs1);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t diff = (e1 > e2) ? (e1 - e2) : (e2 - e1);
        sum += diff;
    }

    return sum;
}

/* Comparison operations (producing masks) */

/**
 * PMSEQ.B - Packed 8-bit equal comparison
 * For each byte: rd[i] = 0xFF if rs1[i] == rs2[i], else 0x00
 */
target_ulong HELPER(pmseq_b)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t res = (e1 == e2) ? 0xFF : 0x00;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PMSLT.B - Packed 8-bit signed less-than comparison
 * For each byte: rd[i] = 0xFF if rs1[i] < rs2[i], else 0x00
 */
target_ulong HELPER(pmslt_b)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i);
        uint8_t res = (e1 < e2) ? 0xFF : 0x00;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PMSLTU.B - Packed 8-bit unsigned less-than comparison
 * For each byte: rd[i] = 0xFF if rs1[i] < rs2[i], else 0x00
 */
target_ulong HELPER(pmsltu_b)(CPURISCVState *env,
                              target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t res = (e1 < e2) ? 0xFF : 0x00;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PMIN.B - Packed 8-bit signed minimum
 * For each byte: rd[i] = min(rs1[i], rs2[i])
 */
target_ulong HELPER(pmin_b)(CPURISCVState *env,
                            target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i);
        int8_t res = (e1 < e2) ? e1 : e2;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PMINU.B - Packed 8-bit unsigned minimum
 * For each byte: rd[i] = min(rs1[i], rs2[i])
 */
target_ulong HELPER(pminu_b)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t res = (e1 < e2) ? e1 : e2;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PMAX.B - Packed 8-bit signed maximum
 * For each byte: rd[i] = max(rs1[i], rs2[i])
 */
target_ulong HELPER(pmax_b)(CPURISCVState *env,
                            target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i);
        int8_t res = (e1 > e2) ? e1 : e2;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PMAXU.B - Packed 8-bit unsigned maximum
 * For each byte: rd[i] = max(rs1[i], rs2[i])
 */
target_ulong HELPER(pmaxu_b)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t res = (e1 > e2) ? e1 : e2;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PMSEQ.H - Packed 16-bit equal comparison
 * For each halfword: rd[i] = 0xFFFF if rs1[i] == rs2[i], else 0x0000
 */
target_ulong HELPER(pmseq_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = (e1 == e2) ? 0xFFFF : 0x0000;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMSLT.H - Packed 16-bit signed less-than comparison
 * For each halfword: rd[i] = 0xFFFF if rs1[i] < rs2[i], else 0x0000
 */
target_ulong HELPER(pmslt_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        uint16_t res = (e1 < e2) ? 0xFFFF : 0x0000;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMSLTU.H - Packed 16-bit unsigned less-than comparison
 * For each halfword: rd[i] = 0xFFFF if rs1[i] < rs2[i], else 0x0000
 */
target_ulong HELPER(pmsltu_h)(CPURISCVState *env,
                              target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = (e1 < e2) ? 0xFFFF : 0x0000;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMIN.H - Packed 16-bit signed minimum
 * For each halfword: rd[i] = min(rs1[i], rs2[i])
 */
target_ulong HELPER(pmin_h)(CPURISCVState *env,
                            target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int16_t res = (e1 < e2) ? e1 : e2;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMINU.H - Packed 16-bit unsigned minimum
 * For each halfword: rd[i] = min(rs1[i], rs2[i])
 */
target_ulong HELPER(pminu_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = (e1 < e2) ? e1 : e2;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMAX.H - Packed 16-bit signed maximum
 * For each halfword: rd[i] = max(rs1[i], rs2[i])
 */
target_ulong HELPER(pmax_h)(CPURISCVState *env,
                            target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int16_t res = (e1 > e2) ? e1 : e2;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMAXU.H - Packed 16-bit unsigned maximum
 * For each halfword: rd[i] = max(rs1[i], rs2[i])
 */
target_ulong HELPER(pmaxu_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = (e1 > e2) ? e1 : e2;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMSEQ.W - Packed 32-bit equal comparison (RV64 only)
 * For each word: rd[i] = 0xFFFFFFFF if rs1[i] == rs2[i], else 0x00000000
 */
uint64_t HELPER(pmseq_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = (e1 == e2) ? 0xFFFFFFFFU : 0x00000000U;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMSLT.W - Packed 32-bit signed less-than comparison (RV64 only)
 * For each word: rd[i] = 0xFFFFFFFF if rs1[i] < rs2[i], else 0x00000000
 */
uint64_t HELPER(pmslt_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        uint32_t res = (e1 < e2) ? 0xFFFFFFFFU : 0x00000000U;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMSLTU.W - Packed 32-bit unsigned less-than comparison (RV64 only)
 * For each word: rd[i] = 0xFFFFFFFF if rs1[i] < rs2[i], else 0x00000000
 */
uint64_t HELPER(pmsltu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = (e1 < e2) ? 0xFFFFFFFFU : 0x00000000U;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMIN.W - Packed 32-bit signed minimum (RV64 only)
 * For each word: rd[i] = min(rs1[i], rs2[i])
 */
uint64_t HELPER(pmin_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int32_t res = (e1 < e2) ? e1 : e2;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMINU.W - Packed 32-bit unsigned minimum (RV64 only)
 * For each word: rd[i] = min(rs1[i], rs2[i])
 */
uint64_t HELPER(pminu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = (e1 < e2) ? e1 : e2;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMAX.W - Packed 32-bit signed maximum (RV64 only)
 * For each word: rd[i] = max(rs1[i], rs2[i])
 */
uint64_t HELPER(pmax_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int32_t res = (e1 > e2) ? e1 : e2;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMAXU.W - Packed 32-bit unsigned maximum (RV64 only)
 * For each word: rd[i] = max(rs1[i], rs2[i])
 */
uint64_t HELPER(pmaxu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = (e1 > e2) ? e1 : e2;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * MSEQ - 32-bit scalar set if equal (mask)
 */
uint32_t HELPER(mseq)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    return (rs1 == rs2) ? 0xFFFFFFFFU : 0x00000000U;
}

/**
 * MSLT - 32-bit scalar set if signed less than (mask)
 */
uint32_t HELPER(mslt)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    return ((int32_t)rs1 < (int32_t)rs2) ? 0xFFFFFFFFU : 0x00000000U;
}

/**
 * MSLTU - 32-bit scalar set if unsigned less than (mask)
 */
uint32_t HELPER(msltu)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    return (rs1 < rs2) ? 0xFFFFFFFFU : 0x00000000U;
}

/* Shift operations (immediate and register) */

/**
 * PSLLI.B - Packed 8-bit logical shift left immediate
 * For each byte: rd[i] = rs1[i] << imm
 */
target_ulong HELPER(pslli_b)(CPURISCVState *env,
                             target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    uint8_t shamt = imm & 0x07;  /* 8-bit elements, max shift 7 */

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t res = e1 << shamt;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PSLL.BS - Packed 8-bit logical shift left from register
 * For each byte: rd[i] = rs1[i] << rs2[4:0]
 */
target_ulong HELPER(psll_bs)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    uint8_t shamt = rs2 & 0x07;  /* rs2[2:0] for 8-bit */

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t res = e1 << shamt;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PSLLI.H - Packed 16-bit logical shift left immediate
 * For each halfword: rd[i] = rs1[i] << imm
 */
target_ulong HELPER(pslli_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint8_t shamt = imm & 0x0F;  /* 16-bit elements, max shift 15 */

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t res = e1 << shamt;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PSLL.HS - Packed 16-bit logical shift left from register
 * For each halfword: rd[i] = rs1[i] << rs2[4:0]
 */
target_ulong HELPER(psll_hs)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint8_t shamt = rs2 & 0x0F;  /* rs2[3:0] for 16-bit */

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t res = e1 << shamt;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PSLLI.W - Packed 32-bit logical shift left immediate (RV64 only)
 * For each word: rd[i] = rs1[i] << imm
 */
uint64_t HELPER(pslli_w)(CPURISCVState *env, uint64_t rs1, uint64_t imm)
{
    uint64_t rd = 0;
    int elems = 2;
    uint8_t shamt = imm & 0x1F;  /* 32-bit elements, max shift 31 */

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t res = e1 << shamt;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PSLL.WS - Packed 32-bit logical shift left from register (RV64 only)
 * For each word: rd[i] = rs1[i] << rs2[5:0]
 */
uint64_t HELPER(psll_ws)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    uint8_t shamt = rs2 & 0x1F;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t res = e1 << shamt;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PSRLI.B - Packed 8-bit logical shift right immediate
 * For each byte: rd[i] = rs1[i] >> imm
 */
target_ulong HELPER(psrli_b)(CPURISCVState *env,
                             target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    uint8_t shamt = imm & 0x07;

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t res = e1 >> shamt;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PSRL.BS - Packed 8-bit logical shift right from register
 * For each byte: rd[i] = rs1[i] >> rs2[4:0]
 */
target_ulong HELPER(psrl_bs)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    uint8_t shamt = rs2 & 0x07;

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t res = e1 >> shamt;
        rd = INSERT8(rd, res, i);
    }
    return rd;
}

/**
 * PSRLI.H - Packed 16-bit logical shift right immediate
 * For each halfword: rd[i] = rs1[i] >> imm
 */
target_ulong HELPER(psrli_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint8_t shamt = imm & 0x0F;

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t res = e1 >> shamt;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PSRL.HS - Packed 16-bit logical shift right from register
 * For each halfword: rd[i] = rs1[i] >> rs2[4:0]
 */
target_ulong HELPER(psrl_hs)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint8_t shamt = rs2 & 0x0F;

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t res = e1 >> shamt;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PSRLI.W - Packed 32-bit logical shift right immediate (RV64 only)
 * For each word: rd[i] = rs1[i] >> imm
 */
uint64_t HELPER(psrli_w)(CPURISCVState *env, uint64_t rs1, uint64_t imm)
{
    uint64_t rd = 0;
    int elems = 2;
    uint8_t shamt = imm & 0x1F;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t res = e1 >> shamt;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PSRL.WS - Packed 32-bit logical shift right from register (RV64 only)
 * For each word: rd[i] = rs1[i] >> rs2[5:0]
 */
uint64_t HELPER(psrl_ws)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    uint8_t shamt = rs2 & 0x1F;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t res = e1 >> shamt;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PSRAI.B - Packed 8-bit arithmetic shift right immediate
 * For each byte: rd[i] = (int8_t)rs1[i] >> imm
 */
target_ulong HELPER(psrai_b)(CPURISCVState *env,
                             target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    uint8_t shamt = imm & 0x07;

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t res = e1 >> shamt;  /* Arithmetic right shift */
        rd = INSERT8(rd, (uint8_t)res, i);
    }
    return rd;
}

/**
 * PSRA.BS - Packed 8-bit arithmetic shift right from register
 * For each byte: rd[i] = (int8_t)rs1[i] >> rs2[4:0]
 */
target_ulong HELPER(psra_bs)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_B(rd);
    uint8_t shamt = rs2 & 0x07;

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        int8_t res = e1 >> shamt;
        rd = INSERT8(rd, (uint8_t)res, i);
    }
    return rd;
}

/**
 * PSRAI.H - Packed 16-bit arithmetic shift right immediate
 * For each halfword: rd[i] = (int16_t)rs1[i] >> imm
 */
target_ulong HELPER(psrai_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint8_t shamt = imm & 0x0F;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t res = e1 >> shamt;
        rd = INSERT16(rd, (uint16_t)res, i);
    }
    return rd;
}

/**
 * PSRA.HS - Packed 16-bit arithmetic shift right from register
 * For each halfword: rd[i] = (int16_t)rs1[i] >> rs2[4:0]
 */
target_ulong HELPER(psra_hs)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint8_t shamt = rs2 & 0x0F;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t res = e1 >> shamt;
        rd = INSERT16(rd, (uint16_t)res, i);
    }
    return rd;
}

/**
 * PSRAI.W - Packed 32-bit arithmetic shift right immediate (RV64 only)
 * For each word: rd[i] = (int32_t)rs1[i] >> imm
 */
uint64_t HELPER(psrai_w)(CPURISCVState *env, uint64_t rs1, uint64_t imm)
{
    uint64_t rd = 0;
    int elems = 2;
    uint8_t shamt = imm & 0x1F;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t res = e1 >> shamt;
        rd = INSERT32(rd, (uint32_t)res, i);
    }
    return rd;
}

/**
 * PSRA.WS - Packed 32-bit arithmetic shift right from register (RV64 only)
 * For each word: rd[i] = (int32_t)rs1[i] >> rs2[5:0]
 */
uint64_t HELPER(psra_ws)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    uint8_t shamt = rs2 & 0x1F;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t res = e1 >> shamt;
        rd = INSERT32(rd, (uint32_t)res, i);
    }
    return rd;
}

/* Saturating shift operations */

/**
 * PSSLAI.H - Packed 16-bit saturating shift left immediate
 * For each halfword: rd[i] = sat16(rs1[i] << imm)
 */
target_ulong HELPER(psslai_h)(CPURISCVState *env,
                              target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;
    uint8_t shamt = imm & 0x0F;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int32_t shifted = (int32_t)e1 << shamt;
        int16_t res = signed_saturate_h(shifted, &sat);
        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSLAI.W - Packed 32-bit saturating shift left immediate (RV64 only)
 * For each word: rd[i] = sat32(rs1[i] << imm)
 */
uint64_t HELPER(psslai_w)(CPURISCVState *env, uint64_t rs1, uint64_t imm)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;
    uint8_t shamt = imm & 0x1F;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int64_t shifted = (int64_t)e1 << shamt;
        int32_t res = signed_saturate_w(shifted, &sat);
        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * SSLAL - 32-bit scalar saturating shift left immediate
 */
uint32_t HELPER(sslai)(CPURISCVState *env, uint32_t rs1, uint32_t imm)
{
    int32_t a = (int32_t)rs1;
    uint8_t shamt = imm & 0x1F;
    int64_t shifted = (int64_t)a << shamt;
    int sat = 0;
    int32_t res = signed_saturate_w(shifted, &sat);

    if (sat) {
        env->vxsat = 1;
    }
    return (uint32_t)res;
}

/* Rounding shift operations */

/**
 * PSRARI.H - Packed 16-bit arithmetic shift right with rounding (immediate)
 * For each halfword: rd[i] = round((int16_t)rs1[i] >> imm)
 */
target_ulong HELPER(psrari_h)(CPURISCVState *env,
                              target_ulong rs1, target_ulong imm)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    uint8_t shamt = imm & 0x0F;

    if (shamt == 0) {
        return rs1;
    }

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int32_t rounded = ((e1 >> (shamt - 1)) + 1) >> 1;
        rd = INSERT16(rd, (int16_t)rounded, i);
    }
    return rd;
}

/**
 * PSRARI.W - Packed 32-bit arithmetic shift right
 * with rounding (immediate) (RV64 only)
 * For each word: rd[i] = round((int32_t)rs1[i] >> imm)
 */
uint64_t HELPER(psrari_w)(CPURISCVState *env, uint64_t rs1, uint64_t imm)
{
    uint64_t rd = 0;
    int elems = 2;
    uint8_t shamt = imm & 0x1F;

    if (shamt == 0) {
        return rs1;
    }

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int64_t rounded = ((e1 >> (shamt - 1)) + 1) >> 1;
        rd = INSERT32(rd, (int32_t)rounded, i);
    }
    return rd;
}

/**
 * SRARI_32 - 32-bit scalar arithmetic shift right with rounding
 */
uint32_t HELPER(srari_32)(CPURISCVState *env, uint32_t rs1, uint32_t imm)
{
    int32_t a = (int32_t)rs1;
    uint8_t shamt = imm & 0x1F;

    if (shamt == 0) {
        return rs1;
    }

    return (uint32_t)(((a >> (shamt - 1)) + 1) >> 1);
}

/**
 * SRARI_64 - 64-bit scalar arithmetic shift right with rounding
 */
uint64_t HELPER(srari_64)(CPURISCVState *env, uint64_t rs1, uint64_t imm)
{
    int64_t a = (int64_t)rs1;
    uint8_t shamt = imm & 0x3F;

    if (shamt == 0) {
        return rs1;
    }

    return (uint64_t)(((a >> (shamt - 1)) + 1) >> 1);
}

/* Variable shift operations (with saturation and rounding) */

/**
 * PSSHA.HS - Packed 16-bit variable shift with saturation
 * Positive shift left (saturating), negative shift right (non-saturating)
 */
target_ulong HELPER(pssha_hs)(CPURISCVState *env,
                              target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;
    int8_t shamt = (int8_t)(rs2 & 0xFF);  /* rs2[7:0] as signed */

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t res;

        if (shamt >= 0) {
            /* Left shift with saturation */
            int32_t shifted = (int32_t)e1 << shamt;
            res = signed_saturate_h(shifted, &sat);
        } else {
            /* Right shift (no saturation) */
            int right = -shamt;
            if (right >= 16) {
                res = (e1 < 0) ? -1 : 0;
            } else {
                res = e1 >> right;
            }
        }

        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSHA.WS - Packed 32-bit variable shift with saturation (RV64 only)
 * Positive shift left (saturating), negative shift right (non-saturating)
 */
uint64_t HELPER(pssha_ws)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;
    int8_t shamt = (int8_t)(rs2 & 0xFF);

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t res;

        if (shamt >= 0) {
            int64_t shifted = (int64_t)e1 << shamt;
            res = signed_saturate_w(shifted, &sat);
        } else {
            int right = -shamt;
            if (right >= 32) {
                res = (e1 < 0) ? -1 : 0;
            } else {
                res = e1 >> right;
            }
        }

        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSHAR.HS - Packed 16-bit variable shift with rounding and saturation
 * Positive shift left (saturating), negative shift right (rounded)
 */
target_ulong HELPER(psshar_hs)(CPURISCVState *env,
                               target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;
    int8_t shamt = (int8_t)(rs2 & 0xFF);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t res;

        if (shamt >= 0) {
            /* Left shift with saturation */
            int32_t shifted = (int32_t)e1 << shamt;
            res = signed_saturate_h(shifted, &sat);
        } else {
            /* Right shift with rounding */
            int right = -shamt;
            if (right >= 16) {
                res = (e1 < 0) ? -1 : 0;
            } else {
                int32_t rounded = ((e1 >> (right - 1)) + 1) >> 1;
                res = (int16_t)rounded;
            }
        }

        rd = INSERT16(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSHAR.WS - Packed 32-bit variable shift with
 * rounding and saturation (RV64 only)
 * Positive shift left (saturating), negative shift right (rounded)
 */
uint64_t HELPER(psshar_ws)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;
    int8_t shamt = (int8_t)(rs2 & 0xFF);

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t res;

        if (shamt >= 0) {
            int64_t shifted = (int64_t)e1 << shamt;
            res = signed_saturate_w(shifted, &sat);
        } else {
            int right = -shamt;
            if (right >= 32) {
                res = (e1 < 0) ? -1 : 0;
            } else {
                int64_t rounded = ((e1 >> (right - 1)) + 1) >> 1;
                res = (int32_t)rounded;
            }
        }

        rd = INSERT32(rd, res, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * SSHA - 32-bit scalar variable shift with saturation
 */
uint32_t HELPER(ssha)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int8_t shamt = (int8_t)(rs2 & 0xFF);
    int sat = 0;
    int32_t res;

    if (shamt >= 0) {
        int64_t shifted = (int64_t)a << shamt;
        res = signed_saturate_w(shifted, &sat);
    } else {
        int right = -shamt;
        if (right >= 32) {
            res = (a < 0) ? -1 : 0;
        } else {
            res = a >> right;
        }
    }

    if (sat) {
        env->vxsat = 1;
    }
    return (uint32_t)res;
}

/**
 * SSHAR - 32-bit scalar variable shift with rounding and saturation
 */
uint32_t HELPER(sshar)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int8_t shamt = (int8_t)(rs2 & 0xFF);
    int sat = 0;
    int32_t res;

    if (shamt >= 0) {
        int64_t shifted = (int64_t)a << shamt;
        res = signed_saturate_w(shifted, &sat);
    } else {
        int right = -shamt;
        if (right >= 32) {
            res = (a < 0) ? -1 : 0;
        } else {
            int64_t rounded = ((a >> (right - 1)) + 1) >> 1;
            res = (int32_t)rounded;
        }
    }

    if (sat) {
        env->vxsat = 1;
    }
    return (uint32_t)res;
}

/**
 * SHA - 64-bit scalar variable shift
 */
uint64_t HELPER(sha)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int64_t a = (int64_t)rs1;
    int8_t shamt = (int8_t)(rs2 & 0xFF);

    if (shamt >= 0) {
        return (uint64_t)(a << shamt);
    } else {
        int right = -shamt;
        if (right >= 64) {
            return (a < 0) ? (uint64_t)-1 : 0;
        } else {
            return (uint64_t)(a >> right);
        }
    }
}

/**
 * SHAR - 64-bit scalar variable shift with rounding
 */
uint64_t HELPER(shar)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int64_t a = (int64_t)rs1;
    int8_t shamt = (int8_t)(rs2 & 0xFF);

    if (shamt >= 0) {
        return (uint64_t)(a << shamt);
    } else {
        int right = -shamt;
        if (right >= 64) {
            return (a < 0) ? (uint64_t)-1 : 0;
        } else {
            __int128_t rounded = ((__int128_t)a >> (right - 1)) + 1;
            return (uint64_t)((int64_t)(rounded >> 1));
        }
    }
}

/* Exchange operations (AS/SA/AS/SA with X suffix) */

/**
 * PAS.HX - Packed add-subtract with exchange
 * For each pair: {rd[2i] = rs1[2i] - rs2[2i+1], rd[2i+1] = rs1[2i+1] + rs2[2i]}
 */
target_ulong HELPER(pas_hx)(CPURISCVState *env,
                            target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i += 2) {
        int16_t s1_lo = (int16_t)EXTRACT16(rs1, i);
        int16_t s1_hi = (int16_t)EXTRACT16(rs1, i + 1);
        int16_t s2_lo = (int16_t)EXTRACT16(rs2, i);
        int16_t s2_hi = (int16_t)EXTRACT16(rs2, i + 1);
        int16_t res_lo = s1_lo - s2_hi;
        int16_t res_hi = s1_hi + s2_lo;
        rd = INSERT16(rd, res_lo, i);
        rd = INSERT16(rd, res_hi, i + 1);
    }
    return rd;
}

/**
 * PSA.HX - Packed subtract-add with exchange
 * For each pair: {rd[2i] = rs1[2i] + rs2[2i+1], rd[2i+1] = rs1[2i+1] - rs2[2i]}
 */
target_ulong HELPER(psa_hx)(CPURISCVState *env,
                            target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i += 2) {
        int16_t s1_lo = (int16_t)EXTRACT16(rs1, i);
        int16_t s1_hi = (int16_t)EXTRACT16(rs1, i + 1);
        int16_t s2_lo = (int16_t)EXTRACT16(rs2, i);
        int16_t s2_hi = (int16_t)EXTRACT16(rs2, i + 1);
        int16_t res_lo = s1_lo + s2_hi;
        int16_t res_hi = s1_hi - s2_lo;
        rd = INSERT16(rd, res_lo, i);
        rd = INSERT16(rd, res_hi, i + 1);
    }
    return rd;
}

/**
 * PSAS.HX - Packed saturating add-subtract with exchange
 */
target_ulong HELPER(psas_hx)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i += 2) {
        int16_t s1_lo = (int16_t)EXTRACT16(rs1, i);
        int16_t s1_hi = (int16_t)EXTRACT16(rs1, i + 1);
        int16_t s2_lo = (int16_t)EXTRACT16(rs2, i);
        int16_t s2_hi = (int16_t)EXTRACT16(rs2, i + 1);
        int32_t diff = (int32_t)s1_lo - (int32_t)s2_hi;
        int32_t sum = (int32_t)s1_hi + (int32_t)s2_lo;
        int16_t res_lo = signed_saturate_h(diff, &sat);
        int16_t res_hi = signed_saturate_h(sum, &sat);
        rd = INSERT16(rd, res_lo, i);
        rd = INSERT16(rd, res_hi, i + 1);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSA.HX - Packed saturating subtract-add with exchange
 */
target_ulong HELPER(pssa_hx)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i += 2) {
        int16_t s1_lo = (int16_t)EXTRACT16(rs1, i);
        int16_t s1_hi = (int16_t)EXTRACT16(rs1, i + 1);
        int16_t s2_lo = (int16_t)EXTRACT16(rs2, i);
        int16_t s2_hi = (int16_t)EXTRACT16(rs2, i + 1);
        int32_t sum = (int32_t)s1_lo + (int32_t)s2_hi;
        int32_t diff = (int32_t)s1_hi - (int32_t)s2_lo;
        int16_t res_lo = signed_saturate_h(sum, &sat);
        int16_t res_hi = signed_saturate_h(diff, &sat);
        rd = INSERT16(rd, res_lo, i);
        rd = INSERT16(rd, res_hi, i + 1);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PAAS.HX - Packed averaging add-subtract with exchange
 */
target_ulong HELPER(paas_hx)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i += 2) {
        int16_t s1_lo = (int16_t)EXTRACT16(rs1, i);
        int16_t s1_hi = (int16_t)EXTRACT16(rs1, i + 1);
        int16_t s2_lo = (int16_t)EXTRACT16(rs2, i);
        int16_t s2_hi = (int16_t)EXTRACT16(rs2, i + 1);
        int16_t res_lo = (s1_lo - s2_hi) >> 1;
        int16_t res_hi = (s1_hi + s2_lo) >> 1;
        rd = INSERT16(rd, res_lo, i);
        rd = INSERT16(rd, res_hi, i + 1);
    }
    return rd;
}

/**
 * PASA.HX - Packed averaging subtract-add with exchange
 */
target_ulong HELPER(pasa_hx)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i += 2) {
        int16_t s1_lo = (int16_t)EXTRACT16(rs1, i);
        int16_t s1_hi = (int16_t)EXTRACT16(rs1, i + 1);
        int16_t s2_lo = (int16_t)EXTRACT16(rs2, i);
        int16_t s2_hi = (int16_t)EXTRACT16(rs2, i + 1);
        int16_t res_lo = (s1_lo + s2_hi) >> 1;
        int16_t res_hi = (s1_hi - s2_lo) >> 1;
        rd = INSERT16(rd, res_lo, i);
        rd = INSERT16(rd, res_hi, i + 1);
    }
    return rd;
}

/**
 * PAS.WX - Word version of packed add-subtract with exchange (RV64 only)
 */
uint64_t HELPER(pas_wx)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i += 2) {
        int32_t s1_lo = (int32_t)EXTRACT32(rs1, i);
        int32_t s1_hi = (int32_t)EXTRACT32(rs1, i + 1);
        int32_t s2_lo = (int32_t)EXTRACT32(rs2, i);
        int32_t s2_hi = (int32_t)EXTRACT32(rs2, i + 1);
        int32_t res_lo = s1_lo - s2_hi;
        int32_t res_hi = s1_hi + s2_lo;
        rd = INSERT32(rd, res_lo, i);
        rd = INSERT32(rd, res_hi, i + 1);
    }
    return rd;
}

/**
 * PSA.WX - Word version of packed subtract-add with exchange (RV64 only)
 */
uint64_t HELPER(psa_wx)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i += 2) {
        int32_t s1_lo = (int32_t)EXTRACT32(rs1, i);
        int32_t s1_hi = (int32_t)EXTRACT32(rs1, i + 1);
        int32_t s2_lo = (int32_t)EXTRACT32(rs2, i);
        int32_t s2_hi = (int32_t)EXTRACT32(rs2, i + 1);
        int32_t res_lo = s1_lo + s2_hi;
        int32_t res_hi = s1_hi - s2_lo;
        rd = INSERT32(rd, res_lo, i);
        rd = INSERT32(rd, res_hi, i + 1);
    }
    return rd;
}

/**
 * PSAS.WX - Word version of packed saturating
 * add-subtract with exchange (RV64 only)
 */
uint64_t HELPER(psas_wx)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i += 2) {
        int32_t s1_lo = (int32_t)EXTRACT32(rs1, i);
        int32_t s1_hi = (int32_t)EXTRACT32(rs1, i + 1);
        int32_t s2_lo = (int32_t)EXTRACT32(rs2, i);
        int32_t s2_hi = (int32_t)EXTRACT32(rs2, i + 1);
        int64_t diff = (int64_t)s1_lo - (int64_t)s2_hi;
        int64_t sum = (int64_t)s1_hi + (int64_t)s2_lo;
        int32_t res_lo = signed_saturate_w(diff, &sat);
        int32_t res_hi = signed_saturate_w(sum, &sat);
        rd = INSERT32(rd, res_lo, i);
        rd = INSERT32(rd, res_hi, i + 1);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PSSA.WX - Word version of packed saturating
 * subtract-add with exchange (RV64 only)
 */
uint64_t HELPER(pssa_wx)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i += 2) {
        int32_t s1_lo = (int32_t)EXTRACT32(rs1, i);
        int32_t s1_hi = (int32_t)EXTRACT32(rs1, i + 1);
        int32_t s2_lo = (int32_t)EXTRACT32(rs2, i);
        int32_t s2_hi = (int32_t)EXTRACT32(rs2, i + 1);
        int64_t sum = (int64_t)s1_lo + (int64_t)s2_hi;
        int64_t diff = (int64_t)s1_hi - (int64_t)s2_lo;
        int32_t res_lo = signed_saturate_w(sum, &sat);
        int32_t res_hi = signed_saturate_w(diff, &sat);
        rd = INSERT32(rd, res_lo, i);
        rd = INSERT32(rd, res_hi, i + 1);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PAAS.WX - Word version of packed averaging
 * add-subtract with exchange (RV64 only)
 */
uint64_t HELPER(paas_wx)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i += 2) {
        int64_t s1_lo = (int32_t)EXTRACT32(rs1, i);
        int64_t s1_hi = (int32_t)EXTRACT32(rs1, i + 1);
        int64_t s2_lo = (int32_t)EXTRACT32(rs2, i);
        int64_t s2_hi = (int32_t)EXTRACT32(rs2, i + 1);
        int32_t res_lo = (s1_lo - s2_hi) >> 1;
        int32_t res_hi = (s1_hi + s2_lo) >> 1;
        rd = INSERT32(rd, res_lo, i);
        rd = INSERT32(rd, res_hi, i + 1);
    }
    return rd;
}

/**
 * PASA.WX - Word version of packed averaging
 * subtract-add with exchange (RV64 only)
 */
uint64_t HELPER(pasa_wx)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i += 2) {
        int64_t s1_lo = (int32_t)EXTRACT32(rs1, i);
        int64_t s1_hi = (int32_t)EXTRACT32(rs1, i + 1);
        int64_t s2_lo = (int32_t)EXTRACT32(rs2, i);
        int64_t s2_hi = (int32_t)EXTRACT32(rs2, i + 1);
        int32_t res_lo = (s1_lo + s2_hi) >> 1;
        int32_t res_hi = (s1_hi - s2_lo) >> 1;
        rd = INSERT32(rd, res_lo, i);
        rd = INSERT32(rd, res_hi, i + 1);
    }
    return rd;
}
