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

/* Horizontal sum operations */

/**
 * PREDSUM.BS - Signed reduction sum of bytes
 * rd = rs2 + sum(sign_extend(rs1[i]))
 */
target_ulong HELPER(predsum_bs)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    int64_t sum = (int64_t)(int32_t)rs2;
    int elems = ELEMS_B(rs1);

    for (int i = 0; i < elems; i++) {
        int8_t e1 = (int8_t)EXTRACT8(rs1, i);
        sum += e1;
    }

    return (target_ulong)sum;
}

/**
 * PREDSUMU.BS - Unsigned reduction sum of bytes
 * rd = rs2 + sum(zero_extend(rs1[i]))
 */
target_ulong HELPER(predsumu_bs)(CPURISCVState *env,
                                 target_ulong rs1, target_ulong rs2)
{
    uint64_t sum = rs2;
    int elems = ELEMS_B(rs1);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        sum += e1;
    }

    return (target_ulong)sum;
}

/**
 * PREDSUM.HS - Signed reduction sum of halfwords
 * rd = rs2 + sum(sign_extend(rs1[i]))
 */
target_ulong HELPER(predsum_hs)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    int64_t sum = (int64_t)(int32_t)rs2;
    int elems = ELEMS_H(rs1);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        sum += e1;
    }

    return (target_ulong)sum;
}

/**
 * PREDSUMU.HS - Unsigned reduction sum of halfwords
 * rd = rs2 + sum(zero_extend(rs1[i]))
 */
target_ulong HELPER(predsumu_hs)(CPURISCVState *env,
                                 target_ulong rs1, target_ulong rs2)
{
    uint64_t sum = rs2;
    int elems = ELEMS_H(rs1);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        sum += e1;
    }

    return (target_ulong)sum;
}

/**
 * PREDSUM.WS - Signed reduction sum of words (RV64 only)
 * rd = rs2 + sum(sign_extend(rs1[i]))
 */
uint64_t HELPER(predsum_ws)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int64_t sum = (int64_t)rs2;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        sum += e1;
    }

    return (uint64_t)sum;
}

/**
 * PREDSUMU.WS - Unsigned reduction sum of words (RV64 only)
 * rd = rs2 + sum(zero_extend(rs1[i]))
 */
uint64_t HELPER(predsumu_ws)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t sum = rs2;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        sum += e1;
    }

    return sum;
}

/* Packing/unpacking operations */

/**
 * PPAIRE.B - Pair low bytes of corresponding halfwords
 * For each halfword: rd[i] = {rs2[i][7:0], rs1[i][7:0]}
 */
target_ulong HELPER(ppaire_b)(CPURISCVState *env,
                               target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = ((e2 & 0x00FF) << 8) | (e1 & 0x00FF);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PPAIREO.B - Pair high byte of rs2 with low byte of rs1
 * For each halfword: rd[i] = {rs2[i][15:8], rs1[i][7:0]}
 */
target_ulong HELPER(ppaireo_b)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = ((e2 >> 8) << 8) | (e1 & 0x00FF);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PPAIROE.B - Pair low byte of rs2 with high byte of rs1
 * For each halfword: rd[i] = {rs2[i][7:0], rs1[i][15:8]}
 */
target_ulong HELPER(ppairoe_b)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = ((e2 & 0x00FF) << 8) | ((e1 >> 8) & 0x00FF);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PPAIRO.B - Pair high bytes of corresponding halfwords
 * For each halfword: rd[i] = {rs2[i][15:8], rs1[i][15:8]}
 */
target_ulong HELPER(ppairo_b)(CPURISCVState *env,
                               target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t res = ((e2 >> 8) << 8) | ((e1 >> 8) & 0x00FF);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PPAIRE.H - Pair low halfwords of corresponding words
 * (RV64 only)
 * For each word: rd[i] = {rs2[i][15:0], rs1[i][15:0]}
 */
uint64_t HELPER(ppaire_h)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = ((e2 & 0x0000FFFF) << 16) | (e1 & 0x0000FFFF);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PPAIREO.H - Pair high halfword of rs2 with low halfword of rs1 (RV64 only)
 */
target_ulong HELPER(ppaireo_h)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = ((e2 >> 16) << 16) | (e1 & 0x0000FFFF);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PPAIROE.H - Pair low halfword of rs2 with high halfword of rs1 (RV64 only)
 */
target_ulong HELPER(ppairoe_h)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = ((e2 & 0x0000FFFF) << 16) | ((e1 >> 16) & 0x0000FFFF);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PPAIRO.H - Pair high halfwords of corresponding words (RV64 only)
 */
target_ulong HELPER(ppairo_h)(CPURISCVState *env,
                               target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t res = ((e2 >> 16) << 16) | ((e1 >> 16) & 0x0000FFFF);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PPAIREO.W - Pair low word of rs2 with low word of rs1 (RV64 only)
 */
uint64_t HELPER(ppaireo_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    uint32_t e1 = EXTRACT32(rs1, 0);
    uint32_t e2 = EXTRACT32(rs2, 1);
    rd = ((uint64_t)e2 << 32) | e1;
    return rd;
}

/**
 * PPAIROE.W - Pair low word of rs2 with high word of rs1 (RV64 only)
 */
uint64_t HELPER(ppairoe_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    uint32_t e1 = EXTRACT32(rs1, 1);
    uint32_t e2 = EXTRACT32(rs2, 0);
    rd = ((uint64_t)e2 << 32) | e1;
    return rd;
}

/**
 * PPAIRO.W - Pair high word of rs2 with high word of rs1 (RV64 only)
 */
uint64_t HELPER(ppairo_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    uint32_t e1 = EXTRACT32(rs1, 1);
    uint32_t e2 = EXTRACT32(rs2, 1);
    rd = ((uint64_t)e2 << 32) | e1;
    return rd;
}

/**
 * PSEXT.H.B - Sign-extend bytes to halfwords within each halfword
 */
target_ulong HELPER(psext_h_b)(CPURISCVState *env, target_ulong rs1)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        int8_t b0 = (int8_t)(e1 & 0xFF);
        int16_t res = (int16_t)b0;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PSEXT.W.B - Sign-extend bytes to words (RV64 only)
 */
uint64_t HELPER(psext_w_b)(CPURISCVState *env, uint64_t rs1)
{
    uint64_t rd = 0;
    int8_t b0 = (int8_t)EXTRACT8(rs1, 0);
    int8_t b4 = (int8_t)EXTRACT8(rs1, 4);
    uint32_t lo = (uint32_t)(int32_t)b0;
    uint32_t hi = (uint32_t)(int32_t)b4;
    rd = ((uint64_t)hi << 32) | lo;
    return rd;
}

/**
 * PSEXT.W.H - Sign-extend halfwords to words (RV64 only)
 */
uint64_t HELPER(psext_w_h)(CPURISCVState *env, uint64_t rs1)
{
    uint64_t rd = 0;
    int16_t h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t h2 = (int16_t)EXTRACT16(rs1, 2);
    uint32_t lo = (uint32_t)(int32_t)h0;
    uint32_t hi = (uint32_t)(int32_t)h2;
    rd = ((uint64_t)hi << 32) | lo;
    return rd;
}

/**
 * REV - Reverse all bits
 */
target_ulong HELPER(rev)(CPURISCVState *env, target_ulong rs1)
{
    target_ulong rd = 0;

    for (int i = 0; i < TARGET_LONG_BITS; i++) {
        rd = (rd << 1) | (rs1 & 1);
        rs1 >>= 1;
    }

    return rd;
}

/**
 * REV16 - Reverse 16-bit chunks (RV64 only)
 */
uint64_t HELPER(rev16)(CPURISCVState *env, uint64_t rs1)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t chunk = EXTRACT16(rs1, i);
        rd = (rd << 16) | chunk;
    }

    return rd;
}

/**
 * ZIP8P - Interleave bytes from rs2 and rs1 (RV64 only)
 * rd = {rs2[31:24], rs1[31:24], rs2[23:16], rs1[23:16],
 *       rs2[15:8], rs1[15:8], rs2[7:0], rs1[7:0]}
 */
uint64_t HELPER(zip8p)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t b1 = EXTRACT8(rs1, 3 - i);
        uint8_t b2 = EXTRACT8(rs2, 3 - i);
        rd = (rd << 16) | ((uint16_t)b2 << 8) | b1;
    }

    return rd;
}

/**
 * ZIP8HP - Interleave high bytes from rs2 and rs1 (RV64 only)
 */
uint64_t HELPER(zip8hp)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t b1 = EXTRACT8(rs1, 7 - i);
        uint8_t b2 = EXTRACT8(rs2, 7 - i);
        rd = (rd << 16) | ((uint16_t)b2 << 8) | b1;
    }

    return rd;
}

/**
 * UNZIP8P - De-interleave bytes
 * (RV64 only)
 */
uint64_t HELPER(unzip8p)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint64_t b1 = EXTRACT8(rs1, 2 * i) << 8 * i;
        uint64_t b2 = EXTRACT8(rs2, 2 * i) << (32 + 8 * i);
        rd = rd | b2 | b1;
    }

    return rd;
}

/**
 * UNZIP8HP - De-interleave high bytes
 * (RV64 only)
 */
uint64_t HELPER(unzip8hp)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint64_t b1 = EXTRACT8(rs1, 2 * i + 1) << 8 * i;
        uint64_t b2 = EXTRACT8(rs2, 2 * i + 1) << (32 + 8 * i);
        rd = rd | b2 | b1;
    }

    return rd;
}

/**
 * ZIP16P - Interleave halfwords from rs2 and rs1 (RV64 only)
 */
uint64_t HELPER(zip16p)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint16_t h1 = EXTRACT16(rs1, 1 - i);
        uint16_t h2 = EXTRACT16(rs2, 1 - i);
        rd = (rd << 32) | ((uint32_t)h2 << 16) | h1;
    }

    return rd;
}

/**
 * ZIP16HP - Interleave high halfwords (RV64 only)
 */
uint64_t HELPER(zip16hp)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint16_t h1 = EXTRACT16(rs1, 3 - i);
        uint16_t h2 = EXTRACT16(rs2, 3 - i);
        rd = (rd << 32) | ((uint32_t)h2 << 16) | h1;
    }

    return rd;
}

/**
 * UNZIP16P - De-interleave halfwords (RV64 only)
 */
uint64_t HELPER(unzip16p)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint64_t b1 = EXTRACT16(rs1, 2 * i) << 16 * i;
        uint64_t b2 = EXTRACT16(rs2, 2 * i) << (32 + 16 * i);
        rd = rd | b2 | b1;
    }

    return rd;
}

/**
 * UNZIP16HP - De-interleave high halfwords (RV64 only)
 */
uint64_t HELPER(unzip16hp)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint64_t b1 = EXTRACT16(rs1, 2 * i + 1) << 16 * i;
        uint64_t b2 = EXTRACT16(rs2, 2 * i + 1) << (32 + 16 * i);
        rd = rd | b2 | b1;
    }

    return rd;
}


/* Merge and mask operations */

/**
 * SLX - Shift left extended (concatenate rd and rs1, shift left, take upper)
 */
target_ulong HELPER(slx)(CPURISCVState *env, target_ulong rs1,
                         target_ulong rs2, target_ulong rd)
{
    int shamt = (TARGET_LONG_BITS == 32) ? (rs2 & 0x1F) : (rs2 & 0x3F);
    target_ulong xrs1 = 0;
    target_ulong xrd = 0;

    if (shamt <= TARGET_LONG_BITS) {
        xrs1 = rs1 >> (TARGET_LONG_BITS - shamt);
        xrd = (rd << shamt) + xrs1;
    } else {
        xrd = rs1 << (shamt - TARGET_LONG_BITS);
    }

    return xrd;
}

/**
 * SRX - Shift right extended (concatenate rs1 and rd, shift right, take lower)
 */
target_ulong HELPER(srx)(CPURISCVState *env, target_ulong rs1,
                         target_ulong rs2, target_ulong rd)
{
    int shamt = (TARGET_LONG_BITS == 32) ? (rs2 & 0x1F) : (rs2 & 0x3F);
    target_ulong xrs1 = 0;
    target_ulong xrd = 0;

    if (shamt <= TARGET_LONG_BITS) {
        xrs1 = rs1 << (TARGET_LONG_BITS - shamt);
        xrd = (rd >> shamt) + xrs1;
    } else {
        xrd = rs1 >> (shamt - TARGET_LONG_BITS);
    }

    return xrd;
}

/**
 * MVM - Move masked
 * For each bit: rd[i] = rs2[i] ? rs1[i] : rd[i]
 */
target_ulong HELPER(mvm)(CPURISCVState *env, target_ulong rs1,
                         target_ulong rs2, target_ulong rd)
{
    return (~rs2 & rd) | (rs2 & rs1);
}

/**
 * MVMN - Move masked not
 * For each bit: rd[i] = rs2[i] ? rd[i] : rs1[i]
 */
target_ulong HELPER(mvmn)(CPURISCVState *env, target_ulong rs1,
                          target_ulong rs2, target_ulong rd)
{
    return (~rs2 & rs1) | (rs2 & rd);
}

/**
 * MERGE - Merge
 * For each bit: rd[i] = rd[i] ? rs2[i] : rs1[i]
 */
target_ulong HELPER(merge)(CPURISCVState *env, target_ulong rs1,
                           target_ulong rs2, target_ulong rd)
{
    return (~rd & rs1) | (rd & rs2);
}

/* Count leading operations */

/**
 * CLS - Count leading redundant sign bits
 */
target_ulong HELPER(cls)(CPURISCVState *env, target_ulong rs1)
{
    target_long a = (target_long)rs1;
    target_ulong cnt = 0;

#if TARGET_LONG_BITS == 64
    target_long lo_bound = 0xC000000000000000LL;
    target_long hi_bound = 0x3FFFFFFFFFFFFFFFLL;
#else
    target_long lo_bound = 0xC0000000;
    target_long hi_bound = 0x3FFFFFFF;
#endif

    while (cnt < TARGET_LONG_BITS - 1 && a >= lo_bound && a <= hi_bound) {
        cnt++;
        a <<= 1;
    }

    return cnt;
}

/**
 * CLSW - Count leading redundant sign bits of low 32 bits (RV64)
 */
uint64_t HELPER(clsw)(CPURISCVState *env, uint64_t rs1)
{
    int32_t a = (int32_t)(rs1 & 0xFFFFFFFF);
    int32_t lo_bound = 0xC0000000;
    int32_t hi_bound = 0x3FFFFFFF;
    int c = 0;

    while (c < 31 && a >= lo_bound && a <= hi_bound) {
        c++;
        a <<= 1;
    }

    return c;
}

/* Pure multiplication operations */

/**
 * PMULH.H - Packed signed 16-bit multiply high
 * For each halfword: rd[i] = (rs1[i] * rs2[i]) >> 16
 */
target_ulong HELPER(pmulh_h)(CPURISCVState *env,
                             target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int32_t prod = (int32_t)e1 * (int32_t)e2;
        uint16_t high = (uint16_t)(prod >> 16);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULHSU.H - Packed signed x unsigned 16-bit multiply high
 */
target_ulong HELPER(pmulhsu_h)(CPURISCVState *env,
                               target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        int32_t prod = (int32_t)e1 * (uint32_t)e2;
        uint16_t high = (uint16_t)(prod >> 16);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULHU.H - Packed unsigned 16-bit multiply high
 */
target_ulong HELPER(pmulhu_h)(CPURISCVState *env,
                              target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint32_t prod = (uint32_t)e1 * (uint32_t)e2;
        uint16_t high = (uint16_t)(prod >> 16);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULHR.H - Packed signed 16-bit multiply high with rounding
 */
target_ulong HELPER(pmulhr_h)(CPURISCVState *env,
                              target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int32_t prod = (int32_t)e1 * (int32_t)e2 + (1 << 15);
        uint16_t high = (uint16_t)(prod >> 16);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULHRSU.H - Packed signed x unsigned 16-bit multiply high with rounding
 */
target_ulong HELPER(pmulhrsu_h)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        int32_t prod = (int32_t)e1 * (uint32_t)e2 + (1 << 15);
        uint16_t high = (uint16_t)(prod >> 16);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULHRU.H - Packed unsigned 16-bit multiply high with rounding
 */
target_ulong HELPER(pmulhru_h)(CPURISCVState *env,
                               target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint32_t prod = (uint32_t)e1 * (uint32_t)e2 + (1 << 15);
        uint16_t high = (uint16_t)(prod >> 16);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULH.W - Packed signed 32-bit multiply high (RV64 only)
 */
uint64_t HELPER(pmulh_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int64_t prod = (int64_t)e1 * (int64_t)e2;
        uint32_t high = (uint32_t)(prod >> 32);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMULHR.W - Packed signed 32-bit multiply high with rounding (RV64 only)
 */
uint64_t HELPER(pmulhr_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int64_t prod = (int64_t)e1 * (int64_t)e2 + (1LL << 31);
        uint32_t high = (uint32_t)(prod >> 32);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMULHSU.W - Packed signed x unsigned 32-bit multiply high (RV64 only)
 */
uint64_t HELPER(pmulhsu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        int64_t prod = (int64_t)e1 * (uint64_t)e2;
        uint32_t high = (uint32_t)(prod >> 32);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMULHRSU.W - Packed signed x unsigned 32-bit
 * multiply high with rounding (RV64 only)
 */
uint64_t HELPER(pmulhrsu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        int64_t prod = (int64_t)e1 * (uint64_t)e2 + (1LL << 31);
        uint32_t high = (uint32_t)(prod >> 32);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMULHU.W - Packed unsigned 32-bit multiply high (RV64 only)
 */
uint64_t HELPER(pmulhu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint64_t prod = (uint64_t)e1 * (uint64_t)e2;
        uint32_t high = (uint32_t)(prod >> 32);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMULHRU.W - Packed unsigned 32-bit multiply high with rounding (RV64 only)
 */
uint64_t HELPER(pmulhru_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint64_t prod = (uint64_t)e1 * (uint64_t)e2 + (1LL << 31);
        uint32_t high = (uint32_t)(prod >> 32);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * MULHR - 32-bit signed multiply high with rounding
 */
uint32_t HELPER(mulhr)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int32_t b = (int32_t)rs2;
    int64_t prod = (int64_t)a * (int64_t)b + (1LL << 31);
    return (uint32_t)(prod >> 32);
}

/**
 * MULHRSU - 32-bit signed x unsigned multiply high with rounding
 */
uint32_t HELPER(mulhrsu)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    uint32_t b = rs2;
    int64_t prod = (int64_t)a * (uint64_t)b + (1LL << 31);
    return (uint32_t)(prod >> 32);
}

/**
 * MULHRU - 32-bit unsigned multiply high with rounding
 */
uint32_t HELPER(mulhru)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint32_t a = rs1;
    uint32_t b = rs2;
    uint64_t prod = (uint64_t)a * (uint64_t)b + (1LL << 31);
    return (uint32_t)(prod >> 32);
}

/**
 * PMULH.H.B0 - Multiply halfword by low byte, result high halfword
 */
target_ulong HELPER(pmulh_h_b0)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i * 2);
        int32_t prod = (int32_t)e1 * (int32_t)e2;
        uint16_t high = (uint16_t)(prod >> 8);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULH.H.B1 - Multiply halfword by high byte, result high halfword
 */
target_ulong HELPER(pmulh_h_b1)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i * 2 + 1);
        int32_t prod = (int32_t)e1 * (int32_t)e2;
        uint16_t high = (uint16_t)(prod >> 8);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULHSU.H.B0 - Multiply signed halfword by unsigned
 * low byte, result high halfword
 */
target_ulong HELPER(pmulhsu_h_b0)(CPURISCVState *env,
                                  target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i * 2);
        int32_t prod = (int32_t)e1 * (uint32_t)e2;
        uint16_t high = (uint16_t)(prod >> 8);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * PMULHSU.H.B1 - Multiply signed halfword by unsigned
 * high byte, result high halfword
 */
target_ulong HELPER(pmulhsu_h_b1)(CPURISCVState *env,
                                  target_ulong rs1, target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i * 2 + 1);
        int32_t prod = (int32_t)e1 * (uint32_t)e2;
        uint16_t high = (uint16_t)(prod >> 8);
        rd = INSERT16(rd, high, i);
    }
    return rd;
}

/**
 * MULH.H0 - 32-bit multiply by low halfword, result high 16 bits
 */
uint32_t HELPER(mulh_h0)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int16_t b = (int16_t)(rs2 & 0xFFFF);
    int64_t prod = (int64_t)a * (int64_t)b;
    return (uint32_t)(prod >> 16);
}

/**
 * MULH.H1 - 32-bit multiply by high halfword, result high 16 bits
 */
uint32_t HELPER(mulh_h1)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int16_t b = (int16_t)((rs2 >> 16) & 0xFFFF);
    int64_t prod = (int64_t)a * (int64_t)b;
    return (uint32_t)(prod >> 16);
}

/**
 * MULHSU.H0 - 32-bit signed multiply by unsigned
 * low halfword, result high 16 bits
 */
uint32_t HELPER(mulhsu_h0)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    uint16_t b = (uint16_t)(rs2 & 0xFFFF);
    int64_t prod = (int64_t)a * (uint64_t)b;
    return (uint32_t)(prod >> 16);
}

/**
 * MULHSU.H1 - 32-bit signed multiply by unsigned
 * high halfword, result high 16 bits
 */
uint32_t HELPER(mulhsu_h1)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    uint16_t b = (uint16_t)((rs2 >> 16) & 0xFFFF);
    int64_t prod = (int64_t)a * (uint64_t)b;
    return (uint32_t)(prod >> 16);
}

/**
 * PMULH.W.H0 - Multiply word by low halfword, result high word (RV64 only)
 */
uint64_t HELPER(pmulh_w_h0)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i * 2);
        int64_t prod = (int64_t)e1 * (int64_t)e2;
        uint32_t high = (uint32_t)(prod >> 16);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMULH.W.H1 - Multiply word by high halfword, result high word (RV64 only)
 */
uint64_t HELPER(pmulh_w_h1)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int64_t prod = (int64_t)e1 * (int64_t)e2;
        uint32_t high = (uint32_t)(prod >> 16);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMULHSU.W.H0 - Multiply signed word by unsigned
 * low halfword, result high word (RV64 only)
 */
uint64_t HELPER(pmulhsu_w_h0)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i * 2);
        int64_t prod = (int64_t)e1 * (uint64_t)e2;
        uint32_t high = (uint32_t)(prod >> 16);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMULHSU.W.H1 - Multiply signed word by unsigned
 * high halfword, result high word (RV64 only)
 */
uint64_t HELPER(pmulhsu_w_h1)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i * 2 + 1);
        int64_t prod = (int64_t)e1 * (uint64_t)e2;
        uint32_t high = (uint32_t)(prod >> 16);
        rd = INSERT32(rd, high, i);
    }
    return rd;
}

/**
 * PMUL.H.B00 - Multiply halfword by low byte of each halfword
 * For each halfword: rd[i] = rs1[i][7:0] * rs2[i][7:0]
 */
target_ulong HELPER(pmul_h_b00)(CPURISCVState *env,
                                target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h = EXTRACT16(s1, i);
        uint16_t s2_h = EXTRACT16(s2, i);
        int8_t s1_b0 = (int8_t)(s1_h & 0xFF);
        int8_t s2_b0 = (int8_t)(s2_h & 0xFF);
        int16_t mul = (int16_t)s1_b0 * (int16_t)s2_b0;
        rd = INSERT16(rd, (uint16_t)mul, i);
    }
    return rd;
}

/**
 * PMUL.H.B01 - Multiply halfword low byte by halfword high byte
 * For each halfword: rd[i] = rs1[i][7:0] * rs2[i][15:8]
 */
target_ulong HELPER(pmul_h_b01)(CPURISCVState *env,
                                target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h = EXTRACT16(s1, i);
        uint16_t s2_h = EXTRACT16(s2, i);
        int8_t s1_b0 = (int8_t)(s1_h & 0xFF);
        int8_t s2_b1 = (int8_t)((s2_h >> 8) & 0xFF);
        int16_t mul = (int16_t)s1_b0 * (int16_t)s2_b1;
        rd = INSERT16(rd, (uint16_t)mul, i);
    }
    return rd;
}

/**
 * PMUL.H.B11 - Multiply halfword high byte by halfword high byte
 * For each halfword: rd[i] = rs1[i][15:8] * rs2[i][15:8]
 */
target_ulong HELPER(pmul_h_b11)(CPURISCVState *env,
                                target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h = EXTRACT16(s1, i);
        uint16_t s2_h = EXTRACT16(s2, i);
        int8_t s1_b1 = (int8_t)((s1_h >> 8) & 0xFF);
        int8_t s2_b1 = (int8_t)((s2_h >> 8) & 0xFF);
        int16_t mul = (int16_t)s1_b1 * (int16_t)s2_b1;
        rd = INSERT16(rd, (uint16_t)mul, i);
    }
    return rd;
}

/**
 * PMULSU.H.B00 - Signed x unsigned multiply, low bytes
 * For each halfword: rd[i] = (signed)rs1[i][7:0] * (unsigned)rs2[i][7:0]
 */
target_ulong HELPER(pmulsu_h_b00)(CPURISCVState *env,
                                  target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h = EXTRACT16(s1, i);
        uint16_t s2_h = EXTRACT16(s2, i);
        int8_t s1_b0 = (int8_t)(s1_h & 0xFF);
        uint8_t s2_b0 = (uint8_t)(s2_h & 0xFF);
        int16_t mul = (int16_t)s1_b0 * (uint16_t)s2_b0;
        rd = INSERT16(rd, (uint16_t)mul, i);
    }
    return rd;
}

/**
 * PMULSU.H.B11 - Signed x unsigned multiply, high bytes
 * For each halfword: rd[i] = (signed)rs1[i][15:8] * (unsigned)rs2[i][15:8]
 */
target_ulong HELPER(pmulsu_h_b11)(CPURISCVState *env,
                                  target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h = EXTRACT16(s1, i);
        uint16_t s2_h = EXTRACT16(s2, i);
        int8_t s1_b1 = (int8_t)((s1_h >> 8) & 0xFF);
        uint8_t s2_b1 = (uint8_t)((s2_h >> 8) & 0xFF);
        int16_t mul = (int16_t)s1_b1 * (uint16_t)s2_b1;
        rd = INSERT16(rd, (uint16_t)mul, i);
    }
    return rd;
}

/**
 * PMULU.H.B00 - Unsigned multiply, low bytes
 * For each halfword: rd[i] = rs1[i][7:0] * rs2[i][7:0] (unsigned)
 */
target_ulong HELPER(pmulu_h_b00)(CPURISCVState *env,
                                 target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h = EXTRACT16(s1, i);
        uint16_t s2_h = EXTRACT16(s2, i);
        uint8_t s1_b0 = (uint8_t)(s1_h & 0xFF);
        uint8_t s2_b0 = (uint8_t)(s2_h & 0xFF);
        uint16_t mul = (uint16_t)s1_b0 * (uint16_t)s2_b0;
        rd = INSERT16(rd, mul, i);
    }
    return rd;
}

/**
 * PMULU.H.B01 - Unsigned multiply, rs1 low byte x rs2 high byte
 * For each halfword: rd[i] = rs1[i][7:0] * rs2[i][15:8] (unsigned)
 */
target_ulong HELPER(pmulu_h_b01)(CPURISCVState *env,
                                 target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h = EXTRACT16(s1, i);
        uint16_t s2_h = EXTRACT16(s2, i);
        uint8_t s1_b0 = (uint8_t)(s1_h & 0xFF);
        uint8_t s2_b1 = (uint8_t)((s2_h >> 8) & 0xFF);
        uint16_t mul = (uint16_t)s1_b0 * (uint16_t)s2_b1;
        rd = INSERT16(rd, mul, i);
    }
    return rd;
}

/**
 * PMULU.H.B11 - Unsigned multiply, high bytes
 * For each halfword: rd[i] = rs1[i][15:8] * rs2[i][15:8] (unsigned)
 */
target_ulong HELPER(pmulu_h_b11)(CPURISCVState *env,
                                 target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h = EXTRACT16(s1, i);
        uint16_t s2_h = EXTRACT16(s2, i);
        uint8_t s1_b1 = (uint8_t)((s1_h >> 8) & 0xFF);
        uint8_t s2_b1 = (uint8_t)((s2_h >> 8) & 0xFF);
        uint16_t mul = (uint16_t)s1_b1 * (uint16_t)s2_b1;
        rd = INSERT16(rd, mul, i);
    }
    return rd;
}

/**
 * PMUL.W.H00 - Multiply word by low halfword of each word
 * For each word: rd[i] = rs1[i][15:0] * rs2[i][15:0]
 */
uint64_t HELPER(pmul_w_h00)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int32_t mul = (int32_t)s1_h0 * (int32_t)s2_h0;
        rd = INSERT32(rd, (uint32_t)mul, i);
    }
    return rd;
}

/**
 * PMUL.W.H01 - Multiply word by low halfword x high halfword
 * For each word: rd[i] = rs1[i][15:0] * rs2[i][31:16]
 */
uint64_t HELPER(pmul_w_h01)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t mul = (int32_t)s1_h0 * (int32_t)s2_h1;
        rd = INSERT32(rd, (uint32_t)mul, i);
    }
    return rd;
}

/**
 * PMUL.W.H11 - Multiply word by high halfword x high halfword
 * For each word: rd[i] = rs1[i][31:16] * rs2[i][31:16]
 */
uint64_t HELPER(pmul_w_h11)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t mul = (int32_t)s1_h1 * (int32_t)s2_h1;
        rd = INSERT32(rd, (uint32_t)mul, i);
    }
    return rd;
}

/**
 * PMULSU.W.H00 - Signed x unsigned multiply, low halfwords
 * For each word: rd[i] = (signed)rs1[i][15:0] * (unsigned)rs2[i][15:0]
 */
uint64_t HELPER(pmulsu_w_h00)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        uint16_t s2_h0 = EXTRACT16(rs2, i * 2);
        int32_t mul = (int32_t)s1_h0 * (uint32_t)s2_h0;
        rd = INSERT32(rd, (uint32_t)mul, i);
    }
    return rd;
}

/**
 * PMULSU.W.H11 - Signed x unsigned multiply, high halfwords
 * For each word: rd[i] = (signed)rs1[i][31:16] * (unsigned)rs2[i][31:16]
 */
uint64_t HELPER(pmulsu_w_h11)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        int32_t mul = (int32_t)s1_h1 * (uint32_t)s2_h1;
        rd = INSERT32(rd, (uint32_t)mul, i);
    }
    return rd;
}

/**
 * PMULU.W.H00 - Unsigned multiply, low halfwords
 * For each word: rd[i] = rs1[i][15:0] * rs2[i][15:0] (unsigned)
 */
uint64_t HELPER(pmulu_w_h00)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h0 = EXTRACT16(rs1, i * 2);
        uint16_t s2_h0 = EXTRACT16(rs2, i * 2);
        uint32_t mul = (uint32_t)s1_h0 * (uint32_t)s2_h0;
        rd = INSERT32(rd, mul, i);
    }
    return rd;
}

/**
 * PMULU.W.H01 - Unsigned multiply, low halfword x high halfword
 * For each word: rd[i] = rs1[i][15:0] * rs2[i][31:16] (unsigned)
 */
uint64_t HELPER(pmulu_w_h01)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h0 = EXTRACT16(rs1, i * 2);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        uint32_t mul = (uint32_t)s1_h0 * (uint32_t)s2_h1;
        rd = INSERT32(rd, mul, i);
    }
    return rd;
}

/**
 * PMULU.W.H11 - Unsigned multiply, high halfwords
 * For each word: rd[i] = rs1[i][31:16] * rs2[i][31:16] (unsigned)
 */
uint64_t HELPER(pmulu_w_h11)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h1 = EXTRACT16(rs1, i * 2 + 1);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        uint32_t mul = (uint32_t)s1_h1 * (uint32_t)s2_h1;
        rd = INSERT32(rd, mul, i);
    }
    return rd;
}

/**
 * PM2SADD.H - Packed saturating multiply-add (non-crossed)
 *
 * For each 32-bit word:
 *   result = sat32(rs1[31:16] * rs2[31:16] + rs1[15:0] * rs2[15:0])
 *
 * Special case: if both halfwords in both sources are 0x8000 (-32768),
 *   result saturates to 0x7FFFFFFF and sets vxsat
 */
target_ulong HELPER(pm2sadd_h)(CPURISCVState *env,
                                target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);  /* Number of 32-bit words */
    int global_sat = 0;

    for (int i = 0; i < elems; i++) {
        /* Extract both halfwords from each source for this word */
        uint32_t s1_word = EXTRACT32(s1, i);
        uint32_t s2_word = EXTRACT32(s2, i);

        int16_t s1_h0 = (int16_t)EXTRACT16(s1_word, 0);
        int16_t s1_h1 = (int16_t)EXTRACT16(s1_word, 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(s2_word, 0);
        int16_t s2_h1 = (int16_t)EXTRACT16(s2_word, 1);

        uint32_t result;

        /* Check for the special saturation case: all halfwords are -32768 */
        if ((s1_h0 == -32768) && (s1_h1 == -32768) &&
            (s2_h0 == -32768) && (s2_h1 == -32768)) {
            result = 0x7FFFFFFF;
            global_sat = 1;
        } else {
            /* Normal case: compute products and sum */
            int32_t mul_00 = (int32_t)s1_h0 * (int32_t)s2_h0;
            int32_t mul_11 = (int32_t)s1_h1 * (int32_t)s2_h1;

            /* The sum may overflow 32 bits; the result is truncated. */
            result = (uint32_t)(mul_00 + mul_11);
        }

        rd = INSERT32(rd, result, i);
    }

    if (global_sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PM2SADD.HX - Packed saturating multiply-add crossed
 *
 * For each 32-bit word:
 *   result = sat32(rs1[31:16] * rs2[15:0] + rs1[15:0] * rs2[31:16])
 *
 * Special case: if both halfwords in both sources are 0x8000 (-32768),
 *   result saturates to 0x7FFFFFFF and sets vxsat
 */
target_ulong HELPER(pm2sadd_hx)(CPURISCVState *env,
                                 target_ulong s1, target_ulong s2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);  /* Number of 32-bit words */
    int global_sat = 0;

    for (int i = 0; i < elems; i++) {
        /* Extract both halfwords from each source for this word */
        uint32_t s1_word = EXTRACT32(s1, i);
        uint32_t s2_word = EXTRACT32(s2, i);

        int16_t s1_h0 = (int16_t)EXTRACT16(s1_word, 0);
        int16_t s1_h1 = (int16_t)EXTRACT16(s1_word, 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(s2_word, 0);
        int16_t s2_h1 = (int16_t)EXTRACT16(s2_word, 1);

        uint32_t result;

        /* Check for the special saturation case: all halfwords are -32768 */
        if ((s1_h0 == -32768) && (s1_h1 == -32768) &&
            (s2_h0 == -32768) && (s2_h1 == -32768)) {
            result = 0x7FFFFFFF;
            global_sat = 1;
        } else {
            /* Crossed products: s1_h0 * s2_h1 and s1_h1 * s2_h0 */
            int32_t mul_01 = (int32_t)s1_h0 * (int32_t)s2_h1;
            int32_t mul_10 = (int32_t)s1_h1 * (int32_t)s2_h0;

            /* Sum the crossed products */
            result = (uint32_t)(mul_01 + mul_10);
        }

        rd = INSERT32(rd, result, i);
    }

    if (global_sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * MUL.H00 - 32-bit signed multiply, low halfwords
 * Returns product of low halfwords of rs1 and rs2
 */
uint32_t HELPER(mul_h00)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int32_t mul = (int32_t)s1_h0 * (int32_t)s2_h0;
    return (uint32_t)mul;
}

/**
 * MUL.H01 - 32-bit signed multiply, rs1 low halfword x rs2 high halfword
 * Returns product of low halfword of rs1 and high halfword of rs2
 */
uint32_t HELPER(mul_h01)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int32_t mul = (int32_t)s1_h0 * (int32_t)s2_h1;
    return (uint32_t)mul;
}

/**
 * MUL.H11 - 32-bit signed multiply, high halfwords
 * Returns product of high halfwords of rs1 and rs2
 */
uint32_t HELPER(mul_h11)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int32_t mul = (int32_t)s1_h1 * (int32_t)s2_h1;
    return (uint32_t)mul;
}

/**
 * MULSU.H00 - 32-bit signed x unsigned multiply, low halfwords
 * Returns product of low halfword of rs1 (signed)
 * and low halfword of rs2 (unsigned)
 */
uint32_t HELPER(mulsu_h00)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    int32_t mul = (int32_t)s1_h0 * (uint32_t)s2_h0;
    return (uint32_t)mul;
}

/**
 * MULSU.H11 - 32-bit signed x unsigned multiply, high halfwords
 * Returns product of high halfword of rs1 (signed)
 * and high halfword of rs2 (unsigned)
 */
uint32_t HELPER(mulsu_h11)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    int32_t mul = (int32_t)s1_h1 * (uint32_t)s2_h1;
    return (uint32_t)mul;
}

/**
 * MULU.H00 - 32-bit unsigned multiply, low halfwords
 * Returns product of low halfwords of rs1 and rs2 (unsigned)
 */
uint32_t HELPER(mulu_h00)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint16_t s1_h0 = EXTRACT16(rs1, 0);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    uint32_t mul = (uint32_t)s1_h0 * (uint32_t)s2_h0;
    return mul;
}

/**
 * MULU.H01 - 32-bit unsigned multiply, rs1 low halfword x rs2 high halfword
 * Returns product of low halfword of rs1 and high halfword of rs2 (unsigned)
 */
uint32_t HELPER(mulu_h01)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint16_t s1_h0 = EXTRACT16(rs1, 0);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint32_t mul = (uint32_t)s1_h0 * (uint32_t)s2_h1;
    return mul;
}

/**
 * MULU.H11 - 32-bit unsigned multiply, high halfwords
 * Returns product of high halfwords of rs1 and rs2 (unsigned)
 */
uint32_t HELPER(mulu_h11)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint16_t s1_h1 = EXTRACT16(rs1, 1);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint32_t mul = (uint32_t)s1_h1 * (uint32_t)s2_h1;
    return mul;
}

/**
 * MUL.W00 - 64-bit signed multiply, low word x low word
 * Returns full 64-bit product of low 32 bits of rs1 and rs2
 */
uint64_t HELPER(mul_w00)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int64_t mul = (int64_t)s1_w0 * (int64_t)s2_w0;
    return (uint64_t)mul;
}

/**
 * MUL.W01 - 64-bit signed multiply, low word x high word
 * Returns full 64-bit product of low 32 bits of rs1 and high 32 bits of rs2
 */
uint64_t HELPER(mul_w01)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t mul = (int64_t)s1_w0 * (int64_t)s2_w1;
    return (uint64_t)mul;
}

/**
 * MUL.W11 - 64-bit signed multiply, high word x high word
 * Returns full 64-bit product of high 32 bits of rs1 and high 32 bits of rs2
 */
uint64_t HELPER(mul_w11)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t mul = (int64_t)s1_w1 * (int64_t)s2_w1;
    return (uint64_t)mul;
}

/**
 * MULSU.W00 - 64-bit signed x unsigned multiply, low word x low word
 * Returns full 64-bit product of low 32 bits of rs1
 * (signed) and low 32 bits of rs2 (unsigned)
 */
uint64_t HELPER(mulsu_w00)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    uint32_t s2_w0 = EXTRACT32(rs2, 0);
    int64_t mul = (int64_t)s1_w0 * (uint64_t)s2_w0;
    return (uint64_t)mul;
}

/**
 * MULSU.W11 - 64-bit signed x unsigned multiply, high word x high word
 * Returns full 64-bit product of high 32 bits of rs1
 * (signed) and high 32 bits of rs2 (unsigned)
 */
uint64_t HELPER(mulsu_w11)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    int64_t mul = (int64_t)s1_w1 * (uint64_t)s2_w1;
    return (uint64_t)mul;
}

/**
 * MULU.W00 - 64-bit unsigned multiply, low word x low word
 * Returns full 64-bit product of low 32 bits of rs1
 * and low 32 bits of rs2 (unsigned)
 */
uint64_t HELPER(mulu_w00)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint32_t s1_w0 = EXTRACT32(rs1, 0);
    uint32_t s2_w0 = EXTRACT32(rs2, 0);
    uint64_t mul = (uint64_t)s1_w0 * (uint64_t)s2_w0;
    return mul;
}

/**
 * MULU.W01 - 64-bit unsigned multiply, low word x high word
 * Returns full 64-bit product of low 32 bits of rs1
 * and high 32 bits of rs2 (unsigned)
 */
uint64_t HELPER(mulu_w01)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint32_t s1_w0 = EXTRACT32(rs1, 0);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    uint64_t mul = (uint64_t)s1_w0 * (uint64_t)s2_w1;
    return mul;
}

/**
 * MULU.W11 - 64-bit unsigned multiply, high word x high word
 * Returns full 64-bit product of high 32 bits of rs1
 * and high 32 bits of rs2 (unsigned)
 */
uint64_t HELPER(mulu_w11)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint32_t s1_w1 = EXTRACT32(rs1, 1);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    uint64_t mul = (uint64_t)s1_w1 * (uint64_t)s2_w1;
    return mul;
}

/* Multiply-Accumulate Operations */

/**
 * PMHACC.H - Packed signed 16-bit multiply high with accumulate
 */
target_ulong HELPER(pmhacc_h)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int16_t d = (int16_t)EXTRACT16(dest, i);
        int32_t prod = (int32_t)e1 * (int32_t)e2;
        int16_t high = (int16_t)(prod >> 16);
        uint16_t res = (uint16_t)(high + d);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHACCSU.H - Packed signed x unsigned 16-bit multiply high with accumulate
 */
target_ulong HELPER(pmhaccsu_h)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        int16_t d = (int16_t)EXTRACT16(dest, i);
        int32_t prod = (int32_t)e1 * (uint32_t)e2;
        int16_t high = (int16_t)(prod >> 16);
        uint16_t res = (uint16_t)(high + d);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHACCU.H - Packed unsigned 16-bit multiply high with accumulate
 */
target_ulong HELPER(pmhaccu_h)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t d = (uint16_t)EXTRACT16(dest, i);
        uint32_t prod = (uint32_t)e1 * (uint32_t)e2;
        uint16_t high = (uint16_t)(prod >> 16);
        uint16_t res = high + d;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHRACC.H - Packed signed 16-bit multiply high with rounding and accumulate
 */
target_ulong HELPER(pmhracc_h)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        int16_t d = (int16_t)EXTRACT16(dest, i);
        int32_t prod = (int32_t)e1 * (int32_t)e2 + (1 << 15);
        int16_t high = (int16_t)(prod >> 16);
        uint16_t res = (uint16_t)(high + d);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHRACCSU.H - Packed signed x unsigned 16-bit
 * multiply high with rounding and accumulate
 */
target_ulong HELPER(pmhraccsu_h)(CPURISCVState *env, target_ulong rs1,
                                 target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        int16_t d = (int16_t)EXTRACT16(dest, i);
        int32_t prod = (int32_t)e1 * (uint32_t)e2 + (1 << 15);
        int16_t high = (int16_t)(prod >> 16);
        uint16_t res = (uint16_t)(high + d);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHRACCU.H - Packed unsigned 16-bit multiply
 * high with rounding and accumulate
 */
target_ulong HELPER(pmhraccu_h)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t e1 = EXTRACT16(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i);
        uint16_t d = (uint16_t)EXTRACT16(dest, i);
        uint32_t prod = (uint32_t)e1 * (uint32_t)e2 + (1 << 15);
        uint16_t high = (uint16_t)(prod >> 16);
        uint16_t res = high + d;
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHACC.W - Packed signed 32-bit multiply high with accumulate (RV64 only)
 */
uint64_t HELPER(pmhacc_w)(CPURISCVState *env, uint64_t rs1,
                          uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)e1 * (int64_t)e2;
        int32_t high = (int32_t)(prod >> 32);
        uint32_t res = (uint32_t)(high + d);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMHRACC.W - Packed signed 32-bit multiply high
 * with rounding and accumulate (RV64 only)
 */
uint64_t HELPER(pmhracc_w)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)e1 * (int64_t)e2 + (1LL << 31);
        int32_t high = (int32_t)(prod >> 32);
        uint32_t res = (uint32_t)(high + d);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMHACCSU.W - Packed signed x unsigned 32-bit
 * multiply high with accumulate (RV64 only)
 */
uint64_t HELPER(pmhaccsu_w)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)e1 * (uint64_t)e2;
        int32_t high = (int32_t)(prod >> 32);
        uint32_t res = (uint32_t)(high + d);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMHRACCSU.W - Packed signed x unsigned 32-bit
 * multiply high with rounding and accumulate
 * (RV64 only)
 */
uint64_t HELPER(pmhraccsu_w)(CPURISCVState *env, uint64_t rs1,
                             uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)e1 * (uint64_t)e2 + (1LL << 31);
        int32_t high = (int32_t)(prod >> 32);
        uint32_t res = (uint32_t)(high + d);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMHACCU.W - Packed unsigned 32-bit multiply high with accumulate (RV64 only)
 */
uint64_t HELPER(pmhaccu_w)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t d = EXTRACT32(dest, i);
        uint64_t prod = (uint64_t)e1 * (uint64_t)e2;
        uint32_t high = (uint32_t)(prod >> 32);
        uint32_t res = high + d;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMHRACCU.W - Packed unsigned 32-bit multiply
 * high with rounding and accumulate (RV64 only)
 */
uint64_t HELPER(pmhraccu_w)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint32_t e1 = EXTRACT32(rs1, i);
        uint32_t e2 = EXTRACT32(rs2, i);
        uint32_t d = EXTRACT32(dest, i);
        uint64_t prod = (uint64_t)e1 * (uint64_t)e2 + (1LL << 31);
        uint32_t high = (uint32_t)(prod >> 32);
        uint32_t res = high + d;
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * MHACC - 32-bit signed multiply high with accumulate
 */
uint32_t HELPER(mhacc)(CPURISCVState *env, uint32_t rs1,
                        uint32_t rs2, uint32_t dest)
{
    int32_t a = (int32_t)rs1;
    int32_t b = (int32_t)rs2;
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)a * (int64_t)b;
    return (uint32_t)(d + (prod >> 32));
}

/**
 * MHRACC - 32-bit signed multiply high with rounding and accumulate
 */
uint32_t HELPER(mhracc)(CPURISCVState *env, uint32_t rs1,
                         uint32_t rs2, uint32_t dest)
{
    int32_t a = (int32_t)rs1;
    int32_t b = (int32_t)rs2;
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)a * (int64_t)b + (1LL << 31);
    return (uint32_t)(d + (prod >> 32));
}

/**
 * MHACCSU - 32-bit signed x unsigned multiply high with accumulate
 */
uint32_t HELPER(mhaccsu)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint32_t dest)
{
    int32_t a = (int32_t)rs1;
    uint32_t b = rs2;
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)a * (uint64_t)b;
    return (uint32_t)(d + (prod >> 32));
}

/**
 * MHRACCSU - 32-bit signed x unsigned multiply high
 * with rounding and accumulate
 */
uint32_t HELPER(mhraccsu)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    int32_t a = (int32_t)rs1;
    uint32_t b = rs2;
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)a * (uint64_t)b + (1LL << 31);
    return (uint32_t)(d + (prod >> 32));
}

/**
 * MHACCU - 32-bit unsigned multiply high with accumulate
 */
uint32_t HELPER(mhaccu)(CPURISCVState *env, uint32_t rs1,
                         uint32_t rs2, uint32_t dest)
{
    uint32_t a = rs1;
    uint32_t b = rs2;
    uint32_t d = dest;
    uint64_t prod = (uint64_t)a * (uint64_t)b;
    return (uint32_t)(d + (prod >> 32));
}

/**
 * MHRACCU - 32-bit unsigned multiply high with rounding and accumulate
 */
uint32_t HELPER(mhraccu)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint32_t dest)
{
    uint32_t a = rs1;
    uint32_t b = rs2;
    uint32_t d = dest;
    uint64_t prod = (uint64_t)a * (uint64_t)b + (1LL << 31);
    return (uint32_t)(d + (prod >> 32));
}

/**
 * PMHACC.H.B0 - Multiply halfword by low byte and accumulate (high halfword)
 */
target_ulong HELPER(pmhacc_h_b0)(CPURISCVState *env, target_ulong rs1,
                                 target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i * 2);
        int16_t d = (int16_t)EXTRACT16(dest, i);
        int32_t prod = (int32_t)e1 * (int32_t)e2;
        int16_t high = (int16_t)(prod >> 8);
        uint16_t res = (uint16_t)(high + d);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHACC.H.B1 - Multiply halfword by high byte and accumulate (high halfword)
 */
target_ulong HELPER(pmhacc_h_b1)(CPURISCVState *env, target_ulong rs1,
                                 target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int8_t e2 = (int8_t)EXTRACT8(rs2, i * 2 + 1);
        int16_t d = (int16_t)EXTRACT16(dest, i);
        int32_t prod = (int32_t)e1 * (int32_t)e2;
        int16_t high = (int16_t)(prod >> 8);
        uint16_t res = (uint16_t)(high + d);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHACCSU.H.B0 - Multiply signed halfword by unsigned low byte and accumulate
 */
target_ulong HELPER(pmhaccsu_h_b0)(CPURISCVState *env, target_ulong rs1,
                                   target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i * 2);
        int16_t d = (int16_t)EXTRACT16(dest, i);
        int32_t prod = (int32_t)e1 * (uint32_t)e2;
        int16_t high = (int16_t)(prod >> 8);
        uint16_t res = (uint16_t)(high + d);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * PMHACCSU.H.B1 - Multiply signed halfword by unsigned high byte and accumulate
 */
target_ulong HELPER(pmhaccsu_h_b1)(CPURISCVState *env, target_ulong rs1,
                                   target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i * 2 + 1);
        int16_t d = (int16_t)EXTRACT16(dest, i);
        int32_t prod = (int32_t)e1 * (uint32_t)e2;
        int16_t high = (int16_t)(prod >> 8);
        uint16_t res = (uint16_t)(high + d);
        rd = INSERT16(rd, res, i);
    }
    return rd;
}

/**
 * MHACC.H0 - 32-bit multiply by low halfword high accumulate
 */
uint32_t HELPER(mhacc_h0)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    int32_t a = (int32_t)rs1;
    int16_t b = (int16_t)(rs2 & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)a * (int64_t)b;
    return (uint32_t)(d + (prod >> 16));
}

/**
 * MHACC.H1 - 32-bit multiply by high halfword high accumulate
 */
uint32_t HELPER(mhacc_h1)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    int32_t a = (int32_t)rs1;
    int16_t b = (int16_t)((rs2 >> 16) & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)a * (int64_t)b;
    return (uint32_t)(d + (prod >> 16));
}

/**
 * MHACCSU.H0 - 32-bit signed multiply by unsigned low halfword high accumulate
 */
uint32_t HELPER(mhaccsu_h0)(CPURISCVState *env, uint32_t rs1,
                             uint32_t rs2, uint32_t dest)
{
    int32_t a = (int32_t)rs1;
    uint16_t b = (uint16_t)(rs2 & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)a * (uint64_t)b;
    return (uint32_t)(d + (prod >> 16));
}

/**
 * MHACCSU.H1 - 32-bit signed multiply by unsigned high halfword high accumulate
 */
uint32_t HELPER(mhaccsu_h1)(CPURISCVState *env, uint32_t rs1,
                             uint32_t rs2, uint32_t dest)
{
    int32_t a = (int32_t)rs1;
    uint16_t b = (uint16_t)((rs2 >> 16) & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)a * (uint64_t)b;
    return (uint32_t)(d + (prod >> 16));
}

/**
 * PMHACC.W.H0 - Multiply word by low halfword high accumulate (RV64 only)
 */
uint64_t HELPER(pmhacc_w_h0)(CPURISCVState *env, uint64_t rs1,
                              uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i * 2);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)e1 * (int64_t)e2;
        int32_t high = (int32_t)(prod >> 16);
        uint32_t res = (uint32_t)(high + d);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMHACC.W.H1 - Multiply word by high halfword high accumulate (RV64 only)
 */
uint64_t HELPER(pmhacc_w_h1)(CPURISCVState *env, uint64_t rs1,
                              uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)e1 * (int64_t)e2;
        int32_t high = (int32_t)(prod >> 16);
        uint32_t res = (uint32_t)(high + d);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMHACCSU.W.H0 - Multiply signed word by unsigned low halfword
 * high accumulate (RV64 only)
 */
uint64_t HELPER(pmhaccsu_w_h0)(CPURISCVState *env, uint64_t rs1,
                                uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i * 2);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)e1 * (uint64_t)e2;
        int32_t high = (int32_t)(prod >> 16);
        uint32_t res = (uint32_t)(high + d);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMHACCSU.W.H1 - Multiply signed word by unsigned high halfword
 * high accumulate (RV64 only)
 */
uint64_t HELPER(pmhaccsu_w_h1)(CPURISCVState *env, uint64_t rs1,
                                uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        uint16_t e2 = EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)e1 * (uint64_t)e2;
        int32_t high = (int32_t)(prod >> 16);
        uint32_t res = (uint32_t)(high + d);
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMACC.W.H00 - Packed multiply-accumulate, low halfwords
 * For each word: rd[i] = dest[i] + (rs1[i][15:0] * rs2[i][15:0])
 */
uint64_t HELPER(pmacc_w_h00)(CPURISCVState *env, uint64_t rs1,
                              uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int32_t d_h = (int32_t)EXTRACT32(dest, i);
        int32_t mul = (int32_t)s1_h0 * (int32_t)s2_h0;
        rd = INSERT32(rd, (uint32_t)(d_h + mul), i);
    }
    return rd;
}

/**
 * PMACC.W.H01 - Packed multiply-accumulate, rs1 low x rs2 high
 * For each word: rd[i] = dest[i] + (rs1[i][15:0] * rs2[i][31:16])
 */
uint64_t HELPER(pmacc_w_h01)(CPURISCVState *env, uint64_t rs1,
                              uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d_h = (int32_t)EXTRACT32(dest, i);
        int32_t mul = (int32_t)s1_h0 * (int32_t)s2_h1;
        rd = INSERT32(rd, (uint32_t)(d_h + mul), i);
    }
    return rd;
}

/**
 * PMACC.W.H11 - Packed multiply-accumulate, high halfwords
 * For each word: rd[i] = dest[i] + (rs1[i][31:16] * rs2[i][31:16])
 */
uint64_t HELPER(pmacc_w_h11)(CPURISCVState *env, uint64_t rs1,
                              uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d_h = (int32_t)EXTRACT32(dest, i);
        int32_t mul = (int32_t)s1_h1 * (int32_t)s2_h1;
        rd = INSERT32(rd, (uint32_t)(d_h + mul), i);
    }
    return rd;
}

/**
 * PMACCSU.W.H00 - Packed signed x unsigned multiply-accumulate, low halfwords
 * For each word: rd[i] = dest[i] +
 * (signed)rs1[i][15:0] * (unsigned)rs2[i][15:0]
 */
uint64_t HELPER(pmaccsu_w_h00)(CPURISCVState *env, uint64_t rs1,
                                uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        uint16_t s2_h0 = EXTRACT16(rs2, i * 2);
        int32_t d_h = (int32_t)EXTRACT32(dest, i);
        int32_t mul = (int32_t)s1_h0 * (uint32_t)s2_h0;
        rd = INSERT32(rd, (uint32_t)(d_h + mul), i);
    }
    return rd;
}

/**
 * PMACCSU.W.H11 - Packed signed x unsigned multiply-accumulate, high halfwords
 * For each word: rd[i] = dest[i] +
 * (signed)rs1[i][31:16] * (unsigned)rs2[i][31:16]
 */
uint64_t HELPER(pmaccsu_w_h11)(CPURISCVState *env, uint64_t rs1,
                                uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        int32_t d_h = (int32_t)EXTRACT32(dest, i);
        int32_t mul = (int32_t)s1_h1 * (uint32_t)s2_h1;
        rd = INSERT32(rd, (uint32_t)(d_h + mul), i);
    }
    return rd;
}

/**
 * PMACCU.W.H00 - Packed unsigned multiply-accumulate, low halfwords
 * For each word: rd[i] = dest[i] + rs1[i][15:0] * rs2[i][15:0] (unsigned)
 */
uint64_t HELPER(pmaccu_w_h00)(CPURISCVState *env, uint64_t rs1,
                               uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h0 = EXTRACT16(rs1, i * 2);
        uint16_t s2_h0 = EXTRACT16(rs2, i * 2);
        uint32_t d_h = EXTRACT32(dest, i);
        uint32_t mul = (uint32_t)s1_h0 * (uint32_t)s2_h0;
        rd = INSERT32(rd, d_h + mul, i);
    }
    return rd;
}

/**
 * PMACCU.W.H01 - Packed unsigned multiply-accumulate, rs1 low x rs2 high
 * For each word: rd[i] = dest[i] + rs1[i][15:0] * rs2[i][31:16] (unsigned)
 */
uint64_t HELPER(pmaccu_w_h01)(CPURISCVState *env, uint64_t rs1,
                               uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h0 = EXTRACT16(rs1, i * 2);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        uint32_t d_h = EXTRACT32(dest, i);
        uint32_t mul = (uint32_t)s1_h0 * (uint32_t)s2_h1;
        rd = INSERT32(rd, d_h + mul, i);
    }
    return rd;
}

/**
 * PMACCU.W.H11 - Packed unsigned multiply-accumulate, high halfwords
 * For each word: rd[i] = dest[i] + rs1[i][31:16] * rs2[i][31:16] (unsigned)
 */
uint64_t HELPER(pmaccu_w_h11)(CPURISCVState *env, uint64_t rs1,
                               uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;
    int elems = 2;

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h1 = EXTRACT16(rs1, i * 2 + 1);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        uint32_t d_h = EXTRACT32(dest, i);
        uint32_t mul = (uint32_t)s1_h1 * (uint32_t)s2_h1;
        rd = INSERT32(rd, d_h + mul, i);
    }
    return rd;
}

/**
 * MACC.H00 - 32-bit signed multiply-accumulate, low halfwords
 * dest = dest + (rs1[15:0] * rs2[15:0])
 */
uint32_t HELPER(macc_h00)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint32_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int32_t d_h = (int32_t)dest;
    int32_t mul = (int32_t)s1_h0 * (int32_t)s2_h0;
    return (uint32_t)(d_h + mul);
}

/**
 * MACC.H01 - 32-bit signed multiply-accumulate, rs1 low x rs2 high
 * dest = dest + (rs1[15:0] * rs2[31:16])
 */
uint32_t HELPER(macc_h01)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint32_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int32_t d_h = (int32_t)dest;
    int32_t mul = (int32_t)s1_h0 * (int32_t)s2_h1;
    return (uint32_t)(d_h + mul);
}

/**
 * MACC.H11 - 32-bit signed multiply-accumulate, high halfwords
 * dest = dest + (rs1[31:16] * rs2[31:16])
 */
uint32_t HELPER(macc_h11)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint32_t dest)
{
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int32_t d_h = (int32_t)dest;
    int32_t mul = (int32_t)s1_h1 * (int32_t)s2_h1;
    return (uint32_t)(d_h + mul);
}

/**
 * MACCSU.H00 - 32-bit signed x unsigned multiply-accumulate, low halfwords
 * dest = dest + (rs1[15:0] * rs2[15:0]) with rs2 unsigned
 */
uint32_t HELPER(maccsu_h00)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint32_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    int32_t d_h = (int32_t)dest;
    int32_t mul = (int32_t)s1_h0 * (uint32_t)s2_h0;
    return (uint32_t)(d_h + mul);
}

/**
 * MACCSU.H11 - 32-bit signed x unsigned multiply-accumulate, high halfwords
 * dest = dest + (rs1[31:16] * rs2[31:16]) with rs2 unsigned
 */
uint32_t HELPER(maccsu_h11)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint32_t dest)
{
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    int32_t d_h = (int32_t)dest;
    int32_t mul = (int32_t)s1_h1 * (uint32_t)s2_h1;
    return (uint32_t)(d_h + mul);
}

/**
 * MACCU.H00 - 32-bit unsigned multiply-accumulate, low halfwords
 * dest = dest + (rs1[15:0] * rs2[15:0]) (unsigned)
 */
uint32_t HELPER(maccu_h00)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    uint16_t s1_h0 = EXTRACT16(rs1, 0);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    uint32_t d_h = dest;
    uint32_t mul = (uint32_t)s1_h0 * (uint32_t)s2_h0;
    return d_h + mul;
}

/**
 * MACCU.H01 - 32-bit unsigned multiply-accumulate, rs1 low x rs2 high
 * dest = dest + (rs1[15:0] * rs2[31:16]) (unsigned)
 */
uint32_t HELPER(maccu_h01)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    uint16_t s1_h0 = EXTRACT16(rs1, 0);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint32_t d_h = dest;
    uint32_t mul = (uint32_t)s1_h0 * (uint32_t)s2_h1;
    return d_h + mul;
}

/**
 * MACCU.H11 - 32-bit unsigned multiply-accumulate, high halfwords
 * dest = dest + (rs1[31:16] * rs2[31:16]) (unsigned)
 */
uint32_t HELPER(maccu_h11)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    uint16_t s1_h1 = EXTRACT16(rs1, 1);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint32_t d_h = dest;
    uint32_t mul = (uint32_t)s1_h1 * (uint32_t)s2_h1;
    return d_h + mul;
}

/**
 * MACC.W00 - 64-bit signed multiply-accumulate, low word x low word
 * dest = dest + (rs1[31:0] * rs2[31:0])
 */
uint64_t HELPER(macc_w00)(CPURISCVState *env, uint64_t rs1,
                          uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int64_t d_w = (int64_t)dest;
    int64_t mul = (int64_t)s1_w0 * (int64_t)s2_w0;
    return (uint64_t)(d_w + mul);
}

/**
 * MACC.W01 - 64-bit signed multiply-accumulate, low word x high word
 * dest = dest + (rs1[31:0] * rs2[63:32])
 */
uint64_t HELPER(macc_w01)(CPURISCVState *env, uint64_t rs1,
                          uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t d_w = (int64_t)dest;
    int64_t mul = (int64_t)s1_w0 * (int64_t)s2_w1;
    return (uint64_t)(d_w + mul);
}

/**
 * MACC.W11 - 64-bit signed multiply-accumulate, high word x high word
 * dest = dest + (rs1[63:32] * rs2[63:32])
 */
uint64_t HELPER(macc_w11)(CPURISCVState *env, uint64_t rs1,
                          uint64_t rs2, uint64_t dest)
{
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t d_w = (int64_t)dest;
    int64_t mul = (int64_t)s1_w1 * (int64_t)s2_w1;
    return (uint64_t)(d_w + mul);
}

/**
 * MACCSU.W00 - 64-bit signed x unsigned
 * multiply-accumulate, low word x low word
 * dest = dest + (rs1[31:0] * rs2[31:0]) with rs2 interpreted as unsigned
 */
uint64_t HELPER(maccsu_w00)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    uint32_t s2_w0 = EXTRACT32(rs2, 0);
    int64_t d_w = (int64_t)dest;
    int64_t mul = (int64_t)s1_w0 * (uint64_t)s2_w0;
    return (uint64_t)(d_w + mul);
}

/**
 * MACCSU.W11 - 64-bit signed x unsigned
 * multiply-accumulate, high word x high word
 * dest = dest + (rs1[63:32] * rs2[63:32]) with rs2 interpreted as unsigned
 */
uint64_t HELPER(maccsu_w11)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    int64_t d_w = (int64_t)dest;
    int64_t mul = (int64_t)s1_w1 * (uint64_t)s2_w1;
    return (uint64_t)(d_w + mul);
}

/**
 * MACCU.W00 - 64-bit unsigned multiply-accumulate, low word x low word
 * dest = dest + (rs1[31:0] * rs2[31:0]) (unsigned)
 */
uint64_t HELPER(maccu_w00)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    uint32_t s1_w0 = EXTRACT32(rs1, 0);
    uint32_t s2_w0 = EXTRACT32(rs2, 0);
    uint64_t d_w = dest;
    uint64_t mul = (uint64_t)s1_w0 * (uint64_t)s2_w0;
    return d_w + mul;
}

/**
 * MACCU.W01 - 64-bit unsigned multiply-accumulate, low word x high word
 * dest = dest + (rs1[31:0] * rs2[63:32]) (unsigned)
 */
uint64_t HELPER(maccu_w01)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    uint32_t s1_w0 = EXTRACT32(rs1, 0);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    uint64_t d_w = dest;
    uint64_t mul = (uint64_t)s1_w0 * (uint64_t)s2_w1;
    return d_w + mul;
}

/**
 * MACCU.W11 - 64-bit unsigned multiply-accumulate, high word x high word
 * dest = dest + (rs1[63:32] * rs2[63:32]) (unsigned)
 */
uint64_t HELPER(maccu_w11)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    uint32_t s1_w1 = EXTRACT32(rs1, 1);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    uint64_t d_w = dest;
    uint64_t mul = (uint64_t)s1_w1 * (uint64_t)s2_w1;
    return d_w + mul;
}

/* Q-Format Multiplication Operations */

/**
 * PMULQ.H - Packed signed Q-format multiply (fractional)
 */
target_ulong HELPER(pmulq_h)(CPURISCVState *env, target_ulong rs1,
                             target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        uint16_t result;

        if ((e1 == -32768) && (e2 == -32768)) {
            sat = 1;
            result = 0x7FFF;
        } else {
            int32_t prod = (int32_t)e1 * (int32_t)e2;
            result = (prod >> 15) & 0xFFFF;
        }
        rd = INSERT16(rd, result, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PMULQR.H - Packed signed Q-format multiply with rounding
 */
target_ulong HELPER(pmulqr_h)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_H(rd);
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int16_t e1 = (int16_t)EXTRACT16(rs1, i);
        int16_t e2 = (int16_t)EXTRACT16(rs2, i);
        uint16_t result;

        if ((e1 == -32768) && (e2 == -32768)) {
            sat = 1;
            result = 0x7FFF;
        } else {
            int32_t prod = (int32_t)e1 * (int32_t)e2 + (1 << 14);
            result = (prod >> 15) & 0xFFFF;
        }
        rd = INSERT16(rd, result, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PMULQ.W - Packed signed 32-bit Q-format multiply (RV64 only)
 */
uint64_t HELPER(pmulq_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        uint32_t result;

        if ((e1 == -2147483647 - 1) && (e2 == -2147483647 - 1)) {
            sat = 1;
            result = 0x7FFFFFFF;
        } else {
            int64_t prod = (int64_t)e1 * (int64_t)e2;
            result = (uint32_t)(prod >> 31);
        }
        rd = INSERT32(rd, result, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PMULQR.W - Packed signed 32-bit Q-format multiply with rounding (RV64 only)
 */
uint64_t HELPER(pmulqr_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int elems = 2;
    int sat = 0;

    for (int i = 0; i < elems; i++) {
        int32_t e1 = (int32_t)EXTRACT32(rs1, i);
        int32_t e2 = (int32_t)EXTRACT32(rs2, i);
        uint32_t result;

        if ((e1 == -2147483647 - 1) && (e2 == -2147483647 - 1)) {
            sat = 1;
            result = 0x7FFFFFFF;
        } else {
            int64_t prod = (int64_t)e1 * (int64_t)e2 + (1LL << 30);
            result = (uint32_t)(prod >> 31);
        }
        rd = INSERT32(rd, result, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * MULQ - 32-bit signed Q-format multiply
 */
uint32_t HELPER(mulq)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int32_t b = (int32_t)rs2;

    if ((a == -2147483647 - 1) && (b == -2147483647 - 1)) {
        env->vxsat = 1;
        return 0x7FFFFFFF;
    } else {
        int64_t prod = (int64_t)a * (int64_t)b;
        return (uint32_t)(prod >> 31);
    }
}

/**
 * MULQR - 32-bit signed Q-format multiply with rounding
 */
uint32_t HELPER(mulqr)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int32_t a = (int32_t)rs1;
    int32_t b = (int32_t)rs2;

    if ((a == -2147483647 - 1) && (b == -2147483647 - 1)) {
        env->vxsat = 1;
        return 0x7FFFFFFF;
    } else {
        int64_t prod = (int64_t)a * (int64_t)b + (1LL << 30);
        return (uint32_t)(prod >> 31);
    }
}


/* Q-Format Multiply-Accumulate Operations */

/**
 * MQACC.H00 - Q-format multiply accumulate, both operands low halfword
 */
uint32_t HELPER(mqacc_h00)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    int16_t s1_h0 = (int16_t)(rs1 & 0xFFFF);
    int16_t s2_h0 = (int16_t)(rs2 & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)s1_h0 * (int64_t)s2_h0;
    return (uint32_t)(d + (int32_t)(prod >> 15));
}

/**
 * MQACC.H01 - Q-format multiply accumulate, rs1 low, rs2 high
 */
uint32_t HELPER(mqacc_h01)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    int16_t s1_h0 = (int16_t)(rs1 & 0xFFFF);
    int16_t s2_h1 = (int16_t)((rs2 >> 16) & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)s1_h0 * (int64_t)s2_h1;
    return (uint32_t)(d + (int32_t)(prod >> 15));
}

/**
 * MQACC.H11 - Q-format multiply accumulate, both operands high halfword
 */
uint32_t HELPER(mqacc_h11)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint32_t dest)
{
    int16_t s1_h1 = (int16_t)((rs1 >> 16) & 0xFFFF);
    int16_t s2_h1 = (int16_t)((rs2 >> 16) & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)s1_h1 * (int64_t)s2_h1;
    return (uint32_t)(d + (int32_t)(prod >> 15));
}

/**
 * MQRACC.H00 - Q-format multiply accumulate with rounding, both low halfword
 */
uint32_t HELPER(mqracc_h00)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint32_t dest)
{
    int16_t s1_h0 = (int16_t)(rs1 & 0xFFFF);
    int16_t s2_h0 = (int16_t)(rs2 & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)s1_h0 * (int64_t)s2_h0 + (1LL << 14);
    return (uint32_t)(d + (int32_t)(prod >> 15));
}

/**
 * MQRACC.H01 - Q-format multiply accumulate with rounding, rs1 low, rs2 high
 */
uint32_t HELPER(mqracc_h01)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint32_t dest)
{
    int16_t s1_h0 = (int16_t)(rs1 & 0xFFFF);
    int16_t s2_h1 = (int16_t)((rs2 >> 16) & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)s1_h0 * (int64_t)s2_h1 + (1LL << 14);
    return (uint32_t)(d + (int32_t)(prod >> 15));
}

/**
 * MQRACC.H11 - Q-format multiply accumulate with rounding, both high halfword
 */
uint32_t HELPER(mqracc_h11)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint32_t dest)
{
    int16_t s1_h1 = (int16_t)((rs1 >> 16) & 0xFFFF);
    int16_t s2_h1 = (int16_t)((rs2 >> 16) & 0xFFFF);
    int32_t d = (int32_t)dest;
    int64_t prod = (int64_t)s1_h1 * (int64_t)s2_h1 + (1LL << 14);
    return (uint32_t)(d + (int32_t)(prod >> 15));
}

/**
 * MQACC.W00 - Q-format multiply accumulate, both low word (RV64)
 */
uint64_t HELPER(mqacc_w00)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)(rs1 & 0xFFFFFFFF);
    int32_t s2_w0 = (int32_t)(rs2 & 0xFFFFFFFF);
    int64_t d = (int64_t)dest;
    int64_t prod = (int64_t)s1_w0 * (int64_t)s2_w0;
    __int128_t prod_95 = ((__int128_t)prod) >> 31;
    return (uint64_t)(d + (int64_t)prod_95);
}

/**
 * MQACC.W01 - Q-format multiply accumulate, rs1 low, rs2 high (RV64)
 */
uint64_t HELPER(mqacc_w01)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)(rs1 & 0xFFFFFFFF);
    int32_t s2_w1 = (int32_t)((rs2 >> 32) & 0xFFFFFFFF);
    int64_t d = (int64_t)dest;
    int64_t prod = (int64_t)s1_w0 * (int64_t)s2_w1;
    __int128_t prod_95 = ((__int128_t)prod) >> 31;
    return (uint64_t)(d + (int64_t)prod_95);
}

/**
 * MQACC.W11 - Q-format multiply accumulate, both high word (RV64)
 */
uint64_t HELPER(mqacc_w11)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    int32_t s1_w1 = (int32_t)((rs1 >> 32) & 0xFFFFFFFF);
    int32_t s2_w1 = (int32_t)((rs2 >> 32) & 0xFFFFFFFF);
    int64_t d = (int64_t)dest;
    int64_t prod = (int64_t)s1_w1 * (int64_t)s2_w1;
    __int128_t prod_95 = ((__int128_t)prod) >> 31;
    return (uint64_t)(d + (int64_t)prod_95);
}

/**
 * MQRACC.W00 - Q-format multiply accumulate with rounding,
 * both low word (RV64)
 */
uint64_t HELPER(mqracc_w00)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)(rs1 & 0xFFFFFFFF);
    int32_t s2_w0 = (int32_t)(rs2 & 0xFFFFFFFF);
    int64_t d = (int64_t)dest;
    int64_t prod = (int64_t)s1_w0 * (int64_t)s2_w0 + (1LL << 30);
    __int128_t prod_95 = ((__int128_t)prod) >> 31;
    return (uint64_t)(d + (int64_t)prod_95);
}

/**
 * MQRACC.W01 - Q-format multiply accumulate with rounding,
 * rs1 low, rs2 high (RV64)
 */
uint64_t HELPER(mqracc_w01)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)(rs1 & 0xFFFFFFFF);
    int32_t s2_w1 = (int32_t)((rs2 >> 32) & 0xFFFFFFFF);
    int64_t d = (int64_t)dest;
    int64_t prod = (int64_t)s1_w0 * (int64_t)s2_w1 + (1LL << 30);
    __int128_t prod_95 = ((__int128_t)prod) >> 31;
    return (uint64_t)(d + (int64_t)prod_95);
}

/**
 * MQRACC.W11 - Q-format multiply accumulate with rounding,
 * both high word (RV64)
 */
uint64_t HELPER(mqracc_w11)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    int32_t s1_w1 = (int32_t)((rs1 >> 32) & 0xFFFFFFFF);
    int32_t s2_w1 = (int32_t)((rs2 >> 32) & 0xFFFFFFFF);
    int64_t d = (int64_t)dest;
    int64_t prod = (int64_t)s1_w1 * (int64_t)s2_w1 + (1LL << 30);
    __int128_t prod_95 = ((__int128_t)prod) >> 31;
    return (uint64_t)(d + (int64_t)prod_95);
}

/**
 * PMQACC.W.H00 - Packed Q-format multiply accumulate,
 * low halfword (RV64)
 */
uint64_t HELPER(pmqacc_w_h00)(CPURISCVState *env, uint64_t rs1,
                              uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)s1_h0 * (int64_t)s2_h0;
        uint32_t res = (uint32_t)(d_w + (int32_t)(prod >> 15));
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMQACC.W.H01 - Packed Q-format multiply accumulate,
 * rs1 low, rs2 high (RV64)
 */
uint64_t HELPER(pmqacc_w_h01)(CPURISCVState *env, uint64_t rs1,
                              uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)s1_h0 * (int64_t)s2_h1;
        uint32_t res = (uint32_t)(d_w + (int32_t)(prod >> 15));
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMQACC.W.H11 - Packed Q-format multiply accumulate,
 * both high halfword (RV64)
 */
uint64_t HELPER(pmqacc_w_h11)(CPURISCVState *env, uint64_t rs1,
                              uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)s1_h1 * (int64_t)s2_h1;
        uint32_t res = (uint32_t)(d_w + (int32_t)(prod >> 15));
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMQRACC.W.H00 - Packed Q-format multiply accumulate
 * with rounding, low halfword (RV64)
 */
uint64_t HELPER(pmqracc_w_h00)(CPURISCVState *env, uint64_t rs1,
                               uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)s1_h0 * (int64_t)s2_h0 + (1LL << 14);
        uint32_t res = (uint32_t)(d_w + (int32_t)(prod >> 15));
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMQRACC.W.H01 - Packed Q-format multiply accumulate
 * with rounding, rs1 low, rs2 high (RV64)
 */
uint64_t HELPER(pmqracc_w_h01)(CPURISCVState *env, uint64_t rs1,
                               uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)s1_h0 * (int64_t)s2_h1 + (1LL << 14);
        uint32_t res = (uint32_t)(d_w + (int32_t)(prod >> 15));
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMQRACC.W.H11 - Packed Q-format multiply accumulate
 * with rounding, both high halfword (RV64)
 */
uint64_t HELPER(pmqracc_w_h11)(CPURISCVState *env, uint64_t rs1,
                               uint64_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)s1_h1 * (int64_t)s2_h1 + (1LL << 14);
        uint32_t res = (uint32_t)(d_w + (int32_t)(prod >> 15));
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/* Two-Way Multiply and Accumulate Operations */

/**
 * PMQ2ADD.H - Add two Q-format products
 */
target_ulong HELPER(pmq2add_h)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t prod0 = (int32_t)s1_h0 * (int32_t)s2_h0;
        int64_t prod0_47 = ((int64_t)prod0) >> 15;
        int32_t prod1 = (int32_t)s1_h1 * (int32_t)s2_h1;
        int64_t prod1_47 = ((int64_t)prod1) >> 15;
        uint32_t sum = (uint32_t)(prod0_47 + prod1_47);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PMQR2ADD.H - Add two Q-format products with rounding
 */
target_ulong HELPER(pmqr2add_h)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t prod0 = (int32_t)s1_h0 * (int32_t)s2_h0 + (1LL << 14);
        int64_t prod0_47 = ((int64_t)prod0) >> 15;
        int32_t prod1 = (int32_t)s1_h1 * (int32_t)s2_h1 + (1LL << 14);
        int64_t prod1_47 = ((int64_t)prod1) >> 15;
        uint32_t sum = (uint32_t)(prod0_47 + prod1_47);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PMQ2ADDA.H - Add two Q-format products with accumulate
 */
target_ulong HELPER(pmq2adda_h)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod0 = (int32_t)s1_h0 * (int32_t)s2_h0;
        int64_t prod0_47 = ((int64_t)prod0) >> 15;
        int32_t prod1 = (int32_t)s1_h1 * (int32_t)s2_h1;
        int64_t prod1_47 = ((int64_t)prod1) >> 15;
        uint32_t sum = (uint32_t)(d + prod0_47 + prod1_47);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PMQR2ADDA.H - Add two Q-format products with rounding and accumulate
 */
target_ulong HELPER(pmqr2adda_h)(CPURISCVState *env, target_ulong rs1,
                                 target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod0 = (int32_t)s1_h0 * (int32_t)s2_h0 + (1LL << 14);
        int64_t prod0_47 = ((int64_t)prod0) >> 15;
        int32_t prod1 = (int32_t)s1_h1 * (int32_t)s2_h1 + (1LL << 14);
        int64_t prod1_47 = ((int64_t)prod1) >> 15;
        uint32_t sum = (uint32_t)(d + prod0_47 + prod1_47);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PMQ2ADD.W - Add two Q-format products (word, RV64 only)
 */
uint64_t HELPER(pmq2add_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t prod0 = (int64_t)s1_w0 * (int64_t)s2_w0;
    __int128_t prod0_95 = ((__int128_t)prod0) >> 31;
    int64_t prod1 = (int64_t)s1_w1 * (int64_t)s2_w1;
    __int128_t prod1_95 = ((__int128_t)prod1) >> 31;
    return (uint64_t)(prod0_95 + prod1_95);
}

/**
 * PMQR2ADD.W - Add two Q-format products with rounding (word, RV64 only)
 */
uint64_t HELPER(pmqr2add_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t prod0 = (int64_t)s1_w0 * (int64_t)s2_w0 + (1LL << 30);
    __int128_t prod0_95 = ((__int128_t)prod0) >> 31;
    int64_t prod1 = (int64_t)s1_w1 * (int64_t)s2_w1 + (1LL << 30);
    __int128_t prod1_95 = ((__int128_t)prod1) >> 31;
    return (uint64_t)(prod0_95 + prod1_95);
}

/**
 * PMQ2ADDA.W - Add two Q-format products with accumulate (word, RV64 only)
 */
uint64_t HELPER(pmq2adda_w)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod0 = (int64_t)s1_w0 * (int64_t)s2_w0;
    __int128_t prod0_95 = ((__int128_t)prod0) >> 31;
    int64_t prod1 = (int64_t)s1_w1 * (int64_t)s2_w1;
    __int128_t prod1_95 = ((__int128_t)prod1) >> 31;
    return (uint64_t)(d + prod0_95 + prod1_95);
}

/**
 * PMQR2ADDA.W - Add two Q-format products with rounding
 * and accumulate (word, RV64 only)
 */
uint64_t HELPER(pmqr2adda_w)(CPURISCVState *env, uint64_t rs1,
                             uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod0 = (int64_t)s1_w0 * (int64_t)s2_w0 + (1LL << 30);
    __int128_t prod0_95 = ((__int128_t)prod0) >> 31;
    int64_t prod1 = (int64_t)s1_w1 * (int64_t)s2_w1 + (1LL << 30);
    __int128_t prod1_95 = ((__int128_t)prod1) >> 31;
    return (uint64_t)(d + prod0_95 + prod1_95);
}

/**
 * PM2ADD.H - Add two products horizontally
 * For each word: rd[i] = rs1[2i] * rs2[2i] + rs1[2i+1] * rs2[2i+1]
 */
target_ulong HELPER(pm2add_h)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t prod0 = (int32_t)s1_h0 * (int32_t)s2_h0;
        int32_t prod1 = (int32_t)s1_h1 * (int32_t)s2_h1;
        uint32_t sum = (uint32_t)(prod0 + prod1);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM2ADDSU.H - Add two products horizontally (signed x unsigned)
 */
target_ulong HELPER(pm2addsu_h)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        uint16_t s2_h0 = EXTRACT16(rs2, i * 2);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        int32_t prod0 = (int32_t)s1_h0 * (uint32_t)s2_h0;
        int32_t prod1 = (int32_t)s1_h1 * (uint32_t)s2_h1;
        uint32_t sum = (uint32_t)(prod0 + prod1);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM2ADDU.H - Add two products horizontally (unsigned)
 */
target_ulong HELPER(pm2addu_h)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h0 = EXTRACT16(rs1, i * 2);
        uint16_t s1_h1 = EXTRACT16(rs1, i * 2 + 1);
        uint16_t s2_h0 = EXTRACT16(rs2, i * 2);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        uint32_t prod0 = (uint32_t)s1_h0 * (uint32_t)s2_h0;
        uint32_t prod1 = (uint32_t)s1_h1 * (uint32_t)s2_h1;
        uint32_t sum = prod0 + prod1;
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM2ADD.HX - Add cross products horizontally
 * For each word: rd[i] = rs1[2i] * rs2[2i+1] + rs1[2i+1] * rs2[2i]
 */
target_ulong HELPER(pm2add_hx)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t prod01 = (int32_t)s1_h0 * (int32_t)s2_h1;
        int32_t prod10 = (int32_t)s1_h1 * (int32_t)s2_h0;
        uint32_t sum = (uint32_t)(prod01 + prod10);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM2SUB.H - Subtract two products horizontally
 * For each word: rd[i] = rs1[2i] * rs2[2i] - rs1[2i+1] * rs2[2i+1]
 */
target_ulong HELPER(pm2sub_h)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t prod0 = (int32_t)s1_h0 * (int32_t)s2_h0;
        int32_t prod1 = (int32_t)s1_h1 * (int32_t)s2_h1;
        uint32_t diff = (uint32_t)(prod0 - prod1);
        rd = INSERT32(rd, diff, i);
    }
    return rd;
}

/**
 * PM2SUB.HX - Subtract cross products horizontally
 * For each word: rd[i] = rs1[2i+1] * rs2[2i] - rs1[2i] * rs2[2i+1]
 */
target_ulong HELPER(pm2sub_hx)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t prod10 = (int32_t)s1_h1 * (int32_t)s2_h0;
        int32_t prod01 = (int32_t)s1_h0 * (int32_t)s2_h1;
        uint32_t diff = (uint32_t)(prod10 - prod01);
        rd = INSERT32(rd, diff, i);
    }
    return rd;
}

/**
 * PM2ADDA.H - Add two products horizontally with accumulate
 */
target_ulong HELPER(pm2adda_h)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod0 = (int32_t)s1_h0 * (int32_t)s2_h0;
        int32_t prod1 = (int32_t)s1_h1 * (int32_t)s2_h1;
        uint32_t sum = (uint32_t)(d + prod0 + prod1);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM2ADDASU.H - Add two products horizontally with accumulate
 * (signed x unsigned)
 */
target_ulong HELPER(pm2addasu_h)(CPURISCVState *env, target_ulong rs1,
                                 target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        uint16_t s2_h0 = EXTRACT16(rs2, i * 2);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod0 = (int32_t)s1_h0 * (uint32_t)s2_h0;
        int32_t prod1 = (int32_t)s1_h1 * (uint32_t)s2_h1;
        uint32_t sum = (uint32_t)(d + prod0 + prod1);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM2ADDAU.H - Add two products horizontally with accumulate (unsigned)
 */
target_ulong HELPER(pm2addau_h)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        uint16_t s1_h0 = EXTRACT16(rs1, i * 2);
        uint16_t s1_h1 = EXTRACT16(rs1, i * 2 + 1);
        uint16_t s2_h0 = EXTRACT16(rs2, i * 2);
        uint16_t s2_h1 = EXTRACT16(rs2, i * 2 + 1);
        uint32_t d = EXTRACT32(dest, i);
        uint32_t prod0 = (uint32_t)s1_h0 * (uint32_t)s2_h0;
        uint32_t prod1 = (uint32_t)s1_h1 * (uint32_t)s2_h1;
        uint32_t sum = d + prod0 + prod1;
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM2ADDA.HX - Add cross products horizontally with accumulate
 */
target_ulong HELPER(pm2adda_hx)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod01 = (int32_t)s1_h0 * (int32_t)s2_h1;
        int32_t prod10 = (int32_t)s1_h1 * (int32_t)s2_h0;
        uint32_t sum = (uint32_t)(d + prod01 + prod10);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM2SUBA.H - Subtract two products horizontally with accumulate
 */
target_ulong HELPER(pm2suba_h)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod0 = (int32_t)s1_h0 * (int32_t)s2_h0;
        int32_t prod1 = (int32_t)s1_h1 * (int32_t)s2_h1;
        uint32_t diff = (uint32_t)(d + prod0 - prod1);
        rd = INSERT32(rd, diff, i);
    }
    return rd;
}

/**
 * PM2SUBA.HX - Subtract cross products horizontally with accumulate
 */
target_ulong HELPER(pm2suba_hx)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int16_t s1_h0 = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s1_h1 = (int16_t)EXTRACT16(rs1, i * 2 + 1);
        int16_t s2_h0 = (int16_t)EXTRACT16(rs2, i * 2);
        int16_t s2_h1 = (int16_t)EXTRACT16(rs2, i * 2 + 1);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod01 = (int32_t)s1_h0 * (int32_t)s2_h1;
        int32_t prod10 = (int32_t)s1_h1 * (int32_t)s2_h0;
        uint32_t diff = (uint32_t)(d + prod01 - prod10);
        rd = INSERT32(rd, diff, i);
    }
    return rd;
}

/**
 * PM2ADD.W - Add two products horizontally (word, RV64 only)
 */
uint64_t HELPER(pm2add_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t prod0 = (int64_t)s1_w0 * (int64_t)s2_w0;
    int64_t prod1 = (int64_t)s1_w1 * (int64_t)s2_w1;
    return (uint64_t)(prod0 + prod1);
}

/**
 * PM2ADDSU.W - Add two products horizontally (signed x unsigned, RV64 only)
 */
uint64_t HELPER(pm2addsu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    uint32_t s2_w0 = EXTRACT32(rs2, 0);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    int64_t prod0 = (int64_t)s1_w0 * (uint64_t)s2_w0;
    int64_t prod1 = (int64_t)s1_w1 * (uint64_t)s2_w1;
    return (uint64_t)(prod0 + prod1);
}

/**
 * PM2ADDU.W - Add two products horizontally (unsigned, RV64 only)
 */
uint64_t HELPER(pm2addu_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint32_t s1_w0 = EXTRACT32(rs1, 0);
    uint32_t s1_w1 = EXTRACT32(rs1, 1);
    uint32_t s2_w0 = EXTRACT32(rs2, 0);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    uint64_t prod0 = (uint64_t)s1_w0 * (uint64_t)s2_w0;
    uint64_t prod1 = (uint64_t)s1_w1 * (uint64_t)s2_w1;
    return prod0 + prod1;
}

/**
 * PM2ADD.WX - Add cross products horizontally (word, RV64 only)
 */
uint64_t HELPER(pm2add_wx)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t prod01 = (int64_t)s1_w0 * (int64_t)s2_w1;
    int64_t prod10 = (int64_t)s1_w1 * (int64_t)s2_w0;
    return (uint64_t)(prod01 + prod10);
}

/**
 * PM2SUB.W - Subtract two products horizontally (word, RV64 only)
 */
uint64_t HELPER(pm2sub_w)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t prod0 = (int64_t)s1_w0 * (int64_t)s2_w0;
    int64_t prod1 = (int64_t)s1_w1 * (int64_t)s2_w1;
    return (uint64_t)(prod0 - prod1);
}

/**
 * PM2SUB.WX - Subtract cross products horizontally (word, RV64 only)
 */
uint64_t HELPER(pm2sub_wx)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t prod10 = (int64_t)s1_w1 * (int64_t)s2_w0;
    int64_t prod01 = (int64_t)s1_w0 * (int64_t)s2_w1;
    return (uint64_t)(prod10 - prod01);
}

/**
 * PM2ADDA.W - Add two products horizontally with accumulate (word, RV64 only)
 */
uint64_t HELPER(pm2adda_w)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod0 = (int64_t)s1_w0 * (int64_t)s2_w0;
    int64_t prod1 = (int64_t)s1_w1 * (int64_t)s2_w1;
    return (uint64_t)(d + prod0 + prod1);
}

/**
 * PM2ADDASU.W - Add two products horizontally with accumulate
 * (signed x unsigned, RV64 only)
 */
uint64_t HELPER(pm2addasu_w)(CPURISCVState *env, uint64_t rs1,
                             uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    uint32_t s2_w0 = EXTRACT32(rs2, 0);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod0 = (int64_t)s1_w0 * (uint64_t)s2_w0;
    int64_t prod1 = (int64_t)s1_w1 * (uint64_t)s2_w1;
    return (uint64_t)(d + prod0 + prod1);
}

/**
 * PM2ADDAU.W - Add two products horizontally with accumulate
 * (unsigned, RV64 only)
 */
uint64_t HELPER(pm2addau_w)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    uint32_t s1_w0 = EXTRACT32(rs1, 0);
    uint32_t s1_w1 = EXTRACT32(rs1, 1);
    uint32_t s2_w0 = EXTRACT32(rs2, 0);
    uint32_t s2_w1 = EXTRACT32(rs2, 1);
    uint64_t d = dest;
    uint64_t prod0 = (uint64_t)s1_w0 * (uint64_t)s2_w0;
    uint64_t prod1 = (uint64_t)s1_w1 * (uint64_t)s2_w1;
    return d + prod0 + prod1;
}

/**
 * PM2ADDA.WX - Add cross products horizontally with accumulate
 * (word, RV64 only)
 */
uint64_t HELPER(pm2adda_wx)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod01 = (int64_t)s1_w0 * (int64_t)s2_w1;
    int64_t prod10 = (int64_t)s1_w1 * (int64_t)s2_w0;
    return (uint64_t)(d + prod01 + prod10);
}

/**
 * PM2SUBA.W - Subtract two products horizontally with accumulate
 * (word, RV64 only)
 */
uint64_t HELPER(pm2suba_w)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod0 = (int64_t)s1_w0 * (int64_t)s2_w0;
    int64_t prod1 = (int64_t)s1_w1 * (int64_t)s2_w1;
    return (uint64_t)(d + prod0 - prod1);
}

/**
 * PM2SUBA.WX - Subtract cross products horizontally with accumulate
 * (word, RV64 only)
 */
uint64_t HELPER(pm2suba_wx)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    int32_t s1_w0 = (int32_t)EXTRACT32(rs1, 0);
    int32_t s1_w1 = (int32_t)EXTRACT32(rs1, 1);
    int32_t s2_w0 = (int32_t)EXTRACT32(rs2, 0);
    int32_t s2_w1 = (int32_t)EXTRACT32(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod01 = (int64_t)s1_w0 * (int64_t)s2_w1;
    int64_t prod10 = (int64_t)s1_w1 * (int64_t)s2_w0;
    return (uint64_t)(d + prod01 - prod10);
}


/* Four-Way Multiply and Accumulate Operations */

/**
 * PM4ADD.B - Add four products horizontally (byte to word)
 */
target_ulong HELPER(pm4add_b)(CPURISCVState *env, target_ulong rs1,
                              target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int8_t s1_b0 = (int8_t)EXTRACT8(rs1, i * 4);
        int8_t s1_b1 = (int8_t)EXTRACT8(rs1, i * 4 + 1);
        int8_t s1_b2 = (int8_t)EXTRACT8(rs1, i * 4 + 2);
        int8_t s1_b3 = (int8_t)EXTRACT8(rs1, i * 4 + 3);
        int8_t s2_b0 = (int8_t)EXTRACT8(rs2, i * 4);
        int8_t s2_b1 = (int8_t)EXTRACT8(rs2, i * 4 + 1);
        int8_t s2_b2 = (int8_t)EXTRACT8(rs2, i * 4 + 2);
        int8_t s2_b3 = (int8_t)EXTRACT8(rs2, i * 4 + 3);
        int32_t prod0 = (int32_t)s1_b0 * (int32_t)s2_b0;
        int32_t prod1 = (int32_t)s1_b1 * (int32_t)s2_b1;
        int32_t prod2 = (int32_t)s1_b2 * (int32_t)s2_b2;
        int32_t prod3 = (int32_t)s1_b3 * (int32_t)s2_b3;
        uint32_t sum = (uint32_t)(prod0 + prod1 + prod2 + prod3);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM4ADDSU.B - Add four products horizontally (signed x unsigned)
 */
target_ulong HELPER(pm4addsu_b)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int8_t s1_b0 = (int8_t)EXTRACT8(rs1, i * 4);
        int8_t s1_b1 = (int8_t)EXTRACT8(rs1, i * 4 + 1);
        int8_t s1_b2 = (int8_t)EXTRACT8(rs1, i * 4 + 2);
        int8_t s1_b3 = (int8_t)EXTRACT8(rs1, i * 4 + 3);
        uint8_t s2_b0 = EXTRACT8(rs2, i * 4);
        uint8_t s2_b1 = EXTRACT8(rs2, i * 4 + 1);
        uint8_t s2_b2 = EXTRACT8(rs2, i * 4 + 2);
        uint8_t s2_b3 = EXTRACT8(rs2, i * 4 + 3);
        int32_t prod0 = (int32_t)s1_b0 * (uint32_t)s2_b0;
        int32_t prod1 = (int32_t)s1_b1 * (uint32_t)s2_b1;
        int32_t prod2 = (int32_t)s1_b2 * (uint32_t)s2_b2;
        int32_t prod3 = (int32_t)s1_b3 * (uint32_t)s2_b3;
        uint32_t sum = (uint32_t)(prod0 + prod1 + prod2 + prod3);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM4ADDU.B - Add four products horizontally (unsigned)
 */
target_ulong HELPER(pm4addu_b)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t s1_b0 = EXTRACT8(rs1, i * 4);
        uint8_t s1_b1 = EXTRACT8(rs1, i * 4 + 1);
        uint8_t s1_b2 = EXTRACT8(rs1, i * 4 + 2);
        uint8_t s1_b3 = EXTRACT8(rs1, i * 4 + 3);
        uint8_t s2_b0 = EXTRACT8(rs2, i * 4);
        uint8_t s2_b1 = EXTRACT8(rs2, i * 4 + 1);
        uint8_t s2_b2 = EXTRACT8(rs2, i * 4 + 2);
        uint8_t s2_b3 = EXTRACT8(rs2, i * 4 + 3);
        uint32_t prod0 = (uint32_t)s1_b0 * (uint32_t)s2_b0;
        uint32_t prod1 = (uint32_t)s1_b1 * (uint32_t)s2_b1;
        uint32_t prod2 = (uint32_t)s1_b2 * (uint32_t)s2_b2;
        uint32_t prod3 = (uint32_t)s1_b3 * (uint32_t)s2_b3;
        uint32_t sum = prod0 + prod1 + prod2 + prod3;
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM4ADDA.B - Add four products horizontally with accumulate
 */
target_ulong HELPER(pm4adda_b)(CPURISCVState *env, target_ulong rs1,
                               target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int8_t s1_b0 = (int8_t)EXTRACT8(rs1, i * 4);
        int8_t s1_b1 = (int8_t)EXTRACT8(rs1, i * 4 + 1);
        int8_t s1_b2 = (int8_t)EXTRACT8(rs1, i * 4 + 2);
        int8_t s1_b3 = (int8_t)EXTRACT8(rs1, i * 4 + 3);
        int8_t s2_b0 = (int8_t)EXTRACT8(rs2, i * 4);
        int8_t s2_b1 = (int8_t)EXTRACT8(rs2, i * 4 + 1);
        int8_t s2_b2 = (int8_t)EXTRACT8(rs2, i * 4 + 2);
        int8_t s2_b3 = (int8_t)EXTRACT8(rs2, i * 4 + 3);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod0 = (int32_t)s1_b0 * (int32_t)s2_b0;
        int32_t prod1 = (int32_t)s1_b1 * (int32_t)s2_b1;
        int32_t prod2 = (int32_t)s1_b2 * (int32_t)s2_b2;
        int32_t prod3 = (int32_t)s1_b3 * (int32_t)s2_b3;
        uint32_t sum = (uint32_t)(d + prod0 + prod1 + prod2 + prod3);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM4ADDASU.B - Add four products horizontally with accumulate
 * (signed x unsigned)
 */
target_ulong HELPER(pm4addasu_b)(CPURISCVState *env, target_ulong rs1,
                                 target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        int8_t s1_b0 = (int8_t)EXTRACT8(rs1, i * 4);
        int8_t s1_b1 = (int8_t)EXTRACT8(rs1, i * 4 + 1);
        int8_t s1_b2 = (int8_t)EXTRACT8(rs1, i * 4 + 2);
        int8_t s1_b3 = (int8_t)EXTRACT8(rs1, i * 4 + 3);
        uint8_t s2_b0 = EXTRACT8(rs2, i * 4);
        uint8_t s2_b1 = EXTRACT8(rs2, i * 4 + 1);
        uint8_t s2_b2 = EXTRACT8(rs2, i * 4 + 2);
        uint8_t s2_b3 = EXTRACT8(rs2, i * 4 + 3);
        int32_t d = (int32_t)EXTRACT32(dest, i);
        int32_t prod0 = (int32_t)s1_b0 * (uint32_t)s2_b0;
        int32_t prod1 = (int32_t)s1_b1 * (uint32_t)s2_b1;
        int32_t prod2 = (int32_t)s1_b2 * (uint32_t)s2_b2;
        int32_t prod3 = (int32_t)s1_b3 * (uint32_t)s2_b3;
        uint32_t sum = (uint32_t)(d + prod0 + prod1 + prod2 + prod3);
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM4ADDAU.B - Add four products horizontally with accumulate (unsigned)
 */
target_ulong HELPER(pm4addau_b)(CPURISCVState *env, target_ulong rs1,
                                target_ulong rs2, target_ulong dest)
{
    target_ulong rd = 0;
    int elems = ELEMS_W(rd);

    for (int i = 0; i < elems; i++) {
        uint8_t s1_b0 = EXTRACT8(rs1, i * 4);
        uint8_t s1_b1 = EXTRACT8(rs1, i * 4 + 1);
        uint8_t s1_b2 = EXTRACT8(rs1, i * 4 + 2);
        uint8_t s1_b3 = EXTRACT8(rs1, i * 4 + 3);
        uint8_t s2_b0 = EXTRACT8(rs2, i * 4);
        uint8_t s2_b1 = EXTRACT8(rs2, i * 4 + 1);
        uint8_t s2_b2 = EXTRACT8(rs2, i * 4 + 2);
        uint8_t s2_b3 = EXTRACT8(rs2, i * 4 + 3);
        uint32_t d = EXTRACT32(dest, i);
        uint32_t prod0 = (uint32_t)s1_b0 * (uint32_t)s2_b0;
        uint32_t prod1 = (uint32_t)s1_b1 * (uint32_t)s2_b1;
        uint32_t prod2 = (uint32_t)s1_b2 * (uint32_t)s2_b2;
        uint32_t prod3 = (uint32_t)s1_b3 * (uint32_t)s2_b3;
        uint32_t sum = d + prod0 + prod1 + prod2 + prod3;
        rd = INSERT32(rd, sum, i);
    }
    return rd;
}

/**
 * PM4ADD.H - Add four products horizontally (halfword to doubleword, RV64 only)
 */
uint64_t HELPER(pm4add_h)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s1_h2 = (int16_t)EXTRACT16(rs1, 2);
    int16_t s1_h3 = (int16_t)EXTRACT16(rs1, 3);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int16_t s2_h2 = (int16_t)EXTRACT16(rs2, 2);
    int16_t s2_h3 = (int16_t)EXTRACT16(rs2, 3);
    int64_t prod0 = (int64_t)s1_h0 * (int64_t)s2_h0;
    int64_t prod1 = (int64_t)s1_h1 * (int64_t)s2_h1;
    int64_t prod2 = (int64_t)s1_h2 * (int64_t)s2_h2;
    int64_t prod3 = (int64_t)s1_h3 * (int64_t)s2_h3;
    rd = (uint64_t)(prod0 + prod1 + prod2 + prod3);
    return rd;
}

/**
 * PM4ADDSU.H - Add four products horizontally (signed x unsigned, RV64 only)
 */
uint64_t HELPER(pm4addsu_h)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s1_h2 = (int16_t)EXTRACT16(rs1, 2);
    int16_t s1_h3 = (int16_t)EXTRACT16(rs1, 3);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint16_t s2_h2 = EXTRACT16(rs2, 2);
    uint16_t s2_h3 = EXTRACT16(rs2, 3);
    int64_t prod0 = (int64_t)s1_h0 * (uint64_t)s2_h0;
    int64_t prod1 = (int64_t)s1_h1 * (uint64_t)s2_h1;
    int64_t prod2 = (int64_t)s1_h2 * (uint64_t)s2_h2;
    int64_t prod3 = (int64_t)s1_h3 * (uint64_t)s2_h3;
    rd = (uint64_t)(prod0 + prod1 + prod2 + prod3);
    return rd;
}

/**
 * PM4ADDU.H - Add four products horizontally (unsigned, RV64 only)
 */
uint64_t HELPER(pm4addu_h)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    uint64_t rd = 0;
    uint16_t s1_h0 = EXTRACT16(rs1, 0);
    uint16_t s1_h1 = EXTRACT16(rs1, 1);
    uint16_t s1_h2 = EXTRACT16(rs1, 2);
    uint16_t s1_h3 = EXTRACT16(rs1, 3);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint16_t s2_h2 = EXTRACT16(rs2, 2);
    uint16_t s2_h3 = EXTRACT16(rs2, 3);
    uint64_t prod0 = (uint64_t)s1_h0 * (uint64_t)s2_h0;
    uint64_t prod1 = (uint64_t)s1_h1 * (uint64_t)s2_h1;
    uint64_t prod2 = (uint64_t)s1_h2 * (uint64_t)s2_h2;
    uint64_t prod3 = (uint64_t)s1_h3 * (uint64_t)s2_h3;
    rd = prod0 + prod1 + prod2 + prod3;
    return rd;
}

/**
 * PM4ADDA.H - Add four products horizontally with accumulate (RV64 only)
 */
uint64_t HELPER(pm4adda_h)(CPURISCVState *env, uint64_t rs1,
                           uint64_t rs2, uint64_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s1_h2 = (int16_t)EXTRACT16(rs1, 2);
    int16_t s1_h3 = (int16_t)EXTRACT16(rs1, 3);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int16_t s2_h2 = (int16_t)EXTRACT16(rs2, 2);
    int16_t s2_h3 = (int16_t)EXTRACT16(rs2, 3);
    int64_t d = (int64_t)dest;
    int64_t prod0 = (int64_t)s1_h0 * (int64_t)s2_h0;
    int64_t prod1 = (int64_t)s1_h1 * (int64_t)s2_h1;
    int64_t prod2 = (int64_t)s1_h2 * (int64_t)s2_h2;
    int64_t prod3 = (int64_t)s1_h3 * (int64_t)s2_h3;
    return (uint64_t)(d + prod0 + prod1 + prod2 + prod3);
}

/**
 * PM4ADDASU.H - Add four products horizontally with accumulate
 * (signed x unsigned, RV64 only)
 */
uint64_t HELPER(pm4addasu_h)(CPURISCVState *env, uint64_t rs1,
                             uint64_t rs2, uint64_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s1_h2 = (int16_t)EXTRACT16(rs1, 2);
    int16_t s1_h3 = (int16_t)EXTRACT16(rs1, 3);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint16_t s2_h2 = EXTRACT16(rs2, 2);
    uint16_t s2_h3 = EXTRACT16(rs2, 3);
    int64_t d = (int64_t)dest;
    int64_t prod0 = (int64_t)s1_h0 * (uint64_t)s2_h0;
    int64_t prod1 = (int64_t)s1_h1 * (uint64_t)s2_h1;
    int64_t prod2 = (int64_t)s1_h2 * (uint64_t)s2_h2;
    int64_t prod3 = (int64_t)s1_h3 * (uint64_t)s2_h3;
    return (uint64_t)(d + prod0 + prod1 + prod2 + prod3);
}

/**
 * PM4ADDAU.H - Add four products horizontally with accumulate
 * (unsigned, RV64 only)
 */
uint64_t HELPER(pm4addau_h)(CPURISCVState *env, uint64_t rs1,
                            uint64_t rs2, uint64_t dest)
{
    uint16_t s1_h0 = EXTRACT16(rs1, 0);
    uint16_t s1_h1 = EXTRACT16(rs1, 1);
    uint16_t s1_h2 = EXTRACT16(rs1, 2);
    uint16_t s1_h3 = EXTRACT16(rs1, 3);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint16_t s2_h2 = EXTRACT16(rs2, 2);
    uint16_t s2_h3 = EXTRACT16(rs2, 3);
    uint64_t d = dest;
    uint64_t prod0 = (uint64_t)s1_h0 * (uint64_t)s2_h0;
    uint64_t prod1 = (uint64_t)s1_h1 * (uint64_t)s2_h1;
    uint64_t prod2 = (uint64_t)s1_h2 * (uint64_t)s2_h2;
    uint64_t prod3 = (uint64_t)s1_h3 * (uint64_t)s2_h3;
    return d + prod0 + prod1 + prod2 + prod3;
}

/* Double-Width Operations (RV32 only, register pairs) */

/**
 * PWADD.B - Packed widening byte to halfword addition (RV32)
 * rd_pair = {rs1[31:24]+rs2[31:24], rs1[23:16]+rs2[23:16],
 *            rs1[15:8]+rs2[15:8], rs1[7:0]+rs2[7:0]} (sign-extended)
 */
uint64_t HELPER(pwadd_b)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        int16_t e1 = (int8_t)((rs1 >> (i * 8)) & 0xFF);
        int16_t e2 = (int8_t)((rs2 >> (i * 8)) & 0xFF);
        int16_t res = e1 + e2;
        rd |= ((uint64_t)(uint16_t)res) << (i * 16);
    }

    return rd;
}

/**
 * PWADDA.B - Packed widening byte to halfword addition with accumulate (RV32)
 * rd_pair += {rs1[i] + rs2[i]}
 */
uint64_t HELPER(pwadda_b)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint64_t rd)
{
    uint64_t result = 0;

    for (int i = 0; i < 4; i++) {
        int16_t e1 = (int8_t)((rs1 >> (i * 8)) & 0xFF);
        int16_t e2 = (int8_t)((rs2 >> (i * 8)) & 0xFF);
        int16_t acc = (int16_t)((rd >> (i * 16)) & 0xFFFF);
        int16_t res = acc + e1 + e2;
        result |= ((uint64_t)(uint16_t)res) << (i * 16);
    }

    return result;
}

/**
 * PWADDU.B - Packed widening byte to halfword unsigned addition (RV32)
 */
uint64_t HELPER(pwaddu_b)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t e1 = (uint8_t)((rs1 >> (i * 8)) & 0xFF);
        uint16_t e2 = (uint8_t)((rs2 >> (i * 8)) & 0xFF);
        uint16_t res = e1 + e2;
        rd |= ((uint64_t)res) << (i * 16);
    }

    return rd;
}

/**
 * PWADDAU.B - Packed widening byte to halfword unsigned addition
 * with accumulate (RV32)
 */
uint64_t HELPER(pwaddau_b)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint64_t rd)
{
    uint64_t result = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t e1 = (uint8_t)((rs1 >> (i * 8)) & 0xFF);
        uint16_t e2 = (uint8_t)((rs2 >> (i * 8)) & 0xFF);
        uint16_t acc = (uint16_t)((rd >> (i * 16)) & 0xFFFF);
        uint16_t res = acc + e1 + e2;
        result |= ((uint64_t)res) << (i * 16);
    }

    return result;
}

/**
 * PWSUB.B - Packed widening byte to halfword subtraction (RV32)
 */
uint64_t HELPER(pwsub_b)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        int16_t e1 = (int8_t)((rs1 >> (i * 8)) & 0xFF);
        int16_t e2 = (int8_t)((rs2 >> (i * 8)) & 0xFF);
        int16_t res = e1 - e2;
        rd |= ((uint64_t)(uint16_t)res) << (i * 16);
    }

    return rd;
}

/**
 * PWSUBA.B - Packed widening byte to halfword subtraction
 * with accumulate (RV32)
 */
uint64_t HELPER(pwsuba_b)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint64_t rd)
{
    uint64_t result = 0;

    for (int i = 0; i < 4; i++) {
        int16_t e1 = (int8_t)((rs1 >> (i * 8)) & 0xFF);
        int16_t e2 = (int8_t)((rs2 >> (i * 8)) & 0xFF);
        int16_t acc = (int16_t)((rd >> (i * 16)) & 0xFFFF);
        int16_t res = acc + (e1 - e2);
        result |= ((uint64_t)(uint16_t)res) << (i * 16);
    }

    return result;
}

/**
 * PWSUBU.B - Packed widening byte to halfword unsigned subtraction (RV32)
 */
uint64_t HELPER(pwsubu_b)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t e1 = (uint8_t)((rs1 >> (i * 8)) & 0xFF);
        uint16_t e2 = (uint8_t)((rs2 >> (i * 8)) & 0xFF);
        uint16_t res = e1 - e2;
        rd |= ((uint64_t)res) << (i * 16);
    }

    return rd;
}

/**
 * PWSUBAU.B - Packed widening byte to halfword unsigned subtraction
 * with accumulate (RV32)
 */
uint64_t HELPER(pwsubau_b)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint64_t rd)
{
    uint64_t result = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t e1 = (uint8_t)((rs1 >> (i * 8)) & 0xFF);
        uint16_t e2 = (uint8_t)((rs2 >> (i * 8)) & 0xFF);
        uint16_t acc = (uint16_t)((rd >> (i * 16)) & 0xFFFF);
        uint16_t res = acc + (e1 - e2);
        result |= ((uint64_t)res) << (i * 16);
    }

    return result;
}

/**
 * PWSLLI.B - Packed widening shift left immediate (byte to halfword)
 */
uint64_t HELPER(pwslli_b)(CPURISCVState *env, uint32_t rs1, uint32_t imm)
{
    uint64_t rd = 0;
    uint8_t shamt = imm & 0x0F;

    for (int i = 0; i < 4; i++) {
        uint16_t e1 = (uint8_t)((rs1 >> (i * 8)) & 0xFF);
        uint16_t res = e1 << shamt;
        rd |= ((uint64_t)res) << (i * 16);
    }

    return rd;
}

/**
 * PWSLL.BS - Packed widening shift left from register (byte to halfword)
 */
uint64_t HELPER(pwsll_bs)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;
    uint8_t shamt = rs2 & 0x1F;

    for (int i = 0; i < 4; i++) {
        uint16_t e1 = (uint8_t)((rs1 >> (i * 8)) & 0xFF);
        uint16_t res = e1 << shamt;
        rd |= ((uint64_t)res) << (i * 16);
    }

    return rd;
}

/**
 * PWSLAI.B - Packed widening signed shift left immediate (byte to halfword)
 */
uint64_t HELPER(pwslai_b)(CPURISCVState *env, uint32_t rs1, uint32_t imm)
{
    uint64_t rd = 0;
    uint8_t shamt = imm & 0x0F;

    for (int i = 0; i < 4; i++) {
        int16_t e1 = (int8_t)((rs1 >> (i * 8)) & 0xFF);
        int16_t res = e1 << shamt;
        rd |= ((uint64_t)(uint16_t)res) << (i * 16);
    }

    return rd;
}

/**
 * PWSLA.BS - Packed widening signed shift left from register (byte to halfword)
 */
uint64_t HELPER(pwsla_bs)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;
    uint8_t shamt = rs2 & 0x1F;

    for (int i = 0; i < 4; i++) {
        int16_t e1 = (int8_t)((rs1 >> (i * 8)) & 0xFF);
        int16_t res = e1 << shamt;
        rd |= ((uint64_t)(uint16_t)res) << (i * 16);
    }

    return rd;
}

/**
 * PWADD.H - Packed widening halfword to word addition (RV32)
 */
uint64_t HELPER(pwadd_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int16_t)((rs1 >> (i * 16)) & 0xFFFF);
        int32_t e2 = (int16_t)((rs2 >> (i * 16)) & 0xFFFF);
        int32_t res = e1 + e2;
        rd |= ((uint64_t)(uint32_t)res) << (i * 32);
    }

    return rd;
}

/**
 * PWADDA.H - Packed widening halfword to word addition with accumulate (RV32)
 */
uint64_t HELPER(pwadda_h)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint64_t rd)
{
    uint64_t result = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int16_t)((rs1 >> (i * 16)) & 0xFFFF);
        int32_t e2 = (int16_t)((rs2 >> (i * 16)) & 0xFFFF);
        int32_t acc = (int32_t)((rd >> (i * 32)) & 0xFFFFFFFF);
        int32_t res = acc + e1 + e2;
        result |= ((uint64_t)(uint32_t)res) << (i * 32);
    }

    return result;
}

/**
 * PWADDU.H - Packed widening halfword to word unsigned addition (RV32)
 */
uint64_t HELPER(pwaddu_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint32_t e1 = (uint16_t)((rs1 >> (i * 16)) & 0xFFFF);
        uint32_t e2 = (uint16_t)((rs2 >> (i * 16)) & 0xFFFF);
        uint32_t res = e1 + e2;
        rd |= ((uint64_t)res) << (i * 32);
    }

    return rd;
}

/**
 * PWADDAU.H - Packed widening halfword to word unsigned addition
 * with accumulate (RV32)
 */
uint64_t HELPER(pwaddau_h)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint64_t rd)
{
    uint64_t result = 0;

    for (int i = 0; i < 2; i++) {
        uint32_t e1 = (uint16_t)((rs1 >> (i * 16)) & 0xFFFF);
        uint32_t e2 = (uint16_t)((rs2 >> (i * 16)) & 0xFFFF);
        uint32_t acc = (uint32_t)((rd >> (i * 32)) & 0xFFFFFFFF);
        uint32_t res = acc + e1 + e2;
        result |= ((uint64_t)res) << (i * 32);
    }

    return result;
}

/**
 * PWSUB.H - Packed widening halfword to word subtraction (RV32)
 */
uint64_t HELPER(pwsub_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int16_t)((rs1 >> (i * 16)) & 0xFFFF);
        int32_t e2 = (int16_t)((rs2 >> (i * 16)) & 0xFFFF);
        int32_t res = e1 - e2;
        rd |= ((uint64_t)(uint32_t)res) << (i * 32);
    }

    return rd;
}

/**
 * PWSUBA.H - Packed widening halfword to word subtraction
 * with accumulate (RV32)
 */
uint64_t HELPER(pwsuba_h)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint64_t rd)
{
    uint64_t result = 0;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int16_t)((rs1 >> (i * 16)) & 0xFFFF);
        int32_t e2 = (int16_t)((rs2 >> (i * 16)) & 0xFFFF);
        int32_t acc = (int32_t)((rd >> (i * 32)) & 0xFFFFFFFF);
        int32_t res = acc + (e1 - e2);
        result |= ((uint64_t)(uint32_t)res) << (i * 32);
    }

    return result;
}

/**
 * PWSUBU.H - Packed widening halfword to word unsigned subtraction (RV32)
 */
uint64_t HELPER(pwsubu_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint32_t e1 = (uint16_t)((rs1 >> (i * 16)) & 0xFFFF);
        uint32_t e2 = (uint16_t)((rs2 >> (i * 16)) & 0xFFFF);
        uint32_t res = e1 - e2;
        rd |= ((uint64_t)res) << (i * 32);
    }

    return rd;
}

/**
 * PWSUBAU.H - Packed widening halfword to word unsigned subtraction
 * with accumulate (RV32)
 */
uint64_t HELPER(pwsubau_h)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint64_t rd)
{
    uint64_t result = 0;

    for (int i = 0; i < 2; i++) {
        uint32_t e1 = (uint16_t)((rs1 >> (i * 16)) & 0xFFFF);
        uint32_t e2 = (uint16_t)((rs2 >> (i * 16)) & 0xFFFF);
        uint32_t acc = (uint32_t)((rd >> (i * 32)) & 0xFFFFFFFF);
        uint32_t res = acc + (e1 - e2);
        result |= ((uint64_t)res) << (i * 32);
    }

    return result;
}

/**
 * PWSLLI.H - Packed widening shift left immediate (halfword to word)
 */
uint64_t HELPER(pwslli_h)(CPURISCVState *env, uint32_t rs1, uint32_t imm)
{
    uint64_t rd = 0;
    uint8_t shamt = imm & 0x1F;

    for (int i = 0; i < 2; i++) {
        uint32_t e1 = (uint16_t)((rs1 >> (i * 16)) & 0xFFFF);
        uint32_t res = e1 << shamt;
        rd |= ((uint64_t)res) << (i * 32);
    }

    return rd;
}

/**
 * PWSLL.HS - Packed widening shift left from register (halfword to word)
 */
uint64_t HELPER(pwsll_hs)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;
    uint8_t shamt = rs2 & 0x1F;

    for (int i = 0; i < 2; i++) {
        uint32_t e1 = (uint16_t)((rs1 >> (i * 16)) & 0xFFFF);
        uint32_t res = e1 << shamt;
        rd |= ((uint64_t)res) << (i * 32);
    }

    return rd;
}

/**
 * PWSLAI.H - Packed widening signed shift left immediate (halfword to word)
 */
uint64_t HELPER(pwslai_h)(CPURISCVState *env, uint32_t rs1, uint32_t imm)
{
    uint64_t rd = 0;
    uint8_t shamt = imm & 0x1F;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int16_t)((rs1 >> (i * 16)) & 0xFFFF);
        int32_t res = e1 << shamt;
        rd |= ((uint64_t)(uint32_t)res) << (i * 32);
    }

    return rd;
}

/**
 * PWSLA.HS - Packed widening signed shift left from register (halfword to word)
 */
uint64_t HELPER(pwsla_hs)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;
    uint8_t shamt = rs2 & 0x1F;

    for (int i = 0; i < 2; i++) {
        int32_t e1 = (int16_t)((rs1 >> (i * 16)) & 0xFFFF);
        int32_t res = e1 << shamt;
        rd |= ((uint64_t)(uint32_t)res) << (i * 32);
    }

    return rd;
}

/**
 * WADD - Widening signed addition (RV32)
 */
uint64_t HELPER(wadd)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int64_t a = (int32_t)rs1;
    int64_t b = (int32_t)rs2;
    return (uint64_t)(a + b);
}

/**
 * WADDA - Widening signed addition with accumulate (RV32)
 */
uint64_t HELPER(wadda)(CPURISCVState *env, uint32_t rs1,
                       uint32_t rs2, uint64_t rd)
{
    int64_t a = (int32_t)rs1;
    int64_t b = (int32_t)rs2;
    int64_t acc = (int64_t)rd;
    return (uint64_t)(acc + a + b);
}

/**
 * WADDU - Widening unsigned addition (RV32)
 */
uint64_t HELPER(waddu)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t a = rs1;
    uint64_t b = rs2;
    return a + b;
}

/**
 * WADDAU - Widening unsigned addition with accumulate (RV32)
 */
uint64_t HELPER(waddau)(CPURISCVState *env, uint32_t rs1,
                        uint32_t rs2, uint64_t rd)
{
    uint64_t acc = rd;
    return acc + rs1 + rs2;
}

/**
 * WSUB - Widening signed subtraction (RV32)
 */
uint64_t HELPER(wsub)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int64_t a = (int32_t)rs1;
    int64_t b = (int32_t)rs2;
    return (uint64_t)(a - b);
}

/**
 * WSUBA - Widening signed subtraction with accumulate (RV32)
 */
uint64_t HELPER(wsuba)(CPURISCVState *env, uint32_t rs1,
                       uint32_t rs2, uint64_t rd)
{
    int64_t a = (int32_t)rs1;
    int64_t b = (int32_t)rs2;
    int64_t acc = (int64_t)rd;
    return (uint64_t)(acc + a - b);
}

/**
 * WSUBU - Widening unsigned subtraction (RV32)
 */
uint64_t HELPER(wsubu)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t a = rs1;
    uint64_t b = rs2;
    return a - b;
}

/**
 * WSUBAU - Widening unsigned subtraction with accumulate (RV32)
 */
uint64_t HELPER(wsubau)(CPURISCVState *env, uint32_t rs1,
                        uint32_t rs2, uint64_t rd)
{
    uint64_t acc = rd;
    return acc + rs1 - rs2;
}

/**
 * WSLLI - Widening logical shift left immediate (RV32)
 */
uint64_t HELPER(wslli)(CPURISCVState *env, uint32_t rs1, uint32_t imm)
{
    uint64_t a = rs1;
    uint8_t shamt = imm & 0x3F;
    return a << shamt;
}

/**
 * WSLL - Widening logical shift left from register (RV32)
 */
uint64_t HELPER(wsll)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t a = rs1;
    uint8_t shamt = rs2 & 0x3F;
    return a << shamt;
}

/**
 * WSLAI - Widening signed shift left immediate (RV32)
 */
uint64_t HELPER(wslai)(CPURISCVState *env, uint32_t rs1, uint32_t imm)
{
    int64_t a = (int32_t)rs1;
    uint8_t shamt = imm & 0x3F;
    return (uint64_t)(a << shamt);
}

/**
 * WSLA - Widening signed shift left from register (RV32)
 */
uint64_t HELPER(wsla)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int64_t a = (int32_t)rs1;
    uint8_t shamt = rs2 & 0x3F;
    return (uint64_t)(a << shamt);
}

/**
 * WZIP8P - Double-width interleave bytes (RV32)
 */
uint64_t HELPER(wzip8p)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint64_t b1 = (uint64_t)EXTRACT8(rs1, i) << 16 * i;
        uint64_t b2 = (uint64_t)EXTRACT8(rs2, i) << (16 * i + 8);
        rd = rd | b2 | b1;
    }

    return rd;
}

/**
 * WZIP16P - Double-width interleave halfwords (RV32)
 */
uint64_t HELPER(wzip16p)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint64_t h1 = (uint64_t)EXTRACT16(rs1, i) << (32 * i);
        uint64_t h2 = (uint64_t)EXTRACT16(rs2, i) << (32 * i + 16);
        rd = rd | h2 | h1;
    }

    return rd;
}

/**
 * PREDSUM.DBS - Double-width signed reduction sum of bytes (RV32)
 */
uint32_t HELPER(predsum_dbs)(CPURISCVState *env, uint32_t rs1_lo,
                             uint32_t rs1_hi, uint32_t rs2)
{
    int64_t sum = (int32_t)rs2;
    int64_t s1 = ((int64_t)rs1_hi << 32) | rs1_lo;

    for (int i = 0; i < 8; i++) {
        int8_t b = (int8_t)((s1 >> (i * 8)) & 0xFF);
        sum += b;
    }

    return (uint32_t)sum;
}

/**
 * PREDSUMU.DBS - Double-width unsigned reduction sum of bytes (RV32)
 */
uint32_t HELPER(predsumu_dbs)(CPURISCVState *env, uint32_t rs1_lo,
                              uint32_t rs1_hi, uint32_t rs2)
{
    uint64_t sum = rs2;
    uint64_t s1 = ((uint64_t)rs1_hi << 32) | rs1_lo;

    for (int i = 0; i < 8; i++) {
        uint8_t b = (uint8_t)((s1 >> (i * 8)) & 0xFF);
        sum += b;
    }

    return (uint32_t)sum;
}

/**
 * PREDSUM.DHS - Double-width signed reduction sum of halfwords (RV32)
 */
uint32_t HELPER(predsum_dhs)(CPURISCVState *env, uint32_t rs1_lo,
                             uint32_t rs1_hi, uint32_t rs2)
{
    int64_t sum = (int32_t)rs2;
    int64_t s1 = ((int64_t)rs1_hi << 32) | rs1_lo;

    for (int i = 0; i < 4; i++) {
        int16_t h = (int16_t)((s1 >> (i * 16)) & 0xFFFF);
        sum += h;
    }

    return (uint32_t)sum;
}

/**
 * PREDSUMU.DHS - Double-width unsigned reduction sum of halfwords (RV32)
 */
uint32_t HELPER(predsumu_dhs)(CPURISCVState *env, uint32_t rs1_lo,
                              uint32_t rs1_hi, uint32_t rs2)
{
    uint64_t sum = rs2;
    uint64_t s1 = ((uint64_t)rs1_hi << 32) | rs1_lo;

    for (int i = 0; i < 4; i++) {
        uint16_t h = (uint16_t)((s1 >> (i * 16)) & 0xFFFF);
        sum += h;
    }

    return (uint32_t)sum;
}


/* Narrowing Operations (RV32 only, register pair sources) */

/**
 * PNSRLI.B - Narrowing logical shift right immediate (64-bit to 32-bit)
 */
uint32_t HELPER(pnsrli_b)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        uint8_t result = (s1_h >> (shamt & 0xF)) & 0xFF;
        rd |= ((uint32_t)result) << (i * 8);
    }

    return rd;
}

/**
 * PNSRL.BS - Narrowing logical shift right from register (64-bit to 32-bit)
 */
uint32_t HELPER(pnsrl_bs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        uint32_t s1_h_z32 = (uint32_t)s1_h;
        uint8_t result = (s1_h_z32 >> (shamt & 0x1F)) & 0xFF;
        rd |= ((uint32_t)result) << (i * 8);
    }

    return rd;
}

/**
 * PNSRAI.B - Narrowing arithmetic shift right immediate (64-bit to 32-bit)
 */
uint32_t HELPER(pnsrai_b)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        int32_t s1_h_s32 = (int32_t)(int16_t)s1_h;
        int32_t s1_h_s24 = (s1_h_s32 << 8) >> 8;
        uint8_t result = s1_h_s24 >> (shamt & 0xF) & 0xFF;
        rd |= ((uint32_t)result) << (i * 8);
    }

    return rd;
}

/**
 * PNSRA.BS - Narrowing arithmetic shift right from register (64-bit to 32-bit)
 */
uint32_t HELPER(pnsra_bs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        int64_t s1_h_s64 = (int64_t)(int16_t)s1_h;
        s1_h_s64 = (s1_h_s64 << 24) >> 24;
        uint8_t result = s1_h_s64 >> (shamt & 0x1F) & 0xFF;
        rd |= ((uint32_t)result) << (i * 8);
    }

    return rd;
}

/**
 * PNSRARI.B - Narrowing arithmetic shift right with rounding
 * immediate (64-bit to 32-bit)
 */
uint32_t HELPER(pnsrari_b)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        int32_t s1_h_s32 = (int32_t)(int16_t)s1_h;
        int32_t s1_h_s24 = (s1_h_s32 << 8) >> 8;
        uint32_t shx_25bit = ((uint32_t)s1_h_s24 << 1);
        uint32_t shx = (shx_25bit >> (shamt & 0xF)) & 0x1FF;
        uint8_t result = ((shx + 1) >> 1) & 0xFF;
        rd |= ((uint32_t)result) << (i * 8);
    }

    return rd;
}

/**
 * PNSRAR.BS - Narrowing arithmetic shift right with rounding
 * from register (64-bit to 32-bit)
 */
uint32_t HELPER(pnsrar_bs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        int64_t s1_h_s64 = (int64_t)(int16_t)s1_h;
        int64_t s1_h_s40 = (s1_h_s64 << 24) >> 24;
        uint64_t shx_41bit = ((uint64_t)s1_h_s40 << 1);
        uint64_t shx = (shx_41bit >> (shamt & 0x1F)) & 0x1FF;
        uint8_t result = ((shx + 1) >> 1) & 0xFF;
        rd |= ((uint32_t)result) << (i * 8);
    }

    return rd;
}

/**
 * PNCLIPI.B - Narrowing clip signed (64-bit to 32-bit) with immediate shift
 */
uint32_t HELPER(pnclipi_b)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        int32_t s1_h_s32 = (int32_t)(int16_t)s1_h;
        int16_t shx = (int16_t)(s1_h_s32 >> (shamt & 0xF));
        uint8_t result = 0;

        if (shx < -128) {
            sat = 1;
            result = 0x80; /* -128 */
        } else if (shx > 127) {
            sat = 1;
            result = 0x7F; /* 127 */
        } else {
            result = (uint8_t)shx;
        }
        rd |= ((uint32_t)result << (i * 8));
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPRI.B - Narrowing clip signed with rounding
 * (64-bit to 32-bit) with immediate shift
 */
uint32_t HELPER(pnclipri_b)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        int32_t s1_h_s32 = (int32_t)(int16_t)s1_h;
        uint64_t shx_33bit = ((uint32_t)s1_h_s32 << 1);
        uint32_t shx = (shx_33bit >> (shamt & 0xF)) & 0x1FFFF;
        uint16_t round_shx = (uint16_t)((shx + 1) >> 1);
        int16_t round_shx_s = (int16_t)round_shx;
        uint8_t result = 0;

        if (round_shx_s < -128) {
            sat = 1;
            result = 0x80;
        } else if (round_shx_s > 127) {
            sat = 1;
            result = 0x7F;
        } else {
            result = (uint8_t)round_shx;
        }

        rd |= ((uint32_t)result) << (i * 8);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPIU.B - Narrowing clip unsigned (64-bit to 32-bit) with immediate shift
 */
uint32_t HELPER(pnclipiu_b)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        uint16_t shx = s1_h >> (shamt & 0xF);
        uint8_t result = 0;

        if (shx > 0x00FF) {
            sat = 1;
            result = 0xFF;
        } else {
            result = (uint8_t)(shx & 0xFF);
        }
        rd |= ((uint32_t)result) << (i * 8);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPRIU.B - Narrowing clip unsigned with rounding
 * (64-bit to 32-bit) with immediate shift
 */
uint32_t HELPER(pnclipriu_b)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        uint32_t shx_17bit = ((uint32_t)s1_h << 1);
        uint32_t shx = shx_17bit >> (shamt & 0xF);
        uint16_t round_shx = (uint16_t)((shx + 1) >> 1);
        uint8_t result = 0;

        if (round_shx > 0x00FF) {
            sat = 1;
            result = 0xFF;
        } else {
            result = (uint8_t)(round_shx & 0xFF);
        }
        rd |= ((uint32_t)result) << (i * 8);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIP.BS - Narrowing clip signed from register (64-bit to 32-bit)
 */
uint32_t HELPER(pnclip_bs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        int64_t s1_h_s64 = (int64_t)(int16_t)s1_h;
        int64_t s1_h_s48 = (s1_h_s64 << 16) >> 16;
        int16_t shx = (int16_t)(s1_h_s48 >> (shamt & 0x1F));
        uint8_t result = 0;

        if (shx < -128) {
            sat = 1;
            result = 0x80;
        } else if (shx > 127) {
            sat = 1;
            result = 0x7F;
        } else {
            result = (uint8_t)shx;
        }
        rd |= ((uint32_t)result) << (i * 8);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPR.BS - Narrowing clip signed with rounding
 * from register (64-bit to 32-bit)
 */
uint32_t HELPER(pnclipr_bs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        int64_t s1_h_s64 = (int64_t)(int16_t)s1_h;
        int64_t s1_h_s48 = (s1_h_s64 << 16) >> 16;
        uint64_t shx_49bit = ((uint64_t)s1_h_s48 << 1);
        uint32_t shx = (shx_49bit >> (shamt & 0x1F)) & 0x1FFFF;
        uint16_t round_shx = (uint16_t)((shx + 1) >> 1);
        int16_t round_shx_s = (int16_t)round_shx;
        uint8_t result = 0;

        if (round_shx_s < -128) {
            sat = 1;
            result = 0x80;
        } else if (round_shx_s > 127) {
            sat = 1;
            result = 0x7F;
        } else {
            result = (uint8_t)round_shx;
        }
        rd |= ((uint32_t)result) << (i * 8);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPU.BS - Narrowing clip unsigned from register (64-bit to 32-bit)
 */
uint32_t HELPER(pnclipu_bs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        uint32_t s1_h_z32 = (uint32_t)s1_h;
        uint16_t shx = (s1_h_z32 >> (shamt & 0x1F)) & 0xFFFF;
        uint8_t result = 0;

        if (shx > 0x00FF) {
            sat = 1;
            result = 0xFF;
        } else {
            result = (uint8_t)(shx & 0xFF);
        }
        rd |= ((uint32_t)result) << (i * 8);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPRU.BS - Narrowing clip unsigned with rounding
 * from register (64-bit to 32-bit)
 */
uint32_t HELPER(pnclipru_bs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t s1_h = (s1 >> (i * 16)) & 0xFFFF;
        uint32_t s1_h_z32 = (uint32_t)s1_h;
        uint64_t shx_33bit = ((uint64_t)s1_h_z32 << 1);
        uint32_t shx = (shx_33bit >> (shamt & 0x1F)) & 0x1FFFF;
        uint16_t round_shx = (uint16_t)((shx + 1) >> 1);
        uint8_t result = 0;

        if (round_shx > 0x00FF) {
            sat = 1;
            result = 0xFF;
        } else {
            result = (uint8_t)(round_shx & 0xFF);
        }
        rd |= ((uint32_t)result) << (i * 8);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNSRLI.H - Narrowing logical shift right immediate
 * (64-bit to 32-bit, word to halfword)
 */
uint32_t HELPER(pnsrli_h)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    uint32_t s1_low  = (uint32_t)(s1 & 0xFFFFFFFF);
    uint32_t s1_high = (uint32_t)((s1 >> 32) & 0xFFFFFFFF);

    uint16_t rd_low  = (s1_low  >> (shamt & 0x1F)) & 0xFFFF;
    uint16_t rd_high = (s1_high >> (shamt & 0x1F)) & 0xFFFF;

    rd = ((uint32_t)rd_high << 16) | rd_low;
    return rd;
}

/**
 * PNSRAI.H - Narrowing arithmetic shift right immediate
 * (64-bit to 32-bit, word to halfword)
 */
uint32_t HELPER(pnsrai_h)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    uint32_t s1_low  = (uint32_t)(s1 & 0xFFFFFFFF);
    int64_t s1_low_s64 = (int64_t)(int32_t)s1_low;
    int64_t s1_low_s48 = (s1_low_s64 << 16) >> 16;

    uint32_t s1_high = (uint32_t)((s1 >> 32) & 0xFFFFFFFF);
    int64_t s1_high_s64 = (int64_t)(int32_t)s1_high;
    int64_t s1_high_s48 = (s1_high_s64 << 16) >> 16;

    uint16_t rd_low  = (s1_low_s48  >> (shamt & 0x1F)) & 0xFFFF;
    uint16_t rd_high = (s1_high_s48 >> (shamt & 0x1F)) & 0xFFFF;

    rd = ((uint32_t)rd_high << 16) | rd_low;
    return rd;
}

/**
 * PNSRARI.H - Narrowing arithmetic shift right with rounding
 * immediate (64-bit to 32-bit, word to halfword)
 */
uint32_t HELPER(pnsrari_h)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint32_t s1_w = (s1 >> (i * 32)) & 0xFFFFFFFF;
        int64_t s1_w_s64 = (int64_t)(int32_t)s1_w;
        int64_t s1_w_s48 = (s1_w_s64 << 16) >> 16;
        uint64_t shx_49bit = ((uint64_t)s1_w_s48 << 1);
        uint32_t shx = (shx_49bit >> (shamt & 0x1F)) & 0x1FFFF;
        rd |= ((uint16_t)((shx + 1) >> 1)) << (i * 16);
    }

    return rd;
}

/**
 * PNSRL.HS - Narrowing logical shift right from register
 * (64-bit to 32-bit, word to halfword)
 */
uint32_t HELPER(pnsrl_hs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    uint32_t s1_low  = (uint32_t)(s1 & 0xFFFFFFFF);
    uint32_t s1_high = (uint32_t)((s1 >> 32) & 0xFFFFFFFF);

    uint16_t rd_low  = (s1_low  >> (shamt & 0x1F)) & 0xFFFF;
    uint16_t rd_high = (s1_high >> (shamt & 0x1F)) & 0xFFFF;

    rd = ((uint32_t)rd_high << 16) | rd_low;
    return rd;
}

/**
 * PNSRA.HS - Narrowing arithmetic shift right from register
 * (64-bit to 32-bit, word to halfword)
 */
uint32_t HELPER(pnsra_hs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    uint32_t s1_low  = (uint32_t)(s1 & 0xFFFFFFFF);
    uint32_t s1_high = (uint32_t)((s1 >> 32) & 0xFFFFFFFF);

    uint16_t rd_low  = (s1_low  >> (shamt & 0x1F)) & 0xFFFF;
    uint16_t rd_high = (s1_high >> (shamt & 0x1F)) & 0xFFFF;

    rd = ((uint32_t)rd_high << 16) | rd_low;
    return rd;
}

/**
 * PNSRAR.HS - Narrowing arithmetic shift right with rounding
 * from register (64-bit to 32-bit, word to halfword)
 */
uint32_t HELPER(pnsrar_hs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint32_t s1_w = (s1 >> (i * 32)) & 0xFFFFFFFF;
        int64_t s1_w_s64 = (int64_t)(int32_t)s1_w;
        int64_t s1_w_s48 = (s1_w_s64 << 16) >> 16;
        uint64_t shx_49bit = ((uint64_t)s1_w_s48 << 1);
        uint32_t shx = (shx_49bit >> (shamt & 0x1F)) & 0x1FFFF;
        rd |= ((uint16_t)((shx + 1) >> 1)) << (i * 16);
    }

    return rd;
}

/**
 * PNCLIP.HS - Narrowing signed clip from register shift (word to halfword)
 * For each word: arithmetic right shift, clip to signed 16-bit
 *   shx = (int32_t)rs1[i] >> shamt
 *   result = sat16(shx)
 */
uint32_t HELPER(pnclip_hs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;
    uint8_t shift = shamt & 0x1F;

    for (int i = 0; i < 2; i++) {
        uint32_t s1_w = EXTRACT32(s1, i);
        int64_t s1_w_s64 = (int64_t)(int32_t)s1_w;
        int32_t shx = (int32_t)(s1_w_s64 >> shift);
        uint16_t result;

        if (shx < -32768) {
            sat = 1;
            result = 0x8000;
        } else if (shx > 32767) {
            sat = 1;
            result = 0x7FFF;
        } else {
            result = (uint16_t)shx;
        }

        rd = INSERT16(rd, result, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPR.HS - Narrowing signed clip with rounding
 * from register (word to halfword)
 * For each word: ((int32_t)rs1[i] << 1) >> shamt, round, clip to signed 16-bit
 *   shx_65bit = ((int64_t)rs1[i] << 1)
 *   shx = (shx_65bit >> shamt) & mask
 *   round = (shx + 1) >> 1
 *   result = sat16(round)
 */
uint32_t HELPER(pnclipr_hs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;
    uint8_t shift = shamt & 0x1F;

    for (int i = 0; i < 2; i++) {
        uint32_t s1_w = EXTRACT32(s1, i);
        int64_t s1_w_s64 = (int64_t)(int32_t)s1_w;
        __uint128_t shx_65bit = (__uint128_t)s1_w_s64 << 1;
        uint64_t shx = (uint64_t)(shx_65bit >> shift) & 0x1FFFFFFFF;
        int32_t round_shx = (int32_t)((shx + 1) >> 1);
        uint16_t result;

        if (round_shx < -32768) {
            sat = 1;
            result = 0x8000;
        } else if (round_shx > 32767) {
            sat = 1;
            result = 0x7FFF;
        } else {
            result = (uint16_t)round_shx;
        }

        rd = INSERT16(rd, result, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPI.H - Narrowing signed clip from immediate shift (word to halfword)
 * For each word: rs1[i] >> imm, clip to signed 16-bit
 */
uint32_t HELPER(pnclipi_h)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    return HELPER(pnclip_hs)(env, s1, shamt);
}

/**
 * PNCLIPRI.H - Narrowing signed clip with rounding
 * from immediate shift (word to halfword)
 * For each word: (rs1[i] << 1) >> imm, round, clip to signed 16-bit
 */
uint32_t HELPER(pnclipri_h)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    return HELPER(pnclipr_hs)(env, s1, shamt);
}

/**
 * PNCLIPU.HS - Narrowing unsigned clip from register shift (word to halfword)
 * For each word: shift right, clip to unsigned 16-bit
 *   shx = rs1[i] >> shamt
 *   result = (shx > 65535) ? 0xFFFF : shx
 */
uint32_t HELPER(pnclipu_hs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;
    uint8_t shift = shamt & 0x1F;

    for (int i = 0; i < 2; i++) {
        uint32_t s1_w = EXTRACT32(s1, i);
        uint32_t shx = s1_w >> shift;
        uint16_t result;

        if (shx > 65535) {
            sat = 1;
            result = 0xFFFF;
        } else {
            result = (uint16_t)shx;
        }

        rd = INSERT16(rd, result, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPRU.HS - Narrowing unsigned clip with rounding
 * from register (word to halfword)
 * For each word: (rs1[i] << 1) >> shamt, round, clip to unsigned 16-bit
 *   shx = ((rs1[i] << 1) >> shamt)
 *   round = (shx + 1) >> 1
 *   result = (round > 65535) ? 0xFFFF : round
 */
uint32_t HELPER(pnclipru_hs)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint32_t rd = 0;
    int sat = 0;
    uint8_t shift = shamt & 0x1F;

    for (int i = 0; i < 2; i++) {
        uint32_t s1_w = EXTRACT32(s1, i);
        uint64_t shx_33bit = (uint64_t)s1_w << 1;
        uint64_t shx = shx_33bit >> shift;
        uint32_t round_shx = (uint32_t)((shx + 1) >> 1);
        uint16_t result;

        if (round_shx > 65535) {
            sat = 1;
            result = 0xFFFF;
        } else {
            result = (uint16_t)round_shx;
        }

        rd = INSERT16(rd, result, i);
    }

    if (sat) {
        env->vxsat = 1;
    }
    return rd;
}

/**
 * PNCLIPIU.H - Narrowing unsigned clip from immediate shift (word to halfword)
 * For each word: rs1[i] >> imm, clip to unsigned 16-bit
 */
uint32_t HELPER(pnclipiu_h)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    return HELPER(pnclipu_hs)(env, s1, shamt);
}

/**
 * PNCLIPRIU.H - Narrowing unsigned clip with rounding
 * from immediate shift (word to halfword)
 * For each word: (rs1[i] << 1) >> imm, round, clip to unsigned 16-bit
 */
uint32_t HELPER(pnclipriu_h)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    return HELPER(pnclipru_hs)(env, s1, shamt);
}

/**
 * NSRLI - Narrowing logical shift right immediate (64-bit to 32-bit)
 */
uint32_t HELPER(nsrli)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    return (s1 >> (shamt & 0x3F)) & 0xFFFFFFFF;
}

/**
 * NSRAI - Narrowing arithmetic shift right immediate (64-bit to 32-bit)
 */
uint32_t HELPER(nsrai)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);
    __int128_t s1_s96 = (s1_s128 << 32) >> 32;
    return (uint32_t)(s1_s96 >> (shamt & 0x3F)) & 0xFFFFFFFF;
}

/**
 * NSRARI - Narrowing arithmetic shift right with rounding
 * immediate (64-bit to 32-bit)
 */
uint32_t HELPER(nsrari)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);
    __int128_t s1_s96 = (s1_s128 << 32) >> 32;
    __uint128_t shx_97bit = ((__uint128_t)s1_s96 << 1);
    uint64_t shx = (uint64_t)(shx_97bit >> (shamt & 0x3F)) & 0x1FFFFFFFF;
    return (uint32_t)((shx + 1) >> 1);
}

/**
 * NSRL - Narrowing logical shift right from register (64-bit to 32-bit)
 */
uint32_t HELPER(nsrl)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    return (s1 >> (shamt & 0x3F)) & 0xFFFFFFFF;
}

/**
 * NSRA - Narrowing arithmetic shift right from register (64-bit to 32-bit)
 */
uint32_t HELPER(nsra)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);
    __int128_t s1_s96 = (s1_s128 << 32) >> 32;
    return (uint32_t)(s1_s96 >> (shamt & 0x3F)) & 0xFFFFFFFF;
}

/**
 * NSRAR - Narrowing arithmetic shift right with rounding
 * from register (64-bit to 32-bit)
 */
uint32_t HELPER(nsrar)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);
    __int128_t s1_s96 = (s1_s128 << 32) >> 32;
    __uint128_t shx_97bit = ((__uint128_t)s1_s96 << 1);
    uint64_t shx = (uint64_t)(shx_97bit >> (shamt & 0x3F)) & 0x1FFFFFFFF;
    return (uint32_t)((shx + 1) >> 1);
}

/**
 * NCLIPI - Narrowing clip signed with immediate shift (64-bit to 32-bit)
 */
uint32_t HELPER(nclipi)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);
    int64_t shx = (int64_t)(s1_s128 >> (shamt & 0x3F));

    if (shx < -2147483648LL) {
        env->vxsat = 1;
        return 0x80000000U;
    } else if (shx > 2147483647LL) {
        env->vxsat = 1;
        return 0x7FFFFFFFU;
    } else {
        return (uint32_t)(shx & 0xFFFFFFFF);
    }
}

/**
 * NCLIPRI - Narrowing clip signed with rounding and immediate
 * shift (64-bit to 32-bit)
 */
uint32_t HELPER(nclipri)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    typedef struct {
        __uint128_t low;
        uint8_t high;
    } Uint129;

    Uint129 left_shift_1(__int128_t s1_s128)
    {
        Uint129 result;
        __uint128_t us1 = (__uint128_t)s1_s128;
        result.low = us1 << 1;
        result.high = (us1 >> 127) & 0x1;
        return result;
    }

    Uint129 right_shift(Uint129 val, uint32_t smt)
    {
        Uint129 result;
        if (smt == 0) {
            return val;
        } else if (smt >= 129) {
            result.low = 0;
            result.high = 0;
        } else if (smt == 128) {
            result.low = val.high;
            result.high = 0;
        } else {
            result.low = (val.low >> smt) |
                         ((__uint128_t)val.high << (128 - smt));
            result.high = (val.high >> smt);
        }
        return result;
    }

    __int128_t s1_s128 = (__int128_t)((int64_t)s1);
    Uint129 shx_129bit = left_shift_1(s1_s128);
    Uint129 shx = right_shift(shx_129bit, shamt & 0x3F);
    int64_t round_shx = (int64_t)((shx.low + 1) >> 1);

    if (round_shx < -2147483648LL) {
        env->vxsat = 1;
        return 0x80000000U;
    } else if (round_shx > 2147483647LL) {
        env->vxsat = 1;
        return 0x7FFFFFFFU;
    } else {
        return (uint32_t)round_shx;
    }
}

/**
 * NCLIPIU - Narrowing clip unsigned with immediate shift (64-bit to 32-bit)
 */
uint32_t HELPER(nclipiu)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint64_t shx = s1 >> (shamt & 0x3F);

    if (shx > 4294967295ULL) {
        env->vxsat = 1;
        return 0xFFFFFFFFU;
    } else {
        return (uint32_t)(shx & 0xFFFFFFFF);
    }
}

/**
 * NCLIPRIU - Narrowing clip unsigned with rounding and immediate
 * shift (64-bit to 32-bit)
 */
uint32_t HELPER(nclipriu)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    __uint128_t shx_65bit = (s1 << 1);
    __uint128_t shx = shx_65bit >> (shamt & 0x3F);
    uint64_t round_shx = (shx + 1) >> 1;

    if (round_shx > 4294967295ULL) {
        env->vxsat = 1;
        return 0xFFFFFFFFU;
    } else {
        return (uint32_t)(round_shx & 0xFFFFFFFF);
    }
}

/**
 * NCLIP - Narrowing clip signed from register (64-bit to 32-bit)
 */
uint32_t HELPER(nclip)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);
    int64_t shx = (int64_t)(s1_s128 >> (shamt & 0x3F));

    if (shx < -2147483648LL) {
        env->vxsat = 1;
        return 0x80000000U;
    } else if (shx > 2147483647LL) {
        env->vxsat = 1;
        return 0x7FFFFFFFU;
    } else {
        return (uint32_t)(shx & 0xFFFFFFFF);
    }
}

/**
 * NCLIPR - Narrowing clip signed with rounding from register (64-bit to 32-bit)
 */
uint32_t HELPER(nclipr)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    typedef struct {
        __uint128_t low;
        uint8_t high;
    } Uint129;

    Uint129 left_shift_1(__int128_t s1_s128)
    {
        Uint129 result;
        __uint128_t us1 = (__uint128_t)s1_s128;
        result.low = us1 << 1;
        result.high = (us1 >> 127) & 0x1;
        return result;
    }

    Uint129 right_shift(Uint129 val, uint32_t smt)
    {
        Uint129 result;
        if (smt == 0) {
            return val;
        } else if (smt >= 129) {
            result.low = 0;
            result.high = 0;
        } else if (smt == 128) {
            result.low = val.high;
            result.high = 0;
        } else {
            result.low = (val.low >> smt) |
                         ((__uint128_t)val.high << (128 - smt));
            result.high = (val.high >> smt);
        }
        return result;
    }

    __int128_t s1_s128 = (__int128_t)((int64_t)s1);
    Uint129 shx_129bit = left_shift_1(s1_s128);
    Uint129 shx = right_shift(shx_129bit, shamt & 0x3F);
    int64_t round_shx = (int64_t)((shx.low + 1) >> 1);

    if (round_shx < -2147483648LL) {
        env->vxsat = 1;
        return 0x80000000U;
    } else if (round_shx > 2147483647LL) {
        env->vxsat = 1;
        return 0x7FFFFFFFU;
    } else {
        return (uint32_t)round_shx;
    }
}

/**
 * NCLIPU - Narrowing clip unsigned from register (64-bit to 32-bit)
 */
uint32_t HELPER(nclipu)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    uint64_t shx = s1 >> (shamt & 0x3F);

    if (shx > 4294967295ULL) {
        env->vxsat = 1;
        return 0xFFFFFFFFU;
    } else {
        return (uint32_t)(shx & 0xFFFFFFFF);
    }
}

/**
 * NCLIPRU - Narrowing clip unsigned with rounding
 * from register (64-bit to 32-bit)
 */
uint32_t HELPER(nclipru)(CPURISCVState *env, uint64_t s1, uint32_t shamt)
{
    __uint128_t shx_65bit = (s1 << 1);
    __uint128_t shx = shx_65bit >> (shamt & 0x3F);
    uint64_t round_shx = (shx + 1) >> 1;

    if (round_shx > 4294967295ULL) {
        env->vxsat = 1;
        return 0xFFFFFFFFU;
    } else {
        return (uint32_t)(round_shx & 0xFFFFFFFF);
    }
}

/* Multiplication with Even-Odd Register Pairs as Destination (RV32 only) */

/**
 * PMQWACC.H - Packed Q-format halfword to word multiply accumulate
 */
uint64_t HELPER(pmqwacc_h)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h = (int16_t)EXTRACT16(rs2, i * 2);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)s1_h * (int64_t)s2_h;
        uint32_t res = (uint32_t)(d_w + (int32_t)(prod >> 15));
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PMQRWACC.H - Packed Q-format halfword to word multiply
 * accumulate with rounding
 */
uint64_t HELPER(pmqrwacc_h)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h = (int16_t)EXTRACT16(rs1, i * 2);
        int16_t s2_h = (int16_t)EXTRACT16(rs2, i * 2);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int64_t prod = (int64_t)s1_h * (int64_t)s2_h + (1LL << 14);
        uint32_t res = (uint32_t)(d_w + (int32_t)(prod >> 15));
        rd = INSERT32(rd, res, i);
    }
    return rd;
}

/**
 * PWMUL.B - Widening byte to halfword multiplication
 */
uint64_t HELPER(pwmul_b)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        int8_t s1_b = (int8_t)EXTRACT8(rs1, i);
        int8_t s2_b = (int8_t)EXTRACT8(rs2, i);
        int16_t prod = (int16_t)s1_b * (int16_t)s2_b;
        rd |= ((uint64_t)(uint16_t)prod) << (i * 16);
    }
    return rd;
}

/**
 * PWMULSU.B - Widening signed x unsigned byte to halfword multiplication
 */
uint64_t HELPER(pwmulsu_b)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        int8_t s1_b = (int8_t)EXTRACT8(rs1, i);
        uint8_t s2_b = EXTRACT8(rs2, i);
        int16_t prod = (int16_t)s1_b * (uint16_t)s2_b;
        rd |= ((uint64_t)(uint16_t)prod) << (i * 16);
    }
    return rd;
}

/**
 * PWMULU.B - Widening unsigned byte to halfword multiplication
 */
uint64_t HELPER(pwmulu_b)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t s1_b = EXTRACT8(rs1, i);
        uint8_t s2_b = EXTRACT8(rs2, i);
        uint16_t prod = (uint16_t)s1_b * (uint16_t)s2_b;
        rd |= ((uint64_t)prod) << (i * 16);
    }
    return rd;
}

/**
 * PWMUL.H - Widening halfword to word multiplication
 */
uint64_t HELPER(pwmul_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h = (int16_t)EXTRACT16(rs1, i);
        int16_t s2_h = (int16_t)EXTRACT16(rs2, i);
        int32_t prod = (int32_t)s1_h * (int32_t)s2_h;
        rd |= ((uint64_t)(uint32_t)prod) << (i * 32);
    }
    return rd;
}

/**
 * PWMULSU.H - Widening signed x unsigned halfword to word multiplication
 */
uint64_t HELPER(pwmulsu_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h = (int16_t)EXTRACT16(rs1, i);
        uint16_t s2_h = EXTRACT16(rs2, i);
        int32_t prod = (int32_t)s1_h * (uint32_t)s2_h;
        rd |= ((uint64_t)(uint32_t)prod) << (i * 32);
    }
    return rd;
}

/**
 * PWMULU.H - Widening unsigned halfword to word multiplication
 */
uint64_t HELPER(pwmulu_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint16_t s1_h = EXTRACT16(rs1, i);
        uint16_t s2_h = EXTRACT16(rs2, i);
        uint32_t prod = (uint32_t)s1_h * (uint32_t)s2_h;
        rd |= ((uint64_t)prod) << (i * 32);
    }
    return rd;
}

/**
 * PWMACC.H - Widening multiply accumulate (halfword to word)
 */
uint64_t HELPER(pwmacc_h)(CPURISCVState *env, uint32_t rs1,
                          uint32_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h = (int16_t)EXTRACT16(rs1, i);
        int16_t s2_h = (int16_t)EXTRACT16(rs2, i);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int32_t prod = (int32_t)s1_h * (int32_t)s2_h;
        uint32_t res = (uint32_t)(d_w + prod);
        rd |= ((uint64_t)res) << (i * 32);
    }
    return rd;
}

/**
 * PWMACCSU.H - Widening signed x unsigned multiply
 * accumulate (halfword to word)
 */
uint64_t HELPER(pwmaccsu_h)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        int16_t s1_h = (int16_t)EXTRACT16(rs1, i);
        uint16_t s2_h = EXTRACT16(rs2, i);
        int32_t d_w = (int32_t)EXTRACT32(dest, i);
        int32_t prod = (int32_t)s1_h * (uint32_t)s2_h;
        uint32_t res = (uint32_t)(d_w + prod);
        rd |= ((uint64_t)res) << (i * 32);
    }
    return rd;
}

/**
 * PWMACCU.H - Widening unsigned multiply accumulate (halfword to word)
 */
uint64_t HELPER(pwmaccu_h)(CPURISCVState *env, uint32_t rs1,
                           uint32_t rs2, uint64_t dest)
{
    uint64_t rd = 0;

    for (int i = 0; i < 2; i++) {
        uint16_t s1_h = EXTRACT16(rs1, i);
        uint16_t s2_h = EXTRACT16(rs2, i);
        uint32_t d_w = EXTRACT32(dest, i);
        uint32_t prod = (uint32_t)s1_h * (uint32_t)s2_h;
        uint32_t res = d_w + prod;
        rd |= ((uint64_t)res) << (i * 32);
    }
    return rd;
}

/**
 * MQWACC - Q-format word multiply accumulate
 */
uint64_t HELPER(mqwacc)(CPURISCVState *env, uint32_t rs1,
                        uint32_t rs2, uint64_t dest)
{
    int64_t s1 = (int64_t)(int32_t)rs1;
    int64_t s2 = (int64_t)(int32_t)rs2;
    int64_t d = (int64_t)dest;
    __int128_t prod = (__int128_t)s1 * (__int128_t)s2;
    return (uint64_t)(d + (int64_t)(prod >> 31));
}

/**
 * MQRWACC - Q-format word multiply accumulate with rounding
 */
uint64_t HELPER(mqrwacc)(CPURISCVState *env, uint32_t rs1,
                         uint32_t rs2, uint64_t dest)
{
    int64_t s1 = (int64_t)(int32_t)rs1;
    int64_t s2 = (int64_t)(int32_t)rs2;
    int64_t d = (int64_t)dest;
    __int128_t prod = (__int128_t)s1 * (__int128_t)s2 + (1LL << 30);
    return (uint64_t)(d + (int64_t)(prod >> 31));
}

/**
 * WMUL - Widening signed multiplication (32-bit to 64-bit, RV32)
 */
uint64_t HELPER(wmul)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    return (uint64_t)((int64_t)(int32_t)rs1 * (int64_t)(int32_t)rs2);
}

/**
 * WMULSU - Widening signed x unsigned multiplication (32-bit to 64-bit, RV32)
 */
uint64_t HELPER(wmulsu)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    return (uint64_t)((int64_t)(int32_t)rs1 * (uint64_t)rs2);
}

/**
 * WMULU - Widening unsigned multiplication (32-bit to 64-bit, RV32)
 */
uint64_t HELPER(wmulu)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    return (uint64_t)rs1 * (uint64_t)rs2;
}

/**
 * WMACC - Widening multiply accumulate signed (32-bit to 64-bit, RV32)
 */
uint64_t HELPER(wmacc)(CPURISCVState *env, uint32_t rs1,
                       uint32_t rs2, uint64_t dest)
{
    return (uint64_t)((int64_t)(int32_t)rs1 *
                      (int64_t)(int32_t)rs2 + (int64_t)dest);
}

/**
 * WMACCSU - Widening multiply accumulate signed x unsigned
 * (32-bit to 64-bit, RV32)
 */
uint64_t HELPER(wmaccsu)(CPURISCVState *env, uint32_t rs1,
                         uint32_t rs2, uint64_t dest)
{
    return (uint64_t)((int64_t)(int32_t)rs1 * (uint64_t)rs2 + (int64_t)dest);
}

/**
 * WMACCU - Widening multiply accumulate unsigned (32-bit to 64-bit, RV32)
 */
uint64_t HELPER(wmaccu)(CPURISCVState *env, uint32_t rs1,
                        uint32_t rs2, uint64_t dest)
{
    return (uint64_t)rs1 * (uint64_t)rs2 + (uint64_t)dest;
}

/**
 * PM2WADD.H - Add two widening products (halfword to doubleword)
 */
uint64_t HELPER(pm2wadd_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int64_t prod0 = (int64_t)s1_h0 * (int64_t)s2_h0;
    int64_t prod1 = (int64_t)s1_h1 * (int64_t)s2_h1;
    return (uint64_t)(prod0 + prod1);
}

/**
 * PM2WADDSU.H - Add two widening products
 * (signed x unsigned, halfword to doubleword)
 */
uint64_t HELPER(pm2waddsu_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    int64_t prod0 = (int64_t)s1_h0 * (uint64_t)s2_h0;
    int64_t prod1 = (int64_t)s1_h1 * (uint64_t)s2_h1;
    return (uint64_t)(prod0 + prod1);
}

/**
 * PM2WADDU.H - Add two widening products (unsigned, halfword to doubleword)
 */
uint64_t HELPER(pm2waddu_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    uint16_t s1_h0 = EXTRACT16(rs1, 0);
    uint16_t s1_h1 = EXTRACT16(rs1, 1);
    uint16_t s2_h0 = EXTRACT16(rs2, 0);
    uint16_t s2_h1 = EXTRACT16(rs2, 1);
    uint64_t prod0 = (uint64_t)s1_h0 * (uint64_t)s2_h0;
    uint64_t prod1 = (uint64_t)s1_h1 * (uint64_t)s2_h1;
    return prod0 + prod1;
}

/**
 * PM2WADDA.H - Add two widening products with accumulate
 * (halfword to doubleword)
 */
uint64_t HELPER(pm2wadda_h)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint64_t dest)
{
    int16_t s1_h0 = EXTRACT16(rs1, 0);
    int16_t s1_h1 = EXTRACT16(rs1, 1);
    int16_t s2_h0 = EXTRACT16(rs2, 0);
    int16_t s2_h1 = EXTRACT16(rs2, 1);
    int64_t d_h = (int64_t)dest;
    int64_t mul_00 = (int64_t)s1_h0 * (int64_t)s2_h0;
    int64_t mul_11 = (int64_t)s1_h1 * (int64_t)s2_h1;
    return (uint64_t)(d_h + mul_00 + mul_11);
}

/**
 * PM2WADDASU.H - Add two widening products with accumulate
 * (signed x unsigned, halfword to doubleword)
 */
uint64_t HELPER(pm2waddasu_h)(CPURISCVState *env, uint32_t rs1,
                              uint32_t rs2, uint64_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    uint16_t s2_h0 = (uint16_t)EXTRACT16(rs2, 0);
    uint16_t s2_h1 = (uint16_t)EXTRACT16(rs2, 1);
    int64_t d_h = (int64_t)dest;
    int64_t mul_00 = (int64_t)s1_h0 * (uint64_t)s2_h0;
    int64_t mul_11 = (int64_t)s1_h1 * (uint64_t)s2_h1;
    return (uint64_t)(d_h + mul_00 + mul_11);
}

/**
 * PM2WADDAU.H - Add two widening products with accumulate
 * (unsigned, halfword to doubleword)
 */
uint64_t HELPER(pm2waddau_h)(CPURISCVState *env, uint32_t rs1,
                             uint32_t rs2, uint64_t dest)
{
    uint16_t s1_h0 = (uint16_t)EXTRACT16(rs1, 0);
    uint16_t s1_h1 = (uint16_t)EXTRACT16(rs1, 1);
    uint16_t s2_h0 = (uint16_t)EXTRACT16(rs2, 0);
    uint16_t s2_h1 = (uint16_t)EXTRACT16(rs2, 1);
    uint64_t d_h = (uint64_t)dest;
    uint64_t mul_00 = (uint64_t)s1_h0 * (uint64_t)s2_h0;
    uint64_t mul_11 = (uint64_t)s1_h1 * (uint64_t)s2_h1;
    return (uint64_t)(d_h + mul_00 + mul_11);
}

/**
 * PM2WADD.HX - Add two widening cross products (halfword to doubleword)
 */
uint64_t HELPER(pm2wadd_hx)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int64_t prod01 = (int64_t)s1_h0 * (int64_t)s2_h1;
    int64_t prod10 = (int64_t)s1_h1 * (int64_t)s2_h0;
    return (uint64_t)(prod01 + prod10);
}

/**
 * PM2WADDA.HX - Add two widening cross products with accumulate
 * (halfword to doubleword)
 */
uint64_t HELPER(pm2wadda_hx)(CPURISCVState *env, uint32_t rs1,
                             uint32_t rs2, uint64_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod01 = (int64_t)s1_h0 * (int64_t)s2_h1;
    int64_t prod10 = (int64_t)s1_h1 * (int64_t)s2_h0;
    return (uint64_t)(d + prod01 + prod10);
}

/**
 * PM2WSUB.H - Subtract two widening products (halfword to doubleword)
 */
uint64_t HELPER(pm2wsub_h)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int64_t prod0 = (int64_t)s1_h0 * (int64_t)s2_h0;
    int64_t prod1 = (int64_t)s1_h1 * (int64_t)s2_h1;
    return (uint64_t)(prod0 - prod1);
}

/**
 * PM2WSUB.HX - Subtract two widening cross products (halfword to doubleword)
 */
uint64_t HELPER(pm2wsub_hx)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int64_t prod01 = (int64_t)s1_h0 * (int64_t)s2_h1;
    int64_t prod10 = (int64_t)s1_h1 * (int64_t)s2_h0;
    return (uint64_t)(prod01 - prod10);
}

/**
 * PM2WSUBA.H - Subtract two widening products with accumulate
 * (halfword to doubleword)
 */
uint64_t HELPER(pm2wsuba_h)(CPURISCVState *env, uint32_t rs1,
                            uint32_t rs2, uint64_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod0 = (int64_t)s1_h0 * (int64_t)s2_h0;
    int64_t prod1 = (int64_t)s1_h1 * (int64_t)s2_h1;
    return (uint64_t)(d + prod0 - prod1);
}

/**
 * PM2WSUBA.HX - Subtract two widening cross products with accumulate
 * (halfword to doubleword)
 */
uint64_t HELPER(pm2wsuba_hx)(CPURISCVState *env, uint32_t rs1,
                             uint32_t rs2, uint64_t dest)
{
    int16_t s1_h0 = (int16_t)EXTRACT16(rs1, 0);
    int16_t s1_h1 = (int16_t)EXTRACT16(rs1, 1);
    int16_t s2_h0 = (int16_t)EXTRACT16(rs2, 0);
    int16_t s2_h1 = (int16_t)EXTRACT16(rs2, 1);
    int64_t d = (int64_t)dest;
    int64_t prod01 = (int64_t)s1_h0 * (int64_t)s2_h1;
    int64_t prod10 = (int64_t)s1_h1 * (int64_t)s2_h0;
    return (uint64_t)(d + prod01 - prod10);
}
