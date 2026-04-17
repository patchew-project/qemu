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
