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
#define ELEMS_D(target) (sizeof(target) * 8 / 64)

/* Element extraction macros - unsigned to avoid sign extension */
#define EXTRACT8(val, idx)  (((val) >> ((idx) * 8)) & 0xFF)
#define EXTRACT16(val, idx) (((val) >> ((idx) * 16)) & 0xFFFF)
#define EXTRACT32(val, idx) (((val) >> ((idx) * 32)) & 0xFFFFFFFF)
#define EXTRACT64(val, idx) (((val) >> ((idx) * 64)) & 0xFFFFFFFFFFFFFFFFULL)

/* Element insertion macros */
#define INSERT8(val, res, idx) \
    ((val) | ((target_ulong)(uint8_t)(res) << ((idx) * 8)))
#define INSERT16(val, res, idx) \
    ((val) | ((target_ulong)(uint16_t)(res) << ((idx) * 16)))
#define INSERT32(val, res, idx) \
    ((val) | ((target_ulong)(uint32_t)(res) << ((idx) * 32)))
#define INSERT32_64(val, res, idx) \
    ((val) | ((uint64_t)(uint32_t)(res) << ((idx) * 32)))
#define INSERT64(val, res, idx) \
    ((val) | ((uint64_t)(res) << ((idx) * 64)))

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

static inline target_ulong psimd_abdsumu_b(target_ulong rs1,
                                           target_ulong rs2,
                                           target_ulong sum)
{
    int elems = ELEMS_B(rs1);

    for (int i = 0; i < elems; i++) {
        uint8_t e1 = EXTRACT8(rs1, i);
        uint8_t e2 = EXTRACT8(rs2, i);
        uint8_t diff = (e1 > e2) ? (e1 - e2) : (e2 - e1);
        sum += diff;
    }

    return sum;
}

#define PSIMD_DO_ADD(N, M) ((N) + (M))
#define PSIMD_DO_SUB(N, M) ((N) - (M))
#define PSIMD_DO_ABD(N, M) ((N) >= (M) ? (N) - (M) : (M) - (N))
#define PSIMD_DO_EQ_MASK(N, M) ((N) == (M) ? -1 : 0)
#define PSIMD_DO_LT_MASK(N, M) ((N) < (M) ? -1 : 0)
#define PSIMD_DO_MIN(N, M) ((N) < (M) ? (N) : (M))
#define PSIMD_DO_MAX(N, M) ((N) > (M) ? (N) : (M))
#define PSIMD_DO_SLL(N, M) ((N) << (M))
#define PSIMD_DO_SRL(N, M) ((N) >> (M))
#define PSIMD_DO_SRA(N, M) ((N) >> (M))

#define GEN_PSIMD_BINOP(NAME, RTYPE, STYPE, DTYPE, EXTRACT, INSERT,       \
                        ELEMS, OP)                                        \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        STYPE e1 = (STYPE)EXTRACT(rs1, i);                                \
        STYPE e2 = (STYPE)EXTRACT(rs2, i);                                \
        DTYPE res = (DTYPE)OP(e1, e2);                                    \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_BINOP_SCALAR(NAME, RTYPE, ETYPE, EXTRACT, INSERT,       \
                               ELEMS, OP)                                 \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    ETYPE e2 = (ETYPE)EXTRACT(rs2, 0);                                    \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE res = OP(e1, e2);                                           \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_SHIFTOP(NAME, RTYPE, STYPE, DTYPE, EXTRACT, INSERT,     \
                          ELEMS, SHMASK, OP)                              \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    uint8_t shamt = rs2 & (SHMASK);                                       \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        STYPE e1 = (STYPE)EXTRACT(rs1, i);                                \
        DTYPE res = (DTYPE)OP(e1, shamt);                                 \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_SAT_SHIFTOP(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT, \
                              ELEMS, SHMASK, SAT_FN)                      \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
    uint8_t shamt = rs2 & (SHMASK);                                       \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        WTYPE shifted = (WTYPE)e1 << shamt;                               \
        ETYPE res = SAT_FN(shifted, &sat);                                \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_ROUND_SRAI(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,  \
                             ELEMS, SHMASK)                               \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    uint8_t shamt = rs2 & (SHMASK);                                       \
                                                                          \
    if (shamt == 0) {                                                     \
        return rs1;                                                       \
    }                                                                     \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        WTYPE rounded = (((WTYPE)e1 >> (shamt - 1)) + 1) >> 1;            \
        rd = INSERT(rd, (ETYPE)rounded, i);                               \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_SAT_BINOP(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,   \
                            ELEMS, OP, SAT_FN)                            \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE e2 = (ETYPE)EXTRACT(rs2, i);                                \
        WTYPE val = OP((WTYPE)e1, (WTYPE)e2);                             \
        ETYPE res = SAT_FN(val, &sat);                                    \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_USUB_SAT(NAME, RTYPE, ETYPE, EXTRACT, INSERT, ELEMS)    \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE e2 = (ETYPE)EXTRACT(rs2, i);                                \
        ETYPE res = (e1 >= e2) ? (e1 - e2) : 0;                           \
        if (e1 < e2) {                                                    \
            sat = 1;                                                      \
        }                                                                 \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_AVG_BINOP(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,   \
                            ELEMS, OP)                                    \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        WTYPE e1 = (WTYPE)(ETYPE)EXTRACT(rs1, i);                         \
        WTYPE e2 = (WTYPE)(ETYPE)EXTRACT(rs2, i);                         \
        ETYPE res = (ETYPE)(OP(e1, e2) >> 1);                             \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_SHADD(NAME, RTYPE, ETYPE, EXTRACT, INSERT, ELEMS,       \
                        SHAMT)                                            \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE e2 = (ETYPE)EXTRACT(rs2, i);                                \
        ETYPE res = (e1 << (SHAMT)) + e2;                                 \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_SAT_SHADD(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,   \
                            ELEMS, SHAMT, SH_MIN, SH_MAX, SAT_MIN,        \
                            SAT_MAX, SAT_FN)                              \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE e2 = (ETYPE)EXTRACT(rs2, i);                                \
        WTYPE shifted;                                                    \
                                                                          \
        if (e1 > (SH_MAX) || e1 < (SH_MIN)) {                             \
            shifted = (e1 < 0) ? (SAT_MIN) : (SAT_MAX);                   \
            sat = 1;                                                      \
        } else {                                                          \
            shifted = (WTYPE)e1 << (SHAMT);                               \
        }                                                                 \
                                                                          \
        WTYPE sum = shifted + e2;                                         \
        ETYPE res = SAT_FN(sum, &sat);                                    \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_SATI(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,        \
                       ELEMS, IMMMASK)                                    \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE imm)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int range = (imm & (IMMMASK)) + 1;                                    \
    WTYPE max = ((WTYPE)1 << (range - 1)) - 1;                            \
    WTYPE min = -((WTYPE)1 << (range - 1));                               \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE res;                                                        \
                                                                          \
        if (e1 > max) {                                                   \
            res = max;                                                    \
            sat = 1;                                                      \
        } else if (e1 < min) {                                            \
            res = min;                                                    \
            sat = 1;                                                      \
        } else {                                                          \
            res = e1;                                                     \
        }                                                                 \
                                                                          \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_USATI(NAME, RTYPE, ETYPE, UTYPE, WTYPE, EXTRACT,        \
                        INSERT, ELEMS, ONE)                               \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE imm)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    WTYPE max = ((WTYPE)(ONE) << imm) - 1;                                \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE res;                                                        \
                                                                          \
        if (e1 < 0) {                                                     \
            res = 0;                                                      \
            sat = 1;                                                      \
        } else if ((UTYPE)e1 > max) {                                     \
            res = max;                                                    \
            sat = 1;                                                      \
        } else {                                                          \
            res = e1;                                                     \
        }                                                                 \
                                                                          \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_ABS(NAME, RTYPE, ETYPE, EXTRACT, INSERT, ELEMS,         \
                      MINVAL, MAXVAL)                                     \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1)                         \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE res;                                                        \
                                                                          \
        if (e1 == (MINVAL)) {                                             \
            res = (MAXVAL);                                               \
            sat = 1;                                                      \
        } else if (e1 < 0) {                                              \
            res = -e1;                                                    \
        } else {                                                          \
            res = e1;                                                     \
        }                                                                 \
                                                                          \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_SCALAR_ABS(NAME, RTYPE, STYPE, UTYPE)                   \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1)                         \
{                                                                         \
    STYPE value = (STYPE)rs1;                                            \
    UTYPE result = (UTYPE)value;                                         \
                                                                          \
    if (value < 0) {                                                      \
        result = (UTYPE)0 - result;                                      \
    }                                                                     \
    return (RTYPE)(STYPE)result;                                         \
}

#define GEN_PSIMD_VAR_SSHA(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,    \
                           ELEMS, BITS, SAT_FN)                           \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
    int8_t shamt = (int8_t)(rs2 & 0xff);                                  \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE res;                                                        \
                                                                          \
        if (shamt >= 0) {                                                 \
            WTYPE shifted = (WTYPE)e1 << shamt;                           \
            res = SAT_FN(shifted, &sat);                                  \
        } else {                                                          \
            int right = -shamt;                                           \
            if (right >= (BITS)) {                                        \
                res = (e1 < 0) ? -1 : 0;                                  \
            } else {                                                      \
                res = e1 >> right;                                        \
            }                                                             \
        }                                                                 \
                                                                          \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_VAR_SSHAR(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,   \
                            ELEMS, BITS, SAT_MIN, SAT_MAX, SAT_FN)        \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
    int8_t shamt = (int8_t)(rs2 & 0xff);                                  \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE res;                                                        \
                                                                          \
        if (shamt >= 0) {                                                 \
            if (shamt >= (BITS)) {                                        \
                if (e1 == 0) {                                            \
                    res = 0;                                              \
                } else if (e1 > 0) {                                      \
                    res = (SAT_MAX);                                      \
                    sat = 1;                                              \
                } else {                                                  \
                    res = (SAT_MIN);                                      \
                    sat = 1;                                              \
                }                                                         \
            } else {                                                      \
                WTYPE shifted = (WTYPE)e1 * ((WTYPE)1 << shamt);          \
                res = SAT_FN(shifted, &sat);                              \
            }                                                             \
        } else {                                                          \
            int right = -shamt;                                           \
            if (right >= (BITS)) {                                        \
                res = 0;                                                  \
            } else {                                                      \
                WTYPE rounded = ((e1 >> (right - 1)) + 1) >> 1;           \
                res = (ETYPE)rounded;                                     \
            }                                                             \
        }                                                                 \
                                                                          \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_VAR_USHL(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,    \
                           ELEMS, BITS, SAT_FN)                           \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
    int8_t shamt = (int8_t)(rs2 & 0xff);                                  \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE res;                                                        \
                                                                          \
        if (shamt >= 0) {                                                 \
            WTYPE shifted = (shamt >= (BITS)) ?                           \
                            ((WTYPE)e1 << (BITS)) :                       \
                            ((WTYPE)e1 << shamt);                         \
            res = SAT_FN(shifted, &sat);                                  \
        } else {                                                          \
            int right = -shamt;                                           \
            if (right >= (BITS)) {                                        \
                res = 0;                                                  \
            } else {                                                      \
                res = e1 >> right;                                        \
            }                                                             \
        }                                                                 \
                                                                          \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_VAR_USHLR(NAME, RTYPE, ETYPE, WTYPE, EXTRACT, INSERT,   \
                            ELEMS, BITS, SAT_FN)                          \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
    int8_t shamt = (int8_t)(rs2 & 0xff);                                  \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE res;                                                        \
                                                                          \
        if (shamt >= 0) {                                                 \
            WTYPE shifted = (shamt >= (BITS)) ?                           \
                            ((WTYPE)e1 << (BITS)) :                       \
                            ((WTYPE)e1 << shamt);                         \
            res = SAT_FN(shifted, &sat);                                  \
        } else {                                                          \
            int right = -shamt;                                           \
            if (right > (BITS)) {                                         \
                res = 0;                                                  \
            } else {                                                      \
                WTYPE rounded = ((WTYPE)e1 >> (right - 1)) + 1;           \
                res = (ETYPE)(rounded >> 1);                              \
            }                                                             \
        }                                                                 \
                                                                          \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define PSIMD_DO_SRA64(A, B) ((A) >> (B))
#define PSIMD_DO_RNDSRA64(A, B)                                          \
    ((int64_t)((((__int128_t)(A) >> ((B) - 1)) + 1) >> 1))

#define GEN_PSIMD_VAR_SRA64(NAME, RIGHT_OP)                               \
uint64_t HELPER(NAME)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)     \
{                                                                         \
    int64_t a = (int64_t)rs1;                                            \
    int8_t shamt = (int8_t)(rs2 & 0xff);                                 \
                                                                          \
    if (shamt >= 0) {                                                     \
        return (uint64_t)(a << shamt);                                    \
    }                                                                     \
                                                                          \
    int right = -shamt;                                                   \
    if (right >= 64) {                                                    \
        return (a < 0) ? (uint64_t)-1 : 0;                               \
    }                                                                     \
    return (uint64_t)RIGHT_OP(a, right);                                  \
}

#define PSIMD_DO_SRL64(A, B) (((B) >= 64) ? 0 : ((A) >> (B)))
#define PSIMD_DO_RNDSRL64(A, B)                                          \
    (((B) > 64) ? 0 : ((((A) >> ((B) - 1)) + 1) >> 1))

#define GEN_PSIMD_VAR_SRL64(NAME, RIGHT_OP)                               \
uint64_t HELPER(NAME)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)     \
{                                                                         \
    int8_t shamt = (int8_t)(rs2 & 0xff);                                 \
                                                                          \
    if (shamt < 0) {                                                      \
        return RIGHT_OP(rs1, -shamt);                                     \
    }                                                                     \
    return (shamt >= 64) ? 0 : (rs1 << shamt);                           \
}

#define GEN_PSIMD_XPAIR_BINOP(NAME, RTYPE, ETYPE, EXTRACT, INSERT,        \
                              ELEMS, OP_LO, OP_HI)                        \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i += 2) {                                  \
        ETYPE s1_lo = (ETYPE)EXTRACT(rs1, i);                             \
        ETYPE s1_hi = (ETYPE)EXTRACT(rs1, i + 1);                         \
        ETYPE s2_lo = (ETYPE)EXTRACT(rs2, i);                             \
        ETYPE s2_hi = (ETYPE)EXTRACT(rs2, i + 1);                         \
        ETYPE res_lo = OP_LO(s1_lo, s2_hi);                               \
        ETYPE res_hi = OP_HI(s1_hi, s2_lo);                               \
        rd = INSERT(rd, res_lo, i);                                       \
        rd = INSERT(rd, res_hi, i + 1);                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_XPAIR_SAT_BINOP(NAME, RTYPE, ETYPE, WTYPE, EXTRACT,     \
                                  INSERT, ELEMS, OP_LO, OP_HI, SAT_FN)    \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < elems; i += 2) {                                  \
        ETYPE s1_lo = (ETYPE)EXTRACT(rs1, i);                             \
        ETYPE s1_hi = (ETYPE)EXTRACT(rs1, i + 1);                         \
        ETYPE s2_lo = (ETYPE)EXTRACT(rs2, i);                             \
        ETYPE s2_hi = (ETYPE)EXTRACT(rs2, i + 1);                         \
        WTYPE val_lo = OP_LO((WTYPE)s1_lo, (WTYPE)s2_hi);                 \
        WTYPE val_hi = OP_HI((WTYPE)s1_hi, (WTYPE)s2_lo);                 \
        ETYPE res_lo = SAT_FN(val_lo, &sat);                              \
        ETYPE res_hi = SAT_FN(val_hi, &sat);                              \
        rd = INSERT(rd, res_lo, i);                                       \
        rd = INSERT(rd, res_hi, i + 1);                                   \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_XPAIR_AVG_BINOP(NAME, RTYPE, ETYPE, WTYPE, EXTRACT,     \
                                  INSERT, ELEMS, OP_LO, OP_HI)            \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i += 2) {                                  \
        ETYPE s1_lo = (ETYPE)EXTRACT(rs1, i);                             \
        ETYPE s1_hi = (ETYPE)EXTRACT(rs1, i + 1);                         \
        ETYPE s2_lo = (ETYPE)EXTRACT(rs2, i);                             \
        ETYPE s2_hi = (ETYPE)EXTRACT(rs2, i + 1);                         \
        ETYPE res_lo = (ETYPE)(OP_LO((WTYPE)s1_lo, (WTYPE)s2_hi) >> 1);   \
        ETYPE res_hi = (ETYPE)(OP_HI((WTYPE)s1_hi, (WTYPE)s2_lo) >> 1);   \
        rd = INSERT(rd, res_lo, i);                                       \
        rd = INSERT(rd, res_hi, i + 1);                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_REDSUM(NAME, RTYPE, SUMTYPE, ETYPE, EXTRACT, ELEMS,     \
                         INIT)                                            \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    SUMTYPE sum = (INIT);                                                 \
    int elems = ELEMS(rs1);                                               \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        sum += e1;                                                        \
    }                                                                     \
                                                                          \
    return (RTYPE)sum;                                                    \
}

#define PSIMD_PAIR_LO(V, MASK, SHIFT) ((V) & (MASK))
#define PSIMD_PAIR_HI(V, MASK, SHIFT) (((V) >> (SHIFT)) & (MASK))

#define GEN_PSIMD_PAIR_PACK(NAME, RTYPE, ETYPE, EXTRACT, INSERT, ELEMS,   \
                            MASK, SHIFT, HI_SEL, LO_SEL)                 \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = EXTRACT(rs1, i);                                       \
        ETYPE e2 = EXTRACT(rs2, i);                                       \
        ETYPE res = (HI_SEL(e2, MASK, SHIFT) << (SHIFT)) |                \
                    LO_SEL(e1, MASK, SHIFT);                              \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_PAIR_WORD(NAME, RS1_IDX, RS2_IDX)                       \
uint64_t HELPER(NAME)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)     \
{                                                                         \
    uint32_t e1 = EXTRACT32(rs1, RS1_IDX);                                \
    uint32_t e2 = EXTRACT32(rs2, RS2_IDX);                                \
                                                                          \
    return ((uint64_t)e2 << 32) | e1;                                     \
}

#define GEN_PSIMD_SIGN_EXTEND(NAME, RTYPE, STYPE, DTYPE, EXTRACT, INSERT, \
                              ELEMS, SCALE)                               \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1)                         \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        STYPE e1 = (STYPE)EXTRACT(rs1, i * (SCALE));                      \
        rd = INSERT(rd, (DTYPE)e1, i);                                    \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_ZIP(NAME, ETYPE, EXTRACT, ELEMS, BITS, START)           \
uint64_t HELPER(NAME)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)     \
{                                                                         \
    uint64_t rd = 0;                                                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = EXTRACT(rs1, (START) - i);                             \
        ETYPE e2 = EXTRACT(rs2, (START) - i);                             \
        rd = (rd << (2 * (BITS))) | ((uint64_t)e2 << (BITS)) | e1;        \
    }                                                                     \
                                                                          \
    return rd;                                                            \
}

#define GEN_PSIMD_UNZIP(NAME, EXTRACT, ELEMS, BITS, OFFSET)               \
uint64_t HELPER(NAME)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)     \
{                                                                         \
    uint64_t rd = 0;                                                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        uint64_t e1 = (uint64_t)EXTRACT(rs1, 2 * i + (OFFSET)) <<         \
                      ((BITS) * i);                                       \
        uint64_t e2 = (uint64_t)EXTRACT(rs2, 2 * i + (OFFSET)) <<         \
                      (32 + (BITS) * i);                                  \
        rd = rd | e2 | e1;                                                \
    }                                                                     \
                                                                          \
    return rd;                                                            \
}

#define GEN_PSIMD_BIT_SELECT(NAME, MASK, TRUE_VAL, FALSE_VAL)             \
target_ulong HELPER(NAME)(CPURISCVState *env, target_ulong rs1,           \
                          target_ulong rs2, target_ulong rd)              \
{                                                                         \
    return (~(MASK) & (FALSE_VAL)) | ((MASK) & (TRUE_VAL));               \
}

#define GEN_PSIMD_NCLIP_PACK(NAME, ITYPE, OTYPE, EXTRACT, INSERT, ELEMS,  \
                             SAT_FN)                                      \
uint64_t HELPER(NAME)(CPURISCVState *env, uint64_t rs1, uint64_t rs2)     \
{                                                                         \
    uint64_t rd = 0;                                                      \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ITYPE lo = (ITYPE)EXTRACT(rs1, i);                                \
        ITYPE hi = (ITYPE)EXTRACT(rs2, i);                                \
        OTYPE res_lo = SAT_FN(lo, &sat);                                  \
        OTYPE res_hi = SAT_FN(hi, &sat);                                  \
                                                                          \
        rd = (uint64_t)INSERT(rd, res_lo, i);                             \
        rd = (uint64_t)INSERT(rd, res_hi, i + (ELEMS));                   \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_CLS(NAME, RTYPE, UTYPE, CLRSB)                          \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1)                         \
{                                                                         \
    return CLRSB((UTYPE)rs1);                                             \
}

#define PSIMD_MUL_S32(A, B)  ((int32_t)(A) * (int32_t)(B))
#define PSIMD_MUL_SU16(A, B) ((int16_t)(A) * (uint16_t)(B))
#define PSIMD_MUL_SU32(A, B) ((int32_t)(A) * (uint32_t)(B))
#define PSIMD_MUL_U32(A, B)  ((uint32_t)(A) * (uint32_t)(B))
#define PSIMD_MUL_S64(A, B)  ((int64_t)(A) * (int64_t)(B))
#define PSIMD_MUL_SU64(A, B) ((int64_t)(A) * (uint64_t)(B))
#define PSIMD_MUL_U64(A, B)  ((uint64_t)(A) * (uint64_t)(B))

#define GEN_PSIMD_MUL_HIGH(NAME, RTYPE, E1TYPE, E2TYPE, PTYPE, HTYPE,     \
                           EXTRACT, INSERT, ELEMS, SHIFT, ROUND, OP)     \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE e1 = (E1TYPE)EXTRACT(rs1, i);                              \
        E2TYPE e2 = (E2TYPE)EXTRACT(rs2, i);                              \
        PTYPE prod = OP(e1, e2) + (ROUND);                                \
        HTYPE high = (HTYPE)(prod >> (SHIFT));                            \
        rd = INSERT(rd, high, i);                                         \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_MUL_HIGH_SEL(NAME, RTYPE, E1TYPE, E2TYPE, PTYPE,        \
                               HTYPE, EXTRACT1, EXTRACT2, INSERT, ELEMS, \
                               SCALE, OFFSET, SHIFT, OP)                 \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE e1 = (E1TYPE)EXTRACT1(rs1, i);                             \
        E2TYPE e2 = (E2TYPE)EXTRACT2(rs2, i * (SCALE) + (OFFSET));        \
        PTYPE prod = OP(e1, e2);                                          \
        HTYPE high = (HTYPE)(prod >> (SHIFT));                            \
        rd = INSERT(rd, high, i);                                         \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_MUL_ELEM_INDEXED(NAME, RTYPE, E1TYPE, E2TYPE, PTYPE,    \
                                   OTYPE, EXTRACT, INSERT, ELEMS, SCALE, \
                                   OFFSET1, OFFSET2, OP)                 \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE e1 = (E1TYPE)EXTRACT(rs1, i * (SCALE) + (OFFSET1));        \
        E2TYPE e2 = (E2TYPE)EXTRACT(rs2, i * (SCALE) + (OFFSET2));        \
        PTYPE mul = OP(e1, e2);                                           \
        rd = INSERT(rd, (OTYPE)mul, i);                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_MUL_ELEM_SCALAR(NAME, RTYPE, E1TYPE, E2TYPE, PTYPE,     \
                                  EXTRACT, OFFSET1, OFFSET2, OP)         \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    E1TYPE e1 = (E1TYPE)EXTRACT(rs1, OFFSET1);                            \
    E2TYPE e2 = (E2TYPE)EXTRACT(rs2, OFFSET2);                            \
    PTYPE mul = OP(e1, e2);                                               \
                                                                          \
    return (RTYPE)mul;                                                    \
}

#define GEN_PSIMD_MUL_HIGH_ACC(NAME, RTYPE, E1TYPE, E2TYPE, DTYPE, PTYPE, \
                               HTYPE, OTYPE, EXTRACT, INSERT, ELEMS,     \
                               SHIFT, ROUND, OP)                         \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2, RTYPE dest)  \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE e1 = (E1TYPE)EXTRACT(rs1, i);                              \
        E2TYPE e2 = (E2TYPE)EXTRACT(rs2, i);                              \
        DTYPE d = (DTYPE)EXTRACT(dest, i);                                \
        PTYPE prod = OP(e1, e2) + (ROUND);                                \
        HTYPE high = (HTYPE)(prod >> (SHIFT));                            \
        OTYPE res = (OTYPE)(high + d);                                    \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_MUL_HIGH_ACC_SEL(NAME, RTYPE, E1TYPE, E2TYPE, DTYPE,    \
                                   PTYPE, HTYPE, OTYPE, EXTRACT1,        \
                                   EXTRACT2, INSERT, ELEMS, SCALE,       \
                                   OFFSET, SHIFT, OP)                    \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2, RTYPE dest)  \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE e1 = (E1TYPE)EXTRACT1(rs1, i);                             \
        E2TYPE e2 = (E2TYPE)EXTRACT2(rs2, i * (SCALE) + (OFFSET));        \
        DTYPE d = (DTYPE)EXTRACT1(dest, i);                               \
        PTYPE prod = OP(e1, e2);                                          \
        HTYPE high = (HTYPE)(prod >> (SHIFT));                            \
        OTYPE res = (OTYPE)(high + d);                                    \
        rd = INSERT(rd, res, i);                                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_MUL_ACC_INDEXED(NAME, RTYPE, E1TYPE, E2TYPE, DTYPE,     \
                                  PTYPE, OTYPE, EXTRACT, EXTRACTD,       \
                                  INSERT, ELEMS, SCALE, OFFSET1,         \
                                  OFFSET2, OP)                           \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2, RTYPE dest)  \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE e1 = (E1TYPE)EXTRACT(rs1, i * (SCALE) + (OFFSET1));        \
        E2TYPE e2 = (E2TYPE)EXTRACT(rs2, i * (SCALE) + (OFFSET2));        \
        DTYPE d = (DTYPE)EXTRACTD(dest, i);                               \
        PTYPE mul = OP(e1, e2);                                           \
        rd = INSERT(rd, (OTYPE)(d + mul), i);                             \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_QMUL(NAME, RTYPE, ETYPE, PTYPE, OTYPE, EXTRACT, INSERT, \
                       ELEMS, SHIFT, ROUND, MINVAL, MAXVAL)              \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i);                                \
        ETYPE e2 = (ETYPE)EXTRACT(rs2, i);                                \
        OTYPE result;                                                     \
                                                                          \
        if ((e1 == (ETYPE)(MINVAL)) && (e2 == (ETYPE)(MINVAL))) {         \
            sat = 1;                                                      \
            result = (OTYPE)(MAXVAL);                                     \
        } else {                                                          \
            PTYPE prod = (PTYPE)e1 * (PTYPE)e2 + (ROUND);                 \
            result = (OTYPE)(prod >> (SHIFT));                            \
        }                                                                 \
        rd = INSERT(rd, result, i);                                       \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_QMUL_ACC_INDEXED(NAME, RTYPE, E1TYPE, E2TYPE, DTYPE,    \
                                   PTYPE, STYPE, OTYPE, EXTRACT,         \
                                   EXTRACTD, INSERT, ELEMS, SCALE,       \
                                   OFFSET1, OFFSET2, SHIFT, ROUND, OP)   \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2, RTYPE dest)  \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE e1 = (E1TYPE)EXTRACT(rs1, i * (SCALE) + (OFFSET1));        \
        E2TYPE e2 = (E2TYPE)EXTRACT(rs2, i * (SCALE) + (OFFSET2));        \
        DTYPE d = (DTYPE)EXTRACTD(dest, i);                               \
        PTYPE prod = OP(e1, e2) + (ROUND);                                \
        STYPE scaled = (STYPE)(((__int128_t)prod) >> (SHIFT));            \
        rd = INSERT(rd, (OTYPE)(d + scaled), i);                          \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_Q2ADD(NAME, RTYPE, ETYPE, PTYPE, STYPE, OTYPE, EXTRACT, \
                        INSERT, ELEMS, SCALE, SHIFT, ROUND, OP)          \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE s1_0 = (ETYPE)EXTRACT(rs1, i * (SCALE));                   \
        ETYPE s1_1 = (ETYPE)EXTRACT(rs1, i * (SCALE) + 1);               \
        ETYPE s2_0 = (ETYPE)EXTRACT(rs2, i * (SCALE));                   \
        ETYPE s2_1 = (ETYPE)EXTRACT(rs2, i * (SCALE) + 1);               \
        PTYPE prod0 = OP(s1_0, s2_0) + (ROUND);                          \
        PTYPE prod1 = OP(s1_1, s2_1) + (ROUND);                          \
        STYPE scaled0 = (STYPE)(((__int128_t)prod0) >> (SHIFT));          \
        STYPE scaled1 = (STYPE)(((__int128_t)prod1) >> (SHIFT));          \
        rd = INSERT(rd, (OTYPE)(scaled0 + scaled1), i);                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_Q2ADDA(NAME, RTYPE, ETYPE, DTYPE, PTYPE, STYPE, OTYPE,  \
                         EXTRACT, EXTRACTD, INSERT, ELEMS, SCALE,        \
                         SHIFT, ROUND, OP)                               \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2, RTYPE dest)  \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        ETYPE s1_0 = (ETYPE)EXTRACT(rs1, i * (SCALE));                   \
        ETYPE s1_1 = (ETYPE)EXTRACT(rs1, i * (SCALE) + 1);               \
        ETYPE s2_0 = (ETYPE)EXTRACT(rs2, i * (SCALE));                   \
        ETYPE s2_1 = (ETYPE)EXTRACT(rs2, i * (SCALE) + 1);               \
        DTYPE d = (DTYPE)EXTRACTD(dest, i);                               \
        PTYPE prod0 = OP(s1_0, s2_0) + (ROUND);                          \
        PTYPE prod1 = OP(s1_1, s2_1) + (ROUND);                          \
        STYPE scaled0 = (STYPE)(((__int128_t)prod0) >> (SHIFT));          \
        STYPE scaled1 = (STYPE)(((__int128_t)prod1) >> (SHIFT));          \
        rd = INSERT(rd, (OTYPE)(d + scaled0 + scaled1), i);               \
    }                                                                     \
    return rd;                                                            \
}

#define PSIMD_COMB_ADD(D, A, B) ((D) + (A) + (B))
#define PSIMD_COMB_SUB(D, A, B) ((D) + (A) - (B))

#define GEN_PSIMD_2WAY_MUL(NAME, RTYPE, E1TYPE, E2TYPE, PTYPE, OTYPE,     \
                           EXTRACT, INSERT, ELEMS, SCALE, OFFSET10,      \
                           OFFSET11, OFFSET20, OFFSET21, OP, COMBINE)   \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE s1_0 = (E1TYPE)EXTRACT(rs1, i * (SCALE) + (OFFSET10));     \
        E1TYPE s1_1 = (E1TYPE)EXTRACT(rs1, i * (SCALE) + (OFFSET11));     \
        E2TYPE s2_0 = (E2TYPE)EXTRACT(rs2, i * (SCALE) + (OFFSET20));     \
        E2TYPE s2_1 = (E2TYPE)EXTRACT(rs2, i * (SCALE) + (OFFSET21));     \
        PTYPE prod0 = OP(s1_0, s2_0);                                     \
        PTYPE prod1 = OP(s1_1, s2_1);                                     \
        rd = INSERT(rd, (OTYPE)COMBINE(0, prod0, prod1), i);              \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_2WAY_SAT_MUL(NAME, OFFSET10, OFFSET11, OFFSET20,        \
                               OFFSET21)                                 \
target_ulong HELPER(NAME)(CPURISCVState *env, target_ulong rs1,           \
                           target_ulong rs2)                              \
{                                                                         \
    target_ulong rd = 0;                                                  \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < ELEMS_W(rd); i++) {                               \
        int16_t s1_0 = (int16_t)EXTRACT16(rs1, i * 2 + (OFFSET10));       \
        int16_t s1_1 = (int16_t)EXTRACT16(rs1, i * 2 + (OFFSET11));       \
        int16_t s2_0 = (int16_t)EXTRACT16(rs2, i * 2 + (OFFSET20));       \
        int16_t s2_1 = (int16_t)EXTRACT16(rs2, i * 2 + (OFFSET21));       \
        uint32_t result;                                                  \
                                                                          \
        if (s1_0 == INT16_MIN && s1_1 == INT16_MIN &&                    \
            s2_0 == INT16_MIN && s2_1 == INT16_MIN) {                    \
            result = INT32_MAX;                                          \
            sat = 1;                                                      \
        } else {                                                          \
            int32_t prod0 = (int32_t)s1_0 * s2_0;                        \
            int32_t prod1 = (int32_t)s1_1 * s2_1;                        \
            result = (uint32_t)(prod0 + prod1);                           \
        }                                                                 \
        rd = INSERT32(rd, result, i);                                     \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_2WAY_MUL_ACC(NAME, RTYPE, E1TYPE, E2TYPE, DTYPE, PTYPE, \
                               OTYPE, EXTRACT, EXTRACTD, INSERT, ELEMS,  \
                               SCALE, OFFSET10, OFFSET11, OFFSET20,      \
                               OFFSET21, OP, COMBINE)                   \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2, RTYPE dest)  \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        E1TYPE s1_0 = (E1TYPE)EXTRACT(rs1, i * (SCALE) + (OFFSET10));     \
        E1TYPE s1_1 = (E1TYPE)EXTRACT(rs1, i * (SCALE) + (OFFSET11));     \
        E2TYPE s2_0 = (E2TYPE)EXTRACT(rs2, i * (SCALE) + (OFFSET20));     \
        E2TYPE s2_1 = (E2TYPE)EXTRACT(rs2, i * (SCALE) + (OFFSET21));     \
        DTYPE d = (DTYPE)EXTRACTD(dest, i);                               \
        PTYPE prod0 = OP(s1_0, s2_0);                                     \
        PTYPE prod1 = OP(s1_1, s2_1);                                     \
        rd = INSERT(rd, (OTYPE)COMBINE(d, prod0, prod1), i);              \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_4WAY_MUL(NAME, RTYPE, E1TYPE, E2TYPE, PTYPE, OTYPE,     \
                           EXTRACT, INSERT, ELEMS, SCALE, OP)            \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2)              \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        PTYPE prod0 = OP((E1TYPE)EXTRACT(rs1, i * (SCALE)),              \
                         (E2TYPE)EXTRACT(rs2, i * (SCALE)));             \
        PTYPE prod1 = OP((E1TYPE)EXTRACT(rs1, i * (SCALE) + 1),          \
                         (E2TYPE)EXTRACT(rs2, i * (SCALE) + 1));         \
        PTYPE prod2 = OP((E1TYPE)EXTRACT(rs1, i * (SCALE) + 2),          \
                         (E2TYPE)EXTRACT(rs2, i * (SCALE) + 2));         \
        PTYPE prod3 = OP((E1TYPE)EXTRACT(rs1, i * (SCALE) + 3),          \
                         (E2TYPE)EXTRACT(rs2, i * (SCALE) + 3));         \
        rd = INSERT(rd, (OTYPE)(prod0 + prod1 + prod2 + prod3), i);       \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_4WAY_MUL_ACC(NAME, RTYPE, E1TYPE, E2TYPE, DTYPE, PTYPE, \
                               OTYPE, EXTRACT, EXTRACTD, INSERT, ELEMS,  \
                               SCALE, OP)                                \
RTYPE HELPER(NAME)(CPURISCVState *env, RTYPE rs1, RTYPE rs2, RTYPE dest)  \
{                                                                         \
    RTYPE rd = 0;                                                         \
    int elems = ELEMS(rd);                                                \
                                                                          \
    for (int i = 0; i < elems; i++) {                                     \
        DTYPE d = (DTYPE)EXTRACTD(dest, i);                               \
        PTYPE prod0 = OP((E1TYPE)EXTRACT(rs1, i * (SCALE)),              \
                         (E2TYPE)EXTRACT(rs2, i * (SCALE)));             \
        PTYPE prod1 = OP((E1TYPE)EXTRACT(rs1, i * (SCALE) + 1),          \
                         (E2TYPE)EXTRACT(rs2, i * (SCALE) + 1));         \
        PTYPE prod2 = OP((E1TYPE)EXTRACT(rs1, i * (SCALE) + 2),          \
                         (E2TYPE)EXTRACT(rs2, i * (SCALE) + 2));         \
        PTYPE prod3 = OP((E1TYPE)EXTRACT(rs1, i * (SCALE) + 3),          \
                         (E2TYPE)EXTRACT(rs2, i * (SCALE) + 3));         \
        rd = INSERT(rd, (OTYPE)(d + prod0 + prod1 + prod2 + prod3), i);   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_WIDEN_BINOP(NAME, ETYPE, WTYPE, OTYPE, ELEMS, IBITS,    \
                              OBITS, IMASK, OP)                          \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)     \
{                                                                         \
    uint64_t rd = 0;                                                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((rs1 >> (i * (IBITS))) & (IMASK));             \
        ETYPE e2 = (ETYPE)((rs2 >> (i * (IBITS))) & (IMASK));             \
        WTYPE res = OP((WTYPE)0, (WTYPE)e1, (WTYPE)e2);                   \
        rd |= ((uint64_t)(OTYPE)res) << (i * (OBITS));                    \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_WIDEN_BINOP_ACC(NAME, ETYPE, WTYPE, OTYPE, ELEMS,       \
                                  IBITS, OBITS, IMASK, OMASK, OP)        \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2,     \
                      uint64_t dest)                                      \
{                                                                         \
    uint64_t rd = 0;                                                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((rs1 >> (i * (IBITS))) & (IMASK));             \
        ETYPE e2 = (ETYPE)((rs2 >> (i * (IBITS))) & (IMASK));             \
        WTYPE acc = (WTYPE)((dest >> (i * (OBITS))) & (OMASK));           \
        WTYPE res = OP(acc, (WTYPE)e1, (WTYPE)e2);                        \
        rd |= ((uint64_t)(OTYPE)res) << (i * (OBITS));                    \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_WIDEN_MUL(NAME, E1TYPE, E2TYPE, PTYPE, OTYPE, ELEMS,    \
                            EXTRACT, OBITS, OP)                          \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)     \
{                                                                         \
    uint64_t rd = 0;                                                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        E1TYPE e1 = (E1TYPE)EXTRACT(rs1, i);                              \
        E2TYPE e2 = (E2TYPE)EXTRACT(rs2, i);                              \
        PTYPE prod = OP(e1, e2);                                          \
        rd |= ((uint64_t)(OTYPE)prod) << (i * (OBITS));                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_WIDEN_MUL_ACC(NAME, E1TYPE, E2TYPE, DTYPE, PTYPE,       \
                                OTYPE, ELEMS, EXTRACT, EXTRACTD, OBITS,  \
                                OP)                                      \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2,     \
                      uint64_t dest)                                      \
{                                                                         \
    uint64_t rd = 0;                                                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        E1TYPE e1 = (E1TYPE)EXTRACT(rs1, i);                              \
        E2TYPE e2 = (E2TYPE)EXTRACT(rs2, i);                              \
        DTYPE d = (DTYPE)EXTRACTD(dest, i);                               \
        PTYPE prod = OP(e1, e2);                                          \
        rd |= ((uint64_t)(OTYPE)(d + prod)) << (i * (OBITS));             \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_WIDEN_QMUL_ACC(NAME, ETYPE, DTYPE, PTYPE, STYPE,        \
                                 OTYPE, ELEMS, EXTRACT, EXTRACTD, SCALE, \
                                 OFFSET, SHIFT, ROUND, OBITS, OP)        \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2,     \
                      uint64_t dest)                                      \
{                                                                         \
    uint64_t rd = 0;                                                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)EXTRACT(rs1, i * (SCALE) + (OFFSET));           \
        ETYPE e2 = (ETYPE)EXTRACT(rs2, i * (SCALE) + (OFFSET));           \
        DTYPE d = (DTYPE)EXTRACTD(dest, i);                               \
        PTYPE prod = OP(e1, e2) + (ROUND);                                \
        STYPE scaled = (STYPE)(((__int128_t)prod) >> (SHIFT));            \
        rd |= ((uint64_t)(OTYPE)(d + scaled)) << (i * (OBITS));           \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_DW_2WAY_MUL(NAME, E1TYPE, E2TYPE, PTYPE, EXTRACT,       \
                              OFFSET10, OFFSET11, OFFSET20, OFFSET21,    \
                              OP, COMBINE)                               \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)     \
{                                                                         \
    E1TYPE s1_0 = (E1TYPE)EXTRACT(rs1, OFFSET10);                         \
    E1TYPE s1_1 = (E1TYPE)EXTRACT(rs1, OFFSET11);                         \
    E2TYPE s2_0 = (E2TYPE)EXTRACT(rs2, OFFSET20);                         \
    E2TYPE s2_1 = (E2TYPE)EXTRACT(rs2, OFFSET21);                         \
    PTYPE prod0 = OP(s1_0, s2_0);                                        \
    PTYPE prod1 = OP(s1_1, s2_1);                                        \
                                                                          \
    return (uint64_t)COMBINE(0, prod0, prod1);                            \
}

#define GEN_PSIMD_DW_2WAY_MUL_ACC(NAME, E1TYPE, E2TYPE, DTYPE, PTYPE,     \
                                  EXTRACT, OFFSET10, OFFSET11, OFFSET20, \
                                  OFFSET21, OP, COMBINE)                \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2,     \
                      uint64_t dest)                                      \
{                                                                         \
    E1TYPE s1_0 = (E1TYPE)EXTRACT(rs1, OFFSET10);                         \
    E1TYPE s1_1 = (E1TYPE)EXTRACT(rs1, OFFSET11);                         \
    E2TYPE s2_0 = (E2TYPE)EXTRACT(rs2, OFFSET20);                         \
    E2TYPE s2_1 = (E2TYPE)EXTRACT(rs2, OFFSET21);                         \
    DTYPE d = (DTYPE)dest;                                                \
    PTYPE prod0 = OP(s1_0, s2_0);                                        \
    PTYPE prod1 = OP(s1_1, s2_1);                                        \
                                                                          \
    return (uint64_t)COMBINE(d, prod0, prod1);                            \
}

#define GEN_PSIMD_WIDEN_SHIFT(NAME, ETYPE, WTYPE, OTYPE, ELEMS, IBITS,    \
                              OBITS, IMASK, SHMASK)                      \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)     \
{                                                                         \
    uint64_t rd = 0;                                                      \
    uint8_t shamt = rs2 & (SHMASK);                                       \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((rs1 >> (i * (IBITS))) & (IMASK));             \
        WTYPE res = (WTYPE)e1 << shamt;                                   \
        rd |= ((uint64_t)(OTYPE)res) << (i * (OBITS));                    \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_DW_ZIP(NAME, EXTRACT, ELEMS, STRIDE, BITS)              \
uint64_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1, uint32_t rs2)     \
{                                                                         \
    uint64_t rd = 0;                                                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        uint64_t e1 = (uint64_t)EXTRACT(rs1, i) << ((STRIDE) * i);        \
        uint64_t e2 = (uint64_t)EXTRACT(rs2, i)                           \
                      << ((STRIDE) * i + (BITS));                         \
        rd |= e2 | e1;                                                    \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_DW_REDSUM(NAME, SUMTYPE, INITTYPE, ETYPE, EXTRACT,      \
                            ELEMS)                                        \
uint32_t HELPER(NAME)(CPURISCVState *env, uint32_t rs1_lo,                \
                      uint32_t rs1_hi, uint32_t rs2)                      \
{                                                                         \
    SUMTYPE sum = (INITTYPE)rs2;                                          \
    uint64_t s1 = ((uint64_t)rs1_hi << 32) | rs1_lo;                      \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        sum += (ETYPE)EXTRACT(s1, i);                                     \
    }                                                                     \
    return (uint32_t)sum;                                                 \
}

#define GEN_PSIMD_NARROW_SRL(NAME, ETYPE, OTYPE, ELEMS, IBITS, OBITS,     \
                             IMASK, SHMASK)                               \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    uint32_t rd = 0;                                                      \
    uint8_t shift = shamt & (SHMASK);                                     \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((s1 >> (i * (IBITS))) & (IMASK));              \
        OTYPE result = (OTYPE)(e1 >> shift);                              \
        rd |= ((uint32_t)result) << (i * (OBITS));                        \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_NARROW_SRA_PACK(NAME, ETYPE, STYPE, OTYPE, ELEMS,       \
                                  IBITS, OBITS, IMASK, SHMASK)            \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    uint32_t rd = 0;                                                      \
    uint8_t shift = shamt & (SHMASK);                                     \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((s1 >> (i * (IBITS))) & (IMASK));              \
        STYPE shx = (STYPE)e1 >> shift;                                   \
        OTYPE result = (OTYPE)shx;                                        \
        rd |= ((uint32_t)result) << (i * (OBITS));                        \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_NARROW_RNDSRA_PACK(NAME, ETYPE, STYPE, OTYPE, ELEMS,    \
                                     IBITS, OBITS, IMASK, SHMASK, RMASK)  \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    uint32_t rd = 0;                                                      \
    uint8_t shift = shamt & (SHMASK);                                     \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((s1 >> (i * (IBITS))) & (IMASK));              \
        STYPE e1_s = (STYPE)e1;                                           \
        uint64_t shx = (((uint64_t)e1_s << 1) >> shift) & (RMASK);        \
        OTYPE result = (OTYPE)((shx + 1) >> 1);                           \
        rd |= ((uint32_t)result) << (i * (OBITS));                        \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_NCLIP_SIGNED_PACK(NAME, ETYPE, STYPE, CTYPE, OTYPE,     \
                                    ELEMS, IBITS, OBITS, IMASK, SHMASK,   \
                                    MIN, MAX, MIN_RES, MAX_RES)           \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    uint32_t rd = 0;                                                      \
    uint8_t shift = shamt & (SHMASK);                                     \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((s1 >> (i * (IBITS))) & (IMASK));              \
        CTYPE shx = (CTYPE)((STYPE)e1 >> shift);                          \
        OTYPE result;                                                     \
                                                                          \
        if (shx < (MIN)) {                                                \
            sat = 1;                                                      \
            result = (MIN_RES);                                           \
        } else if (shx > (MAX)) {                                         \
            sat = 1;                                                      \
            result = (MAX_RES);                                           \
        } else {                                                          \
            result = (OTYPE)shx;                                          \
        }                                                                 \
        rd |= ((uint32_t)result) << (i * (OBITS));                        \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_NCLIPR_SIGNED_PACK(NAME, ETYPE, STYPE, CTYPE, OTYPE,    \
                                     ELEMS, IBITS, OBITS, IMASK, SHMASK,  \
                                     RMASK, MIN, MAX, MIN_RES, MAX_RES)   \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    uint32_t rd = 0;                                                      \
    uint8_t shift = shamt & (SHMASK);                                     \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((s1 >> (i * (IBITS))) & (IMASK));              \
        STYPE e1_s = (STYPE)e1;                                           \
        uint64_t shx = (((uint64_t)e1_s << 1) >> shift) & (RMASK);        \
        CTYPE round_shx = (CTYPE)((shx + 1) >> 1);                        \
        OTYPE result;                                                     \
                                                                          \
        if (round_shx < (MIN)) {                                          \
            sat = 1;                                                      \
            result = (MIN_RES);                                           \
        } else if (round_shx > (MAX)) {                                   \
            sat = 1;                                                      \
            result = (MAX_RES);                                           \
        } else {                                                          \
            result = (OTYPE)round_shx;                                    \
        }                                                                 \
        rd |= ((uint32_t)result) << (i * (OBITS));                        \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_NCLIP_UNSIGNED_PACK(NAME, ETYPE, WTYPE, OTYPE, ELEMS,   \
                                      IBITS, OBITS, IMASK, SHMASK, MAX)   \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    uint32_t rd = 0;                                                      \
    uint8_t shift = shamt & (SHMASK);                                     \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((s1 >> (i * (IBITS))) & (IMASK));              \
        WTYPE shx = (WTYPE)e1 >> shift;                                   \
        OTYPE result;                                                     \
                                                                          \
        if (shx > (MAX)) {                                                \
            sat = 1;                                                      \
            result = (OTYPE)(MAX);                                        \
        } else {                                                          \
            result = (OTYPE)shx;                                          \
        }                                                                 \
        rd |= ((uint32_t)result) << (i * (OBITS));                        \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_NCLIPR_UNSIGNED_PACK(NAME, ETYPE, WTYPE, OTYPE, ELEMS,  \
                                       IBITS, OBITS, IMASK, SHMASK, MAX)  \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    uint32_t rd = 0;                                                      \
    uint8_t shift = shamt & (SHMASK);                                     \
    int sat = 0;                                                          \
                                                                          \
    for (int i = 0; i < (ELEMS); i++) {                                   \
        ETYPE e1 = (ETYPE)((s1 >> (i * (IBITS))) & (IMASK));              \
        WTYPE shx = ((WTYPE)e1 << 1) >> shift;                            \
        WTYPE round_shx = (shx + 1) >> 1;                                 \
        OTYPE result;                                                     \
                                                                          \
        if (round_shx > (MAX)) {                                          \
            sat = 1;                                                      \
            result = (OTYPE)(MAX);                                        \
        } else {                                                          \
            result = (OTYPE)round_shx;                                    \
        }                                                                 \
        rd |= ((uint32_t)result) << (i * (OBITS));                        \
    }                                                                     \
                                                                          \
    if (sat) {                                                            \
        env->vxsat = 1;                                                   \
    }                                                                     \
    return rd;                                                            \
}

#define GEN_PSIMD_NARROW_SRA(NAME)                                        \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);                       \
    __int128_t s1_s96 = (s1_s128 << 32) >> 32;                            \
                                                                          \
    return (uint32_t)(s1_s96 >> (shamt & 0x3F)) & 0xFFFFFFFF;             \
}

#define GEN_PSIMD_NARROW_RNDSRA(NAME)                                     \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);                       \
    __int128_t s1_s96 = (s1_s128 << 32) >> 32;                            \
    __uint128_t shx_97bit = ((__uint128_t)s1_s96 << 1);                   \
    uint64_t shx = (uint64_t)(shx_97bit >> (shamt & 0x3F)) & 0x1FFFFFFFF; \
                                                                          \
    return (uint32_t)((shx + 1) >> 1);                                    \
}

#define GEN_PSIMD_NARROW_ALIAS(NAME, TARGET)                              \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    return HELPER(TARGET)(env, s1, shamt);                                \
}

typedef struct {
    __uint128_t low;
    uint8_t high;
} PsImdUint129;

static inline PsImdUint129 psimd_uint129_lshift1(__int128_t val)
{
    PsImdUint129 result;
    __uint128_t uval = (__uint128_t)val;

    result.low = uval << 1;
    result.high = (uval >> 127) & 0x1;
    return result;
}

static inline PsImdUint129 psimd_uint129_rshift(PsImdUint129 val,
                                                uint32_t shamt)
{
    PsImdUint129 result;

    if (shamt == 0) {
        return val;
    } else if (shamt >= 129) {
        result.low = 0;
        result.high = 0;
    } else if (shamt == 128) {
        result.low = val.high;
        result.high = 0;
    } else {
        result.low = (val.low >> shamt) |
                     ((__uint128_t)val.high << (128 - shamt));
        result.high = val.high >> shamt;
    }
    return result;
}

#define GEN_PSIMD_NCLIP(NAME)                                             \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);                       \
    int64_t shx = (int64_t)(s1_s128 >> (shamt & 0x3F));                  \
                                                                          \
    if (shx < -2147483648LL) {                                            \
        env->vxsat = 1;                                                   \
        return 0x80000000U;                                               \
    } else if (shx > 2147483647LL) {                                      \
        env->vxsat = 1;                                                   \
        return 0x7FFFFFFFU;                                               \
    } else {                                                              \
        return (uint32_t)(shx & 0xFFFFFFFF);                              \
    }                                                                     \
}

#define GEN_PSIMD_NCLIPR(NAME)                                            \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    __int128_t s1_s128 = (__int128_t)((int64_t)s1);                       \
    PsImdUint129 shx_129bit = psimd_uint129_lshift1(s1_s128);             \
    PsImdUint129 shx = psimd_uint129_rshift(shx_129bit, shamt & 0x3F);    \
    int64_t round_shx = (int64_t)((shx.low + 1) >> 1);                    \
                                                                          \
    if (round_shx < -2147483648LL) {                                      \
        env->vxsat = 1;                                                   \
        return 0x80000000U;                                               \
    } else if (round_shx > 2147483647LL) {                                \
        env->vxsat = 1;                                                   \
        return 0x7FFFFFFFU;                                               \
    } else {                                                              \
        return (uint32_t)round_shx;                                       \
    }                                                                     \
}

#define GEN_PSIMD_NCLIPU(NAME)                                            \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    uint64_t shx = s1 >> (shamt & 0x3F);                                  \
                                                                          \
    if (shx > 4294967295ULL) {                                            \
        env->vxsat = 1;                                                   \
        return 0xFFFFFFFFU;                                               \
    } else {                                                              \
        return (uint32_t)(shx & 0xFFFFFFFF);                              \
    }                                                                     \
}

#define GEN_PSIMD_NCLIPRU(NAME)                                           \
uint32_t HELPER(NAME)(CPURISCVState *env, uint64_t s1, uint32_t shamt)    \
{                                                                         \
    __uint128_t shx_65bit = (__uint128_t)s1 << 1;                         \
    __uint128_t shx = shx_65bit >> (shamt & 0x3F);                        \
    uint64_t round_shx = (shx + 1) >> 1;                                  \
                                                                          \
    if (round_shx > 4294967295ULL) {                                      \
        env->vxsat = 1;                                                   \
        return 0xFFFFFFFFU;                                               \
    } else {                                                              \
        return (uint32_t)(round_shx & 0xFFFFFFFF);                        \
    }                                                                     \
}

/* Basic addition operations (non-saturating) */

GEN_PSIMD_BINOP(padd_b, target_ulong, uint8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_ADD)
GEN_PSIMD_BINOP(padd_h, target_ulong, uint16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_ADD)
GEN_PSIMD_BINOP(padd_w, uint64_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD)

GEN_PSIMD_BINOP_SCALAR(padd_bs, target_ulong, uint8_t,
                       EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_ADD)
GEN_PSIMD_BINOP_SCALAR(padd_hs, target_ulong, uint16_t,
                       EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_ADD)
GEN_PSIMD_BINOP_SCALAR(padd_ws, uint64_t, uint32_t,
                       EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD)

/* Basic subtraction operations (non-saturating) */

GEN_PSIMD_BINOP(psub_b, target_ulong, uint8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_SUB)
GEN_PSIMD_BINOP(psub_h, target_ulong, uint16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_SUB)
GEN_PSIMD_BINOP(psub_w, uint64_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_SUB)

/* Shift-left-by-one and add operations */

GEN_PSIMD_SHADD(psh1add_h, target_ulong, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, 1)
GEN_PSIMD_SHADD(psh1add_w, uint64_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, 1)

GEN_PSIMD_SAT_SHADD(pssh1sadd_h, target_ulong, int16_t, int32_t,
                    EXTRACT16, INSERT16, ELEMS_H, 1, -0x4000, 0x3fff,
                    SAT_MIN_H, SAT_MAX_H, signed_saturate_h)
GEN_PSIMD_SAT_SHADD(pssh1sadd_w, uint64_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, 1, -0x40000000,
                    0x3fffffff, SAT_MIN_W, SAT_MAX_W, signed_saturate_w)

GEN_PSIMD_SAT_SHADD(ssh1sadd, uint32_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, 1, -0x40000000,
                    0x3fffffff, SAT_MIN_W, SAT_MAX_W, signed_saturate_w)

/* Saturating addition operations */

GEN_PSIMD_SAT_BINOP(psadd_b, target_ulong, int8_t, int32_t,
                    EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_ADD,
                    signed_saturate_b)
GEN_PSIMD_SAT_BINOP(psadd_h, target_ulong, int16_t, int32_t,
                    EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_ADD,
                    signed_saturate_h)
GEN_PSIMD_SAT_BINOP(psadd_w, uint64_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD,
                    signed_saturate_w)

GEN_PSIMD_SAT_BINOP(psaddu_b, target_ulong, uint8_t, uint32_t,
                    EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_ADD,
                    unsigned_saturate_b)
GEN_PSIMD_SAT_BINOP(psaddu_h, target_ulong, uint16_t, uint32_t,
                    EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_ADD,
                    unsigned_saturate_h)
GEN_PSIMD_SAT_BINOP(psaddu_w, uint64_t, uint32_t, uint64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD,
                    unsigned_saturate_w)

GEN_PSIMD_SAT_BINOP(sadd, uint32_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD,
                    signed_saturate_w)
GEN_PSIMD_SAT_BINOP(saddu, uint32_t, uint32_t, uint64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD,
                    unsigned_saturate_w)

/* Saturating subtraction operations */

GEN_PSIMD_SAT_BINOP(pssub_b, target_ulong, int8_t, int32_t,
                    EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_SUB,
                    signed_saturate_b)
GEN_PSIMD_SAT_BINOP(pssub_h, target_ulong, int16_t, int32_t,
                    EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_SUB,
                    signed_saturate_h)
GEN_PSIMD_SAT_BINOP(pssub_w, uint64_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_SUB,
                    signed_saturate_w)

GEN_PSIMD_USUB_SAT(pssubu_b, target_ulong, uint8_t,
                   EXTRACT8, INSERT8, ELEMS_B)
GEN_PSIMD_USUB_SAT(pssubu_h, target_ulong, uint16_t,
                   EXTRACT16, INSERT16, ELEMS_H)
GEN_PSIMD_USUB_SAT(pssubu_w, uint64_t, uint32_t,
                   EXTRACT32, INSERT32, ELEMS_W)

GEN_PSIMD_SAT_BINOP(ssub, uint32_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_SUB,
                    signed_saturate_w)
GEN_PSIMD_USUB_SAT(ssubu, uint32_t, uint32_t,
                   EXTRACT32, INSERT32, ELEMS_W)

/* Saturation instructions (SAT, USAT) */

GEN_PSIMD_SATI(psati_h, target_ulong, int16_t, int64_t,
               EXTRACT16, INSERT16, ELEMS_H, 0x0f)
GEN_PSIMD_USATI(pusati_h, target_ulong, int16_t, uint16_t, uint32_t,
                EXTRACT16, INSERT16, ELEMS_H, 1U)
GEN_PSIMD_SATI(psati_w, uint64_t, int32_t, int64_t,
               EXTRACT32, INSERT32, ELEMS_W, 0x1f)
GEN_PSIMD_USATI(pusati_w, uint64_t, int32_t, uint32_t, uint64_t,
                EXTRACT32, INSERT32, ELEMS_W, 1ULL)
GEN_PSIMD_SATI(sati_32, uint32_t, int32_t, int64_t,
               EXTRACT32, INSERT32, ELEMS_W, 0x1f)
GEN_PSIMD_USATI(usati_32, uint32_t, int32_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, 1U)
GEN_PSIMD_SATI(sati_64, uint64_t, int64_t, int64_t,
               EXTRACT64, INSERT64, ELEMS_D, 0x3f)
GEN_PSIMD_USATI(usati_64, uint64_t, int64_t, uint64_t, uint64_t,
                EXTRACT64, INSERT64, ELEMS_D, 1ULL)

/* Averaging Operations (non-saturating) */

GEN_PSIMD_AVG_BINOP(paadd_b, target_ulong, int8_t, int16_t,
                    EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_ADD)
GEN_PSIMD_AVG_BINOP(paadd_h, target_ulong, int16_t, int32_t,
                    EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_ADD)
GEN_PSIMD_AVG_BINOP(paadd_w, uint64_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD)

GEN_PSIMD_AVG_BINOP(paaddu_b, target_ulong, uint8_t, uint16_t,
                    EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_ADD)
GEN_PSIMD_AVG_BINOP(paaddu_h, target_ulong, uint16_t, uint32_t,
                    EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_ADD)
GEN_PSIMD_AVG_BINOP(paaddu_w, uint64_t, uint32_t, uint64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD)

GEN_PSIMD_AVG_BINOP(aadd, uint32_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD)
GEN_PSIMD_AVG_BINOP(aaddu, uint32_t, uint32_t, uint64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_ADD)

GEN_PSIMD_AVG_BINOP(pasub_b, target_ulong, int8_t, int16_t,
                    EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_SUB)
GEN_PSIMD_AVG_BINOP(pasub_h, target_ulong, int16_t, int32_t,
                    EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_SUB)
GEN_PSIMD_AVG_BINOP(pasub_w, uint64_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_SUB)

GEN_PSIMD_AVG_BINOP(pasubu_b, target_ulong, uint8_t, uint16_t,
                    EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_SUB)
GEN_PSIMD_AVG_BINOP(pasubu_h, target_ulong, uint16_t, uint32_t,
                    EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_SUB)
GEN_PSIMD_AVG_BINOP(pasubu_w, uint64_t, uint32_t, uint64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_SUB)

GEN_PSIMD_AVG_BINOP(asub, uint32_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_SUB)
GEN_PSIMD_AVG_BINOP(asubu, uint32_t, uint32_t, uint64_t,
                    EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_SUB)

/* Absolute value operations */

GEN_PSIMD_ABS(psabs_b, target_ulong, int8_t,
              EXTRACT8, INSERT8, ELEMS_B, INT8_MIN, INT8_MAX)
GEN_PSIMD_ABS(psabs_h, target_ulong, int16_t,
              EXTRACT16, INSERT16, ELEMS_H, INT16_MIN, INT16_MAX)

GEN_PSIMD_SCALAR_ABS(abs, target_ulong, target_long, target_ulong)
GEN_PSIMD_SCALAR_ABS(absw, uint64_t, int32_t, uint32_t)

/* Absolute difference operations */

GEN_PSIMD_BINOP(pabd_b, target_ulong, int8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_ABD)
GEN_PSIMD_BINOP(pabdu_b, target_ulong, uint8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_ABD)
GEN_PSIMD_BINOP(pabd_h, target_ulong, int16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_ABD)
GEN_PSIMD_BINOP(pabdu_h, target_ulong, uint16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_ABD)

/**
 * PABDSUMU.B - Sum of unsigned absolute differences
 * Returns sum(|rs1[i] - rs2[i]|) for all bytes
 */
target_ulong HELPER(pabdsumu_b)(CPURISCVState *env,
                                target_ulong rs1, target_ulong rs2)
{
    return psimd_abdsumu_b(rs1, rs2, 0);
}

/**
 * PABDSUMAU.B - Accumulated sum of unsigned absolute differences
 * rd = rd + sum(|rs1[i] - rs2[i]|)
 */
target_ulong HELPER(pabdsumau_b)(CPURISCVState *env, target_ulong rs1,
                                 target_ulong rs2, target_ulong rd)
{
    return psimd_abdsumu_b(rs1, rs2, rd);
}

/* Comparison operations (producing masks) */

GEN_PSIMD_BINOP(pmseq_b, target_ulong, uint8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_EQ_MASK)
GEN_PSIMD_BINOP(pmslt_b, target_ulong, int8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_LT_MASK)
GEN_PSIMD_BINOP(pmsltu_b, target_ulong, uint8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_LT_MASK)
GEN_PSIMD_BINOP(pmin_b, target_ulong, int8_t, int8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_MIN)
GEN_PSIMD_BINOP(pminu_b, target_ulong, uint8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_MIN)
GEN_PSIMD_BINOP(pmax_b, target_ulong, int8_t, int8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_MAX)
GEN_PSIMD_BINOP(pmaxu_b, target_ulong, uint8_t, uint8_t,
                EXTRACT8, INSERT8, ELEMS_B, PSIMD_DO_MAX)

GEN_PSIMD_BINOP(pmseq_h, target_ulong, uint16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_EQ_MASK)
GEN_PSIMD_BINOP(pmslt_h, target_ulong, int16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_LT_MASK)
GEN_PSIMD_BINOP(pmsltu_h, target_ulong, uint16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_LT_MASK)
GEN_PSIMD_BINOP(pmin_h, target_ulong, int16_t, int16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_MIN)
GEN_PSIMD_BINOP(pminu_h, target_ulong, uint16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_MIN)
GEN_PSIMD_BINOP(pmax_h, target_ulong, int16_t, int16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_MAX)
GEN_PSIMD_BINOP(pmaxu_h, target_ulong, uint16_t, uint16_t,
                EXTRACT16, INSERT16, ELEMS_H, PSIMD_DO_MAX)

GEN_PSIMD_BINOP(pmseq_w, uint64_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_EQ_MASK)
GEN_PSIMD_BINOP(pmslt_w, uint64_t, int32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_LT_MASK)
GEN_PSIMD_BINOP(pmsltu_w, uint64_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_LT_MASK)
GEN_PSIMD_BINOP(pmin_w, uint64_t, int32_t, int32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_MIN)
GEN_PSIMD_BINOP(pminu_w, uint64_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_MIN)
GEN_PSIMD_BINOP(pmax_w, uint64_t, int32_t, int32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_MAX)
GEN_PSIMD_BINOP(pmaxu_w, uint64_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_MAX)

GEN_PSIMD_BINOP(mseq, uint32_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_EQ_MASK)
GEN_PSIMD_BINOP(mslt, uint32_t, int32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_LT_MASK)
GEN_PSIMD_BINOP(msltu, uint32_t, uint32_t, uint32_t,
                EXTRACT32, INSERT32, ELEMS_W, PSIMD_DO_LT_MASK)

/* Shift operations (immediate and register) */

GEN_PSIMD_SHIFTOP(pslli_b, target_ulong, uint8_t, uint8_t,
                  EXTRACT8, INSERT8, ELEMS_B, 0x07, PSIMD_DO_SLL)
GEN_PSIMD_SHIFTOP(psll_bs, target_ulong, uint8_t, uint8_t,
                  EXTRACT8, INSERT8, ELEMS_B, 0x07, PSIMD_DO_SLL)
GEN_PSIMD_SHIFTOP(pslli_h, target_ulong, uint16_t, uint16_t,
                  EXTRACT16, INSERT16, ELEMS_H, 0x0f, PSIMD_DO_SLL)
GEN_PSIMD_SHIFTOP(psll_hs, target_ulong, uint16_t, uint16_t,
                  EXTRACT16, INSERT16, ELEMS_H, 0x0f, PSIMD_DO_SLL)
GEN_PSIMD_SHIFTOP(pslli_w, uint64_t, uint32_t, uint32_t,
                  EXTRACT32, INSERT32, ELEMS_W, 0x1f, PSIMD_DO_SLL)
GEN_PSIMD_SHIFTOP(psll_ws, uint64_t, uint32_t, uint32_t,
                  EXTRACT32, INSERT32, ELEMS_W, 0x1f, PSIMD_DO_SLL)

GEN_PSIMD_SHIFTOP(psrli_b, target_ulong, uint8_t, uint8_t,
                  EXTRACT8, INSERT8, ELEMS_B, 0x07, PSIMD_DO_SRL)
GEN_PSIMD_SHIFTOP(psrl_bs, target_ulong, uint8_t, uint8_t,
                  EXTRACT8, INSERT8, ELEMS_B, 0x07, PSIMD_DO_SRL)
GEN_PSIMD_SHIFTOP(psrli_h, target_ulong, uint16_t, uint16_t,
                  EXTRACT16, INSERT16, ELEMS_H, 0x0f, PSIMD_DO_SRL)
GEN_PSIMD_SHIFTOP(psrl_hs, target_ulong, uint16_t, uint16_t,
                  EXTRACT16, INSERT16, ELEMS_H, 0x0f, PSIMD_DO_SRL)
GEN_PSIMD_SHIFTOP(psrli_w, uint64_t, uint32_t, uint32_t,
                  EXTRACT32, INSERT32, ELEMS_W, 0x1f, PSIMD_DO_SRL)
GEN_PSIMD_SHIFTOP(psrl_ws, uint64_t, uint32_t, uint32_t,
                  EXTRACT32, INSERT32, ELEMS_W, 0x1f, PSIMD_DO_SRL)

GEN_PSIMD_SHIFTOP(psrai_b, target_ulong, int8_t, uint8_t,
                  EXTRACT8, INSERT8, ELEMS_B, 0x07, PSIMD_DO_SRA)
GEN_PSIMD_SHIFTOP(psra_bs, target_ulong, int8_t, uint8_t,
                  EXTRACT8, INSERT8, ELEMS_B, 0x07, PSIMD_DO_SRA)
GEN_PSIMD_SHIFTOP(psrai_h, target_ulong, int16_t, uint16_t,
                  EXTRACT16, INSERT16, ELEMS_H, 0x0f, PSIMD_DO_SRA)
GEN_PSIMD_SHIFTOP(psra_hs, target_ulong, int16_t, uint16_t,
                  EXTRACT16, INSERT16, ELEMS_H, 0x0f, PSIMD_DO_SRA)
GEN_PSIMD_SHIFTOP(psrai_w, uint64_t, int32_t, uint32_t,
                  EXTRACT32, INSERT32, ELEMS_W, 0x1f, PSIMD_DO_SRA)
GEN_PSIMD_SHIFTOP(psra_ws, uint64_t, int32_t, uint32_t,
                  EXTRACT32, INSERT32, ELEMS_W, 0x1f, PSIMD_DO_SRA)

/* Saturating shift operations */

GEN_PSIMD_SAT_SHIFTOP(psslai_h, target_ulong, int16_t, int32_t,
                      EXTRACT16, INSERT16, ELEMS_H, 0x0f,
                      signed_saturate_h)
GEN_PSIMD_SAT_SHIFTOP(psslai_w, uint64_t, int32_t, int64_t,
                      EXTRACT32, INSERT32, ELEMS_W, 0x1f,
                      signed_saturate_w)

GEN_PSIMD_SAT_SHIFTOP(sslai, uint32_t, int32_t, int64_t,
                      EXTRACT32, INSERT32, ELEMS_W, 0x1f,
                      signed_saturate_w)

/* Rounding shift operations */

GEN_PSIMD_ROUND_SRAI(psrari_h, target_ulong, int16_t, int32_t,
                     EXTRACT16, INSERT16, ELEMS_H, 0x0f)
GEN_PSIMD_ROUND_SRAI(psrari_w, uint64_t, int32_t, int64_t,
                     EXTRACT32, INSERT32, ELEMS_W, 0x1f)

GEN_PSIMD_ROUND_SRAI(srari_32, uint32_t, int32_t, int64_t,
                     EXTRACT32, INSERT32, ELEMS_W, 0x1f)

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

GEN_PSIMD_VAR_SSHA(pssha_hs, target_ulong, int16_t, int32_t,
                   EXTRACT16, INSERT16, ELEMS_H, 16, signed_saturate_h)
GEN_PSIMD_VAR_SSHA(pssha_ws, uint64_t, int32_t, int64_t,
                   EXTRACT32, INSERT32, ELEMS_W, 32, signed_saturate_w)

GEN_PSIMD_VAR_SSHAR(psshar_hs, target_ulong, int16_t, int32_t,
                    EXTRACT16, INSERT16, ELEMS_H, 16, SAT_MIN_H, SAT_MAX_H,
                    signed_saturate_h)
GEN_PSIMD_VAR_SSHAR(psshar_ws, uint64_t, int32_t, int64_t,
                    EXTRACT32, INSERT32, ELEMS_W, 32, SAT_MIN_W, SAT_MAX_W,
                    signed_saturate_w)

GEN_PSIMD_VAR_SSHA(ssha, uint32_t, int32_t, int64_t,
                   EXTRACT32, INSERT32, ELEMS_W, 32, signed_saturate_w)

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

GEN_PSIMD_VAR_SRA64(sha, PSIMD_DO_SRA64)
GEN_PSIMD_VAR_SRA64(shar, PSIMD_DO_RNDSRA64)

GEN_PSIMD_VAR_USHL(psshl_hs, target_ulong, uint16_t, uint32_t,
                   EXTRACT16, INSERT16, ELEMS_H, 16, unsigned_saturate_h)

GEN_PSIMD_VAR_USHLR(psshlr_hs, target_ulong, uint16_t, uint32_t,
                    EXTRACT16, INSERT16, ELEMS_H, 16, unsigned_saturate_h)

GEN_PSIMD_VAR_USHL(psshl_ws, uint64_t, uint32_t, uint64_t,
                   EXTRACT32, INSERT32, ELEMS_W, 32, unsigned_saturate_w)

GEN_PSIMD_VAR_USHLR(psshlr_ws, uint64_t, uint32_t, uint64_t,
                    EXTRACT32, INSERT32, ELEMS_W, 32, unsigned_saturate_w)

GEN_PSIMD_VAR_USHL(sshl, uint32_t, uint32_t, uint64_t,
                   EXTRACT32, INSERT32, ELEMS_W, 32, unsigned_saturate_w)

GEN_PSIMD_VAR_USHLR(sshlr, uint32_t, uint32_t, uint64_t,
                    EXTRACT32, INSERT32, ELEMS_W, 32, unsigned_saturate_w)

GEN_PSIMD_VAR_SRL64(shl, PSIMD_DO_SRL64)
GEN_PSIMD_VAR_SRL64(shlr, PSIMD_DO_RNDSRL64)

/* Exchange operations (AS/SA/AS/SA with X suffix) */

GEN_PSIMD_XPAIR_BINOP(pas_hx, target_ulong, int16_t,
                      EXTRACT16, INSERT16, ELEMS_H,
                      PSIMD_DO_SUB, PSIMD_DO_ADD)
GEN_PSIMD_XPAIR_BINOP(psa_hx, target_ulong, int16_t,
                      EXTRACT16, INSERT16, ELEMS_H,
                      PSIMD_DO_ADD, PSIMD_DO_SUB)

GEN_PSIMD_XPAIR_SAT_BINOP(psas_hx, target_ulong, int16_t, int32_t,
                          EXTRACT16, INSERT16, ELEMS_H,
                          PSIMD_DO_SUB, PSIMD_DO_ADD, signed_saturate_h)
GEN_PSIMD_XPAIR_SAT_BINOP(pssa_hx, target_ulong, int16_t, int32_t,
                          EXTRACT16, INSERT16, ELEMS_H,
                          PSIMD_DO_ADD, PSIMD_DO_SUB, signed_saturate_h)

GEN_PSIMD_XPAIR_AVG_BINOP(paas_hx, target_ulong, int16_t, int32_t,
                          EXTRACT16, INSERT16, ELEMS_H,
                          PSIMD_DO_SUB, PSIMD_DO_ADD)
GEN_PSIMD_XPAIR_AVG_BINOP(pasa_hx, target_ulong, int16_t, int32_t,
                          EXTRACT16, INSERT16, ELEMS_H,
                          PSIMD_DO_ADD, PSIMD_DO_SUB)

GEN_PSIMD_XPAIR_BINOP(pas_wx, uint64_t, int32_t,
                      EXTRACT32, INSERT32, ELEMS_W,
                      PSIMD_DO_SUB, PSIMD_DO_ADD)
GEN_PSIMD_XPAIR_BINOP(psa_wx, uint64_t, int32_t,
                      EXTRACT32, INSERT32, ELEMS_W,
                      PSIMD_DO_ADD, PSIMD_DO_SUB)

GEN_PSIMD_XPAIR_SAT_BINOP(psas_wx, uint64_t, int32_t, int64_t,
                          EXTRACT32, INSERT32, ELEMS_W,
                          PSIMD_DO_SUB, PSIMD_DO_ADD, signed_saturate_w)
GEN_PSIMD_XPAIR_SAT_BINOP(pssa_wx, uint64_t, int32_t, int64_t,
                          EXTRACT32, INSERT32, ELEMS_W,
                          PSIMD_DO_ADD, PSIMD_DO_SUB, signed_saturate_w)

GEN_PSIMD_XPAIR_AVG_BINOP(paas_wx, uint64_t, int32_t, int64_t,
                          EXTRACT32, INSERT32, ELEMS_W,
                          PSIMD_DO_SUB, PSIMD_DO_ADD)
GEN_PSIMD_XPAIR_AVG_BINOP(pasa_wx, uint64_t, int32_t, int64_t,
                          EXTRACT32, INSERT32, ELEMS_W,
                          PSIMD_DO_ADD, PSIMD_DO_SUB)

/* Horizontal sum operations */

GEN_PSIMD_REDSUM(predsum_bs, target_ulong, int64_t, int8_t,
                 EXTRACT8, ELEMS_B, (int64_t)(int32_t)rs2)
GEN_PSIMD_REDSUM(predsumu_bs, target_ulong, uint64_t, uint8_t,
                 EXTRACT8, ELEMS_B, rs2)
GEN_PSIMD_REDSUM(predsum_hs, target_ulong, int64_t, int16_t,
                 EXTRACT16, ELEMS_H, (int64_t)(int32_t)rs2)
GEN_PSIMD_REDSUM(predsumu_hs, target_ulong, uint64_t, uint16_t,
                 EXTRACT16, ELEMS_H, rs2)
GEN_PSIMD_REDSUM(predsum_ws, uint64_t, int64_t, int32_t,
                 EXTRACT32, ELEMS_W, (int64_t)rs2)
GEN_PSIMD_REDSUM(predsumu_ws, uint64_t, uint64_t, uint32_t,
                 EXTRACT32, ELEMS_W, rs2)

/* Packing/unpacking operations */

GEN_PSIMD_PAIR_PACK(ppaire_b, target_ulong, uint16_t,
                    EXTRACT16, INSERT16, ELEMS_H, 0x00ff, 8,
                    PSIMD_PAIR_LO, PSIMD_PAIR_LO)
GEN_PSIMD_PAIR_PACK(ppaireo_b, target_ulong, uint16_t,
                    EXTRACT16, INSERT16, ELEMS_H, 0x00ff, 8,
                    PSIMD_PAIR_HI, PSIMD_PAIR_LO)
GEN_PSIMD_PAIR_PACK(ppairoe_b, target_ulong, uint16_t,
                    EXTRACT16, INSERT16, ELEMS_H, 0x00ff, 8,
                    PSIMD_PAIR_LO, PSIMD_PAIR_HI)
GEN_PSIMD_PAIR_PACK(ppairo_b, target_ulong, uint16_t,
                    EXTRACT16, INSERT16, ELEMS_H, 0x00ff, 8,
                    PSIMD_PAIR_HI, PSIMD_PAIR_HI)

GEN_PSIMD_PAIR_PACK(ppaire_h, uint64_t, uint32_t,
                    EXTRACT32, INSERT32, ELEMS_W, 0x0000ffff, 16,
                    PSIMD_PAIR_LO, PSIMD_PAIR_LO)
GEN_PSIMD_PAIR_PACK(ppaireo_h, target_ulong, uint32_t,
                    EXTRACT32, INSERT32, ELEMS_W, 0x0000ffff, 16,
                    PSIMD_PAIR_HI, PSIMD_PAIR_LO)
GEN_PSIMD_PAIR_PACK(ppairoe_h, target_ulong, uint32_t,
                    EXTRACT32, INSERT32, ELEMS_W, 0x0000ffff, 16,
                    PSIMD_PAIR_LO, PSIMD_PAIR_HI)
GEN_PSIMD_PAIR_PACK(ppairo_h, target_ulong, uint32_t,
                    EXTRACT32, INSERT32, ELEMS_W, 0x0000ffff, 16,
                    PSIMD_PAIR_HI, PSIMD_PAIR_HI)

GEN_PSIMD_PAIR_WORD(ppaireo_w, 0, 1)
GEN_PSIMD_PAIR_WORD(ppairoe_w, 1, 0)
GEN_PSIMD_PAIR_WORD(ppairo_w, 1, 1)

GEN_PSIMD_SIGN_EXTEND(psext_h_b, target_ulong, int8_t, int16_t,
                      EXTRACT8, INSERT16, ELEMS_H, 2)
GEN_PSIMD_SIGN_EXTEND(psext_w_b, uint64_t, int8_t, int32_t,
                      EXTRACT8, INSERT32, ELEMS_W, 4)
GEN_PSIMD_SIGN_EXTEND(psext_w_h, uint64_t, int16_t, int32_t,
                      EXTRACT16, INSERT32, ELEMS_W, 2)

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

GEN_PSIMD_ZIP(zip8p, uint8_t, EXTRACT8, 4, 8, 3)
GEN_PSIMD_ZIP(zip8hp, uint8_t, EXTRACT8, 4, 8, 7)
GEN_PSIMD_UNZIP(unzip8p, EXTRACT8, 4, 8, 0)
GEN_PSIMD_UNZIP(unzip8hp, EXTRACT8, 4, 8, 1)

GEN_PSIMD_ZIP(zip16p, uint16_t, EXTRACT16, 2, 16, 1)
GEN_PSIMD_ZIP(zip16hp, uint16_t, EXTRACT16, 2, 16, 3)
GEN_PSIMD_UNZIP(unzip16p, EXTRACT16, 2, 16, 0)
GEN_PSIMD_UNZIP(unzip16hp, EXTRACT16, 2, 16, 1)

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

GEN_PSIMD_BIT_SELECT(mvm, rs2, rs1, rd)
GEN_PSIMD_BIT_SELECT(mvmn, rs2, rd, rs1)
GEN_PSIMD_BIT_SELECT(merge, rd, rs2, rs1)

GEN_PSIMD_NCLIP_PACK(pnclipp_b, int16_t, int8_t,
                     EXTRACT16, INSERT8, 4, signed_saturate_b)
GEN_PSIMD_NCLIP_PACK(pnclipup_b, uint16_t, uint8_t,
                     EXTRACT16, INSERT8, 4, unsigned_saturate_b)
GEN_PSIMD_NCLIP_PACK(pnclipp_h, int32_t, int16_t,
                     EXTRACT32, INSERT16, 2, signed_saturate_h)
GEN_PSIMD_NCLIP_PACK(pnclipup_h, uint32_t, uint16_t,
                     EXTRACT32, INSERT16, 2, unsigned_saturate_h)
GEN_PSIMD_NCLIP_PACK(pnclipp_w, int64_t, int32_t,
                     EXTRACT64, INSERT32_64, 1, signed_saturate_w)
GEN_PSIMD_NCLIP_PACK(pnclipup_w, uint64_t, uint32_t,
                     EXTRACT64, INSERT32_64, 1, unsigned_saturate_w)

/* Count leading operations */

#if TARGET_LONG_BITS == 64
GEN_PSIMD_CLS(cls, target_ulong, uint64_t, clrsb64)
#else
GEN_PSIMD_CLS(cls, target_ulong, uint32_t, clrsb32)
#endif

GEN_PSIMD_CLS(clsw, uint64_t, uint32_t, clrsb32)

