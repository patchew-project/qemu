/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch LSX helper functions.
 *
 * Copyright (c) 2022-2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "internals.h"

void helper_vadd_q(CPULoongArchState *env,
                   uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_add(Vj->Q(0), Vk->Q(0));
}

void helper_vsub_q(CPULoongArchState *env,
                   uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_sub(Vj->Q(0), Vk->Q(0));
}

#define DO_ADD(a, b)  (a + b)
#define DO_SUB(a, b)  (a - b)

#define DO_ODD_EVEN_S(NAME, BIT, T, E1, E2, DO_OP)                 \
void HELPER(NAME)(CPULoongArchState *env,                          \
                  uint32_t vd, uint32_t vj, uint32_t vk)           \
{                                                                  \
    int i;                                                         \
    VReg *Vd = &(env->fpr[vd].vreg);                               \
    VReg *Vj = &(env->fpr[vj].vreg);                               \
    VReg *Vk = &(env->fpr[vk].vreg);                               \
                                                                   \
    for (i = 0; i < LSX_LEN/BIT; i++) {                            \
        Vd->E1(i) = DO_OP((T)Vj->E2(2 * i + 1), (T)Vk->E2(2 * i)); \
    }                                                              \
}

DO_ODD_EVEN_S(vhaddw_h_b, 16, int16_t, H, B, DO_ADD)
DO_ODD_EVEN_S(vhaddw_w_h, 32, int32_t, W, H, DO_ADD)
DO_ODD_EVEN_S(vhaddw_d_w, 64, int64_t, D, W, DO_ADD)

void HELPER(vhaddw_q_d)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_add(int128_makes64(Vj->D(1)), int128_makes64(Vk->D(0)));
}

DO_ODD_EVEN_S(vhsubw_h_b, 16, int16_t, H, B, DO_SUB)
DO_ODD_EVEN_S(vhsubw_w_h, 32, int32_t, W, H, DO_SUB)
DO_ODD_EVEN_S(vhsubw_d_w, 64, int64_t, D, W, DO_SUB)

void HELPER(vhsubw_q_d)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_sub(int128_makes64(Vj->D(1)), int128_makes64(Vk->D(0)));
}

#define DO_ODD_EVEN_U(NAME, BIT, TD, TS,  E1, E2, DO_OP)                     \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t vd, uint32_t vj, uint32_t vk)                     \
{                                                                            \
    int i;                                                                   \
    VReg *Vd = &(env->fpr[vd].vreg);                                         \
    VReg *Vj = &(env->fpr[vj].vreg);                                         \
    VReg *Vk = &(env->fpr[vk].vreg);                                         \
                                                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                      \
        Vd->E1(i) = DO_OP((TD)(TS)Vj->E2(2 * i + 1), (TD)(TS)Vk->E2(2 * i)); \
    }                                                                        \
}

DO_ODD_EVEN_U(vhaddw_hu_bu, 16, uint16_t, uint8_t, H, B, DO_ADD)
DO_ODD_EVEN_U(vhaddw_wu_hu, 32, uint32_t, uint16_t, W, H, DO_ADD)
DO_ODD_EVEN_U(vhaddw_du_wu, 64, uint64_t, uint32_t, D, W, DO_ADD)

void HELPER(vhaddw_qu_du)(CPULoongArchState *env,
                          uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(1)),
                          int128_make64((uint64_t)Vk->D(0)));
}

DO_ODD_EVEN_U(vhsubw_hu_bu, 16, uint16_t, uint8_t, H, B, DO_SUB)
DO_ODD_EVEN_U(vhsubw_wu_hu, 32, uint32_t, uint16_t, W, H, DO_SUB)
DO_ODD_EVEN_U(vhsubw_du_wu, 64, uint64_t, uint32_t, D, W, DO_SUB)

void HELPER(vhsubw_qu_du)(CPULoongArchState *env,
                          uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_sub(int128_make64((uint64_t)Vj->D(1)),
                          int128_make64((uint64_t)Vk->D(0)));
}

#define DO_EVEN_S(NAME, BIT, T, E1, E2, DO_OP)                 \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)    \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    for (i = 0; i < LSX_LEN/BIT; i++) {                        \
        Vd->E1(i) = DO_OP((T)Vj->E2(2 * i) ,(T)Vk->E2(2 * i)); \
    }                                                          \
}

#define DO_ODD_S(NAME, BIT, T, E1, E2, DO_OP)                          \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)            \
{                                                                      \
    int i;                                                             \
    VReg *Vd = (VReg *)vd;                                             \
    VReg *Vj = (VReg *)vj;                                             \
    VReg *Vk = (VReg *)vk;                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                \
        Vd->E1(i) = DO_OP((T)Vj->E2(2 * i + 1), (T)Vk->E2(2 * i + 1)); \
    }                                                                  \
}

void HELPER(vaddwev_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_makes64(Vj->D(0)), int128_makes64(Vk->D(0)));
}

DO_EVEN_S(vaddwev_h_b, 16, int16_t, H, B, DO_ADD)
DO_EVEN_S(vaddwev_w_h, 32, int32_t, W, H, DO_ADD)
DO_EVEN_S(vaddwev_d_w, 64, int64_t, D, W, DO_ADD)

void HELPER(vaddwod_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_makes64(Vj->D(1)), int128_makes64(Vk->D(1)));
}

DO_ODD_S(vaddwod_h_b, 16, int16_t, H, B, DO_ADD)
DO_ODD_S(vaddwod_w_h, 32, int32_t, W, H, DO_ADD)
DO_ODD_S(vaddwod_d_w, 64, int64_t, D, W, DO_ADD)

void HELPER(vsubwev_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_sub(int128_makes64(Vj->D(0)), int128_makes64(Vk->D(0)));
}

DO_EVEN_S(vsubwev_h_b, 16, int16_t, H, B, DO_SUB)
DO_EVEN_S(vsubwev_w_h, 32, int32_t, W, H, DO_SUB)
DO_EVEN_S(vsubwev_d_w, 64, int64_t, D, W, DO_SUB)

void HELPER(vsubwod_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_sub(int128_makes64(Vj->D(1)), int128_makes64(Vk->D(1)));
}

DO_ODD_S(vsubwod_h_b, 16, int16_t, H, B, DO_SUB)
DO_ODD_S(vsubwod_w_h, 32, int32_t, W, H, DO_SUB)
DO_ODD_S(vsubwod_d_w, 64, int64_t, D, W, DO_SUB)

#define DO_EVEN_U(NAME, BIT, TD, TS, E1, E2, DO_OP)         \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E1(i) = DO_OP((TD)(TS)Vj->E2(2 * i),            \
                          (TD)(TS)Vk->E2(2 * i));           \
    }                                                       \
}

#define DO_ODD_U(NAME, BIT, TD, TS, E1, E2, DO_OP)          \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E1(i) = DO_OP((TD)(TS)Vj->E2(2 * i + 1),        \
                          (TD)(TS)Vk->E2(2 * i + 1));       \
    }                                                       \
}

void HELPER(vaddwev_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(0)),
                          int128_make64((uint64_t)Vk->D(0)));
}

DO_EVEN_U(vaddwev_h_bu, 16, uint16_t, uint8_t, H, B, DO_ADD)
DO_EVEN_U(vaddwev_w_hu, 32, uint32_t, uint16_t, W, H, DO_ADD)
DO_EVEN_U(vaddwev_d_wu, 64, uint64_t, uint32_t, D, W, DO_ADD)

void HELPER(vaddwod_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(1)),
                          int128_make64((uint64_t)Vk->D(1)));
}

DO_ODD_U(vaddwod_h_bu, 16, uint16_t, uint8_t, H, B, DO_ADD)
DO_ODD_U(vaddwod_w_hu, 32, uint32_t, uint16_t, W, H, DO_ADD)
DO_ODD_U(vaddwod_d_wu, 64, uint64_t, uint32_t, D, W, DO_ADD)

void HELPER(vsubwev_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_sub(int128_make64((uint64_t)Vj->D(0)),
                          int128_make64((uint64_t)Vk->D(0)));
}

DO_EVEN_U(vsubwev_h_bu, 16, uint16_t, uint8_t, H, B, DO_SUB)
DO_EVEN_U(vsubwev_w_hu, 32, uint32_t, uint16_t, W, H, DO_SUB)
DO_EVEN_U(vsubwev_d_wu, 64, uint64_t, uint32_t, D, W, DO_SUB)

void HELPER(vsubwod_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_sub(int128_make64((uint64_t)Vj->D(1)),
                          int128_make64((uint64_t)Vk->D(1)));
}

DO_ODD_U(vsubwod_h_bu, 16, uint16_t, uint8_t, H, B, DO_SUB)
DO_ODD_U(vsubwod_w_hu, 32, uint32_t, uint16_t, W, H, DO_SUB)
DO_ODD_U(vsubwod_d_wu, 64, uint64_t, uint32_t, D, W, DO_SUB)

#define DO_EVEN_U_S(NAME, BIT, T1, TD1, T2, E1, E2, DO_OP)            \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)           \
{                                                                     \
    int i;                                                            \
    VReg *Vd = (VReg *)vd;                                            \
    VReg *Vj = (VReg *)vj;                                            \
    VReg *Vk = (VReg *)vk;                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                               \
        Vd->E1(i) = DO_OP((TD1)(T1)Vj->E2(2 * i) ,(T2)Vk->E2(2 * i)); \
    }                                                                 \
}

#define DO_ODD_U_S(NAME, BIT, T1, TD1, T2, E1, E2, DO_OP)                     \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)                   \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = (VReg *)vd;                                                    \
    VReg *Vj = (VReg *)vj;                                                    \
    VReg *Vk = (VReg *)vk;                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        Vd->E1(i) = DO_OP((TD1)(T1)Vj->E2(2 * i + 1), (T2)Vk->E2(2 * i + 1)); \
    }                                                                         \
}

void HELPER(vaddwev_q_du_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(0)),
                          int128_makes64(Vk->D(0)));
}

DO_EVEN_U_S(vaddwev_h_bu_b, 16, uint16_t, uint8_t, int16_t, H, B, DO_ADD)
DO_EVEN_U_S(vaddwev_w_hu_h, 32, uint32_t, uint16_t, int32_t, W, H, DO_ADD)
DO_EVEN_U_S(vaddwev_d_wu_w, 64, uint64_t, uint32_t, int64_t, D, W, DO_ADD)

void HELPER(vaddwod_q_du_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(1)),
                          int128_makes64(Vk->D(1)));
}

DO_ODD_U_S(vaddwod_h_bu_b, 16, uint16_t, uint8_t, int16_t, H, B, DO_ADD)
DO_ODD_U_S(vaddwod_w_hu_h, 32, uint32_t, uint16_t, int32_t, W, H, DO_ADD)
DO_ODD_U_S(vaddwod_d_wu_w, 64, uint64_t, uint32_t, int64_t, D, W, DO_ADD)

#define DO_VAVG(a, b)  ((a >> 1) + (b >> 1) + (a & b & 1))
#define DO_VAVGR(a, b) ((a >> 1) + (b >> 1) + ((a | b) & 1))

#define DO_VAVG_S(NAME, BIT, E, DO_OP)                      \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vj->E(i), Vk->E(i));               \
    }                                                       \
}

DO_VAVG_S(vavg_b, 8, B, DO_VAVG)
DO_VAVG_S(vavg_h, 16, H, DO_VAVG)
DO_VAVG_S(vavg_w, 32, W, DO_VAVG)
DO_VAVG_S(vavg_d, 64, D, DO_VAVG)
DO_VAVG_S(vavgr_b, 8, B, DO_VAVGR)
DO_VAVG_S(vavgr_h, 16, H, DO_VAVGR)
DO_VAVG_S(vavgr_w, 32, W, DO_VAVGR)
DO_VAVG_S(vavgr_d, 64, D, DO_VAVGR)

#define DO_VAVG_U(NAME, BIT, T, E, DO_OP)                   \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP((T)Vj->E(i), (T)Vk->E(i));         \
    }                                                       \
}

DO_VAVG_U(vavg_bu, 8, uint8_t, B, DO_VAVG)
DO_VAVG_U(vavg_hu, 16, uint16_t, H, DO_VAVG)
DO_VAVG_U(vavg_wu, 32, uint32_t, W, DO_VAVG)
DO_VAVG_U(vavg_du, 64, uint64_t, D, DO_VAVG)
DO_VAVG_U(vavgr_bu, 8, uint8_t, B, DO_VAVGR)
DO_VAVG_U(vavgr_hu, 16, uint16_t, H, DO_VAVGR)
DO_VAVG_U(vavgr_wu, 32, uint32_t, W, DO_VAVGR)
DO_VAVG_U(vavgr_du, 64, uint64_t, D, DO_VAVGR)

#define DO_VABSD(a, b)  ((a > b) ? (a -b) : (b-a))

#define DO_VABSD_S(NAME, BIT, E, DO_OP)                     \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vj->E(i), Vk->E(i));               \
    }                                                       \
}

DO_VABSD_S(vabsd_b, 8, B, DO_VABSD)
DO_VABSD_S(vabsd_h, 16, H, DO_VABSD)
DO_VABSD_S(vabsd_w, 32, W, DO_VABSD)
DO_VABSD_S(vabsd_d, 64, D, DO_VABSD)

#define DO_VABSD_U(NAME, BIT, T, E, DO_OP)                  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP((T)Vj->E(i), (T)Vk->E(i));         \
    }                                                       \
}

DO_VABSD_U(vabsd_bu, 8, uint8_t, B, DO_VABSD)
DO_VABSD_U(vabsd_hu, 16, uint16_t, H, DO_VABSD)
DO_VABSD_U(vabsd_wu, 32, uint32_t, W, DO_VABSD)
DO_VABSD_U(vabsd_du, 64, uint64_t, D, DO_VABSD)

#define DO_VABS(a)  ((a < 0) ? (-a) : (a))

#define DO_VADDA(NAME, BIT, E, DO_OP)                       \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vj->E(i)) + DO_OP(Vk->E(i));       \
    }                                                       \
}

DO_VADDA(vadda_b, 8, B, DO_VABS)
DO_VADDA(vadda_h, 16, H, DO_VABS)
DO_VADDA(vadda_w, 32, W, DO_VABS)
DO_VADDA(vadda_d, 64, D, DO_VABS)

#define DO_MIN(a, b) (a < b ? a : b)
#define DO_MAX(a, b) (a > b ? a : b)

#define DO_VMINMAXI_S(NAME, BIT, T, E, DO_OP)                   \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t v) \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = DO_OP(Vj->E(i), (T)imm);                     \
    }                                                           \
}

DO_VMINMAXI_S(vmini_b, 8, int8_t, B, DO_MIN)
DO_VMINMAXI_S(vmini_h, 16, int16_t, H, DO_MIN)
DO_VMINMAXI_S(vmini_w, 32, int32_t, W, DO_MIN)
DO_VMINMAXI_S(vmini_d, 64, int64_t, D, DO_MIN)
DO_VMINMAXI_S(vmaxi_b, 8, int8_t, B, DO_MAX)
DO_VMINMAXI_S(vmaxi_h, 16, int16_t, H, DO_MAX)
DO_VMINMAXI_S(vmaxi_w, 32, int32_t, W, DO_MAX)
DO_VMINMAXI_S(vmaxi_d, 64, int64_t, D, DO_MAX)

#define DO_VMINMAXI_U(NAME, BIT, T, E, DO_OP)                   \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t v) \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = DO_OP((T)Vj->E(i), (T)imm);                  \
    }                                                           \
}

DO_VMINMAXI_U(vmini_bu, 8, uint8_t, B, DO_MIN)
DO_VMINMAXI_U(vmini_hu, 16, uint16_t, H, DO_MIN)
DO_VMINMAXI_U(vmini_wu, 32, uint32_t, W, DO_MIN)
DO_VMINMAXI_U(vmini_du, 64, uint64_t, D, DO_MIN)
DO_VMINMAXI_U(vmaxi_bu, 8, uint8_t, B, DO_MAX)
DO_VMINMAXI_U(vmaxi_hu, 16, uint16_t, H, DO_MAX)
DO_VMINMAXI_U(vmaxi_wu, 32, uint32_t, W, DO_MAX)
DO_VMINMAXI_U(vmaxi_du, 64, uint64_t, D, DO_MAX)

#define DO_VMUH_S(NAME, BIT, T, E, DO_OP)                   \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = ((T)Vj->E(i)) * ((T)Vk->E(i)) >> BIT;    \
    }                                                       \
}

void HELPER(vmuh_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    uint64_t l, h1, h2;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    muls64(&l, &h1, Vj->D(0), Vk->D(0));
    muls64(&l, &h2, Vj->D(1), Vk->D(1));

    Vd->D(0) = h1;
    Vd->D(1) = h2;
}

DO_VMUH_S(vmuh_b, 8, int16_t, B, DO_MUH)
DO_VMUH_S(vmuh_h, 16, int32_t, H, DO_MUH)
DO_VMUH_S(vmuh_w, 32, int64_t, W, DO_MUH)

#define DO_VMUH_U(NAME, BIT, T, T2, E, DO_OP)                   \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)     \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    VReg *Vk = (VReg *)vk;                                      \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = ((T)((T2)Vj->E(i)) * ((T2)Vk->E(i))) >> BIT; \
    }                                                           \
}

void HELPER(vmuh_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    uint64_t l, h1, h2;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    mulu64(&l, &h1, Vj->D(0), Vk->D(0));
    mulu64(&l, &h2, Vj->D(1), Vk->D(1));

    Vd->D(0) = h1;
    Vd->D(1) = h2;
}

DO_VMUH_U(vmuh_bu, 8, uint16_t, uint8_t, B, DO_MUH)
DO_VMUH_U(vmuh_hu, 16, uint32_t, uint16_t, H, DO_MUH)
DO_VMUH_U(vmuh_wu, 32, uint64_t, uint32_t, W, DO_MUH)

#define DO_MUL(a, b) (a * b)

void HELPER(vmulwev_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_makes64(Vj->D(0)) * int128_makes64(Vk->D(0));
}

DO_EVEN_S(vmulwev_h_b, 16, int16_t, H, B, DO_MUL)
DO_EVEN_S(vmulwev_w_h, 32, int32_t, W, H, DO_MUL)
DO_EVEN_S(vmulwev_d_w, 64, int64_t, D, W, DO_MUL)

void HELPER(vmulwod_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_makes64(Vj->D(1)) * int128_makes64(Vk->D(1));
}

DO_ODD_S(vmulwod_h_b, 16, int16_t, H, B, DO_MUL)
DO_ODD_S(vmulwod_w_h, 32, int32_t, W, H, DO_MUL)
DO_ODD_S(vmulwod_d_w, 64, int64_t, D, W, DO_MUL)

void HELPER(vmulwev_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_make64(Vj->D(0)) * int128_make64(Vk->D(0));
}

DO_EVEN_U(vmulwev_h_bu, 16, uint16_t, uint8_t, H, B, DO_MUL)
DO_EVEN_U(vmulwev_w_hu, 32, uint32_t, uint16_t, W, H, DO_MUL)
DO_EVEN_U(vmulwev_d_wu, 64, uint64_t, uint32_t, D, W, DO_MUL)

void HELPER(vmulwod_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_make64(Vj->D(1)) * int128_make64(Vk->D(1));
}

DO_ODD_U(vmulwod_h_bu, 16, uint16_t, uint8_t, H, B, DO_MUL)
DO_ODD_U(vmulwod_w_hu, 32, uint32_t, uint16_t, W, H, DO_MUL)
DO_ODD_U(vmulwod_d_wu, 64, uint64_t, uint32_t, D, W, DO_MUL)

void HELPER(vmulwev_q_du_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_make64(Vj->D(0)) * int128_makes64(Vk->D(0));
}

DO_EVEN_U_S(vmulwev_h_bu_b, 16, uint16_t, uint8_t, int16_t, H, B, DO_MUL)
DO_EVEN_U_S(vmulwev_w_hu_h, 32, uint32_t, uint16_t, int32_t, W, H, DO_MUL)
DO_EVEN_U_S(vmulwev_d_wu_w, 64, uint64_t, uint32_t, int64_t, D, W, DO_MUL)

void HELPER(vmulwod_q_du_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_make64(Vj->D(1)) * int128_makes64(Vk->D(1));
}

DO_ODD_U_S(vmulwod_h_bu_b, 16, uint16_t, uint8_t, int16_t, H, B, DO_MUL)
DO_ODD_U_S(vmulwod_w_hu_h, 32, uint32_t, uint16_t, int32_t, W, H, DO_MUL)
DO_ODD_U_S(vmulwod_d_wu_w, 64, uint64_t, uint32_t, int64_t, D, W, DO_MUL)

#define DO_MADD(a, b, c)  (a + b * c)
#define DO_MSUB(a, b, c)  (a - b * c)

#define VMADDSUB(NAME, BIT, E, DO_OP)                       \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vd->E(i), Vj->E(i) ,Vk->E(i));     \
    }                                                       \
}

VMADDSUB(vmadd_b, 8, B, DO_MADD)
VMADDSUB(vmadd_h, 16, H, DO_MADD)
VMADDSUB(vmadd_w, 32, W, DO_MADD)
VMADDSUB(vmadd_d, 64, D, DO_MADD)
VMADDSUB(vmsub_b, 8, B, DO_MSUB)
VMADDSUB(vmsub_h, 16, H, DO_MSUB)
VMADDSUB(vmsub_w, 32, W, DO_MSUB)
VMADDSUB(vmsub_d, 64, D, DO_MSUB)

#define VMADD_Q(NAME, FN1, FN2, index)                            \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)       \
{                                                                 \
    VReg *Vd = (VReg *)vd;                                        \
    VReg *Vj = (VReg *)vj;                                        \
    VReg *Vk = (VReg *)vk;                                        \
                                                                  \
    Vd->Q(0) = int128_add(Vd->Q(0),                               \
                          FN1(Vj->D(index)) * FN2(Vk->D(index))); \
}

#define VMADDWEV(NAME, BIT, T1, T2, E1, E2, DO_OP)                        \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)               \
{                                                                         \
    int i;                                                                \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    VReg *Vk = (VReg *)vk;                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                   \
        Vd->E1(i) += DO_OP((T1)(T2)Vj->E2(2 * i), (T1)(T2)Vk->E2(2 * i)); \
    }                                                                     \
}

VMADDWEV(vmaddwev_h_b, 16, int16_t, int8_t, H, B, DO_MUL)
VMADDWEV(vmaddwev_w_h, 32, int32_t, int16_t, W, H, DO_MUL)
VMADDWEV(vmaddwev_d_w, 64, int64_t, int32_t, D, W, DO_MUL)
VMADD_Q(vmaddwev_q_d, int128_makes64, int128_makes64, 0)
VMADDWEV(vmaddwev_h_bu, 16, uint16_t, uint8_t, H, B, DO_MUL)
VMADDWEV(vmaddwev_w_hu, 32, uint32_t, uint16_t, W, H, DO_MUL)
VMADDWEV(vmaddwev_d_wu, 64, uint64_t, uint32_t, D, W, DO_MUL)
VMADD_Q(vmaddwev_q_du, int128_make64, int128_make64, 0)

#define VMADDWOD(NAME, BIT, T1, T2, E1, E2, DO_OP)          \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E1(i) += DO_OP((T1)(T2)Vj->E2(2 * i + 1),       \
                           (T1)(T2)Vk->E2(2 * i + 1));      \
    }                                                       \
}

VMADDWOD(vmaddwod_h_b, 16, int16_t, int8_t,  H, B, DO_MUL)
VMADDWOD(vmaddwod_w_h, 32, int32_t, int16_t, W, H, DO_MUL)
VMADDWOD(vmaddwod_d_w, 64, int64_t, int32_t, D, W, DO_MUL)
VMADD_Q(vmaddwod_q_d, int128_makes64, int128_makes64, 1)
VMADDWOD(vmaddwod_h_bu, 16, uint16_t, uint8_t, H, B, DO_MUL)
VMADDWOD(vmaddwod_w_hu, 32, uint32_t, uint16_t, W, H, DO_MUL)
VMADDWOD(vmaddwod_d_wu, 64, uint64_t, uint32_t, D, W, DO_MUL)
VMADD_Q(vmaddwod_q_du, int128_make64, int128_make64, 1)

#define VMADDWEV_U_S(NAME, BIT, T1, T2, TS, E1, E2, DO_OP)  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E1(i) += DO_OP((T1)(T2)Vj->E2(2 * i),           \
                           (TS)Vk->E2(2 * i));              \
    }                                                       \
}

VMADDWEV_U_S(vmaddwev_h_bu_b, 16, uint16_t, uint8_t, int16_t, H, B, DO_MUL)
VMADDWEV_U_S(vmaddwev_w_hu_h, 32, uint32_t, uint16_t, int32_t, W, H, DO_MUL)
VMADDWEV_U_S(vmaddwev_d_wu_w, 64, uint64_t, uint32_t, int64_t, D, W, DO_MUL)
VMADD_Q(vmaddwev_q_du_d, int128_make64, int128_makes64, 0)

#define VMADDWOD_U_S(NAME, BIT, T1, T2, TS, E1, E2, DO_OP)  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E1(i) += DO_OP((T1)(T2)Vj->E2(2 * i + 1),       \
                           (TS)Vk->E2(2 * i + 1));          \
    }                                                       \
}

VMADDWOD_U_S(vmaddwod_h_bu_b, 16, uint16_t, uint8_t, int16_t, H, B, DO_MUL)
VMADDWOD_U_S(vmaddwod_w_hu_h, 32, uint32_t, uint16_t, int32_t, W, H, DO_MUL)
VMADDWOD_U_S(vmaddwod_d_wu_w, 64, uint64_t, uint32_t, int64_t, D, W, DO_MUL)
VMADD_Q(vmaddwod_q_du_d, int128_make64, int128_makes64, 1)

#define DO_DIVU(N, M) (unlikely(M == 0) ? 0 : N / M)
#define DO_REMU(N, M) (unlikely(M == 0) ? 0 : N % M)
#define DO_DIV(N, M)  (unlikely(M == 0) ? 0 :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? N : N / M)
#define DO_REM(N, M)  (unlikely(M == 0) ? 0 :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? 0 : N % M)

#define DO_3OP(NAME, BIT, T, E, DO_OP)                   \
void HELPER(NAME)(CPULoongArchState *env,                \
                  uint32_t vd, uint32_t vj, uint32_t vk) \
{                                                        \
    int i;                                               \
    VReg *Vd = &(env->fpr[vd].vreg);                     \
    VReg *Vj = &(env->fpr[vj].vreg);                     \
    VReg *Vk = &(env->fpr[vk].vreg);                     \
                                                         \
    for (i = 0; i < LSX_LEN/BIT; i++) {                  \
        Vd->E(i) = DO_OP((T)Vj->E(i), (T)Vk->E(i));      \
    }                                                    \
}

DO_3OP(vdiv_b, 8, int8_t, B, DO_DIV)
DO_3OP(vdiv_h, 16, int16_t, H, DO_DIV)
DO_3OP(vdiv_w, 32, int32_t, W, DO_DIV)
DO_3OP(vdiv_d, 64, int64_t, D, DO_DIV)
DO_3OP(vdiv_bu, 8, uint8_t, B, DO_DIVU)
DO_3OP(vdiv_hu, 16, uint16_t, H, DO_DIVU)
DO_3OP(vdiv_wu, 32, uint32_t, W, DO_DIVU)
DO_3OP(vdiv_du, 64, uint64_t, D, DO_DIVU)
DO_3OP(vmod_b, 8, int8_t, B, DO_REM)
DO_3OP(vmod_h, 16, int16_t, H, DO_REM)
DO_3OP(vmod_w, 32, int32_t, W, DO_REM)
DO_3OP(vmod_d, 64, int64_t, D, DO_REM)
DO_3OP(vmod_bu, 8, uint8_t, B, DO_REMU)
DO_3OP(vmod_hu, 16, uint16_t, H, DO_REMU)
DO_3OP(vmod_wu, 32, uint32_t, W, DO_REMU)
DO_3OP(vmod_du, 64, uint64_t, D, DO_REMU)

#define do_vsats(E, T)                      \
static T do_vsats_ ## E(T s1, uint64_t imm) \
{                                           \
    T mask,top;                             \
                                            \
    mask = (1l << imm) - 1;                 \
    top = s1 >> imm;                        \
    if (top > 0) {                          \
        return mask;                        \
    } else if (top < -1) {                  \
        return ~mask;                       \
    } else {                                \
        return s1;                          \
    }                                       \
}

do_vsats(B, int8_t)
do_vsats(H, int16_t)
do_vsats(W, int32_t)
do_vsats(D, int64_t)

#define VSAT_S(NAME, BIT, E)                                    \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t v) \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = do_vsats_ ## E(Vj->E(i), imm);               \
    }                                                           \
}

VSAT_S(vsat_b, 8, B)
VSAT_S(vsat_h, 16, H)
VSAT_S(vsat_w, 32, W)
VSAT_S(vsat_d, 64, D)

#define do_vsatu(E, T)                                         \
static T do_vsatu_ ## E(T s1, uint64_t imm)                    \
{                                                              \
    uint64_t max;                                              \
                                                               \
    max = (imm == 0x3f) ? UINT64_MAX : (1ul << (imm + 1)) - 1; \
    if (s1 >(T)max) {                                          \
        return (T)max;                                         \
    } else {                                                   \
        return s1;                                             \
    }                                                          \
}

do_vsatu(B, uint8_t)
do_vsatu(H, uint16_t)
do_vsatu(W, uint32_t)
do_vsatu(D, uint64_t)

#define VSAT_U(NAME, BIT, T, E)                                 \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t v) \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = do_vsatu_ ## E((T)Vj->E(i), imm);            \
    }                                                           \
}

VSAT_U(vsat_bu, 8, uint8_t, B)
VSAT_U(vsat_hu, 16, uint16_t, H)
VSAT_U(vsat_wu, 32, uint32_t, W)
VSAT_U(vsat_du, 64, uint64_t, D)

#define VEXTH(NAME, BIT, T1, T2, E1, E2)                            \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E1(i) = (T2)(T1)Vj->E2(i + LSX_LEN/BIT);                \
    }                                                               \
}

void HELPER(vexth_q_d)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    Vd->Q(0) = int128_makes64(Vj->D(1));
}

void HELPER(vexth_qu_du)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    Vd->Q(0) = int128_make64((uint64_t)Vj->D(1));
}

VEXTH(vexth_h_b, 16, int16_t, int8_t, H, B)
VEXTH(vexth_w_h, 32, int32_t, int16_t, W, H)
VEXTH(vexth_d_w, 64, int64_t, int32_t, D, W)
VEXTH(vexth_hu_bu, 16, uint16_t, uint8_t, H, B)
VEXTH(vexth_wu_hu, 32, uint32_t, uint16_t, W, H)
VEXTH(vexth_du_wu, 64, uint64_t, uint32_t, D, W)

#define DO_SIGNCOV(a, b)  (a == 0 ? 0 : a < 0 ? -b : b)

#define VSIGNCOV(NAME, BIT, E, DO_OP)                       \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vj->E(i),  Vk->E(i));              \
    }                                                       \
}

VSIGNCOV(vsigncov_b, 8, B, DO_SIGNCOV)
VSIGNCOV(vsigncov_h, 16, H, DO_SIGNCOV)
VSIGNCOV(vsigncov_w, 32, W, DO_SIGNCOV)
VSIGNCOV(vsigncov_d, 64, D, DO_SIGNCOV)

static uint64_t do_vmskltz_b(int64_t val)
{
    uint64_t m = 0x8080808080808080ULL;
    uint64_t c =  val & m;
    c |= c << 7;
    c |= c << 14;
    c |= c << 28;
    return c >> 56;
}

void HELPER(vmskltz_b)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.H(0) = do_vmskltz_b(Vj->D(0));
    temp.H(0) |= (do_vmskltz_b(Vj->D(1)) << 8);
    Vd->D(0) = temp.D(0);
    Vd->D(1) = 0;
}

static uint64_t do_vmskltz_h(int64_t val)
{
    uint64_t m = 0x8000800080008000ULL;
    uint64_t c =  val & m;
    c |= c << 15;
    c |= c << 30;
    return c >> 60;
}

void HELPER(vmskltz_h)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.H(0) = do_vmskltz_h(Vj->D(0));
    temp.H(0) |= (do_vmskltz_h(Vj->D(1)) << 4);
    Vd->D(0) = temp.D(0);
    Vd->D(1) = 0;
}

static uint64_t do_vmskltz_w(int64_t val)
{
    uint64_t m = 0x8000000080000000ULL;
    uint64_t c =  val & m;
    c |= c << 31;
    return c >> 62;
}

void HELPER(vmskltz_w)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.H(0) = do_vmskltz_w(Vj->D(0));
    temp.H(0) |= (do_vmskltz_w(Vj->D(1)) << 2);
    Vd->D(0) = temp.D(0);
    Vd->D(1) = 0;
}

static uint64_t do_vmskltz_d(int64_t val)
{
    uint64_t m = 0x8000000000000000ULL;
    uint64_t c =  val & m;
    c |= c << 63;
    return c >> 63;
}
void HELPER(vmskltz_d)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.H(0) = do_vmskltz_d(Vj->D(0));
    temp.H(0) |= (do_vmskltz_d(Vj->D(1)) << 1);
    Vd->D(0) = temp.D(0);
    Vd->D(1) = 0;
}

void HELPER(vmskgez_b)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.H(0) =   do_vmskltz_b(Vj->D(0));
    temp.H(0) |= (do_vmskltz_b(Vj->D(1)) << 8);
    temp.H(0) = ~temp.H(0);
    Vd->D(0) = temp.D(0);
    Vd->D(1) = 0;
}

static uint64_t do_vmskez_b(uint64_t a)
{
    uint64_t m = 0x7f7f7f7f7f7f7f7fULL;
    uint64_t c = ~(((a & m) + m) | a | m);
    c |= c << 7;
    c |= c << 14;
    c |= c << 28;
    return c >> 56;
}

void HELPER(vmsknz_b)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.H(0) = do_vmskez_b(Vj->D(0));
    temp.H(0) |= (do_vmskez_b(Vj->D(1)) << 8);
    temp.H(0) = ~temp.H(0);
    Vd->D(0) = temp.D(0);
    Vd->D(1) = 0;
}

void HELPER(vnori_b)(void *vd, void *vj, uint64_t imm, uint32_t v)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    for (i = 0; i < LSX_LEN/8; i++) {
        Vd->B(i) = ~(Vj->B(i) | (uint8_t)imm);
    }
}

#define VSLLWIL(NAME, BIT, T1, T2, E1, E2)                \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i;                                                \
    VReg temp;                                            \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
    temp.D(0) = 0;                                        \
    temp.D(1) = 0;                                        \
    for (i = 0; i < LSX_LEN/BIT; i++) {                   \
        temp.E1(i) = (T1)(T2)Vj->E2(i) << (imm % BIT);    \
    }                                                     \
    Vd->D(0) = temp.D(0);                                 \
    Vd->D(1) = temp.D(1);                                 \
}

void HELPER(vextl_q_d)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    Vd->Q(0) = int128_makes64(Vj->D(0));
}

void HELPER(vextl_qu_du)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    Vd->Q(0) = int128_make64(Vj->D(0));
}

VSLLWIL(vsllwil_h_b, 16, int16_t, int8_t, H, B)
VSLLWIL(vsllwil_w_h, 32, int32_t, int16_t, W, H)
VSLLWIL(vsllwil_d_w, 64, int64_t, int32_t, D, W)
VSLLWIL(vsllwil_hu_bu, 16, uint16_t, uint8_t, H, B)
VSLLWIL(vsllwil_wu_hu, 32, uint32_t, uint16_t, W, H)
VSLLWIL(vsllwil_du_wu, 64, uint64_t, uint32_t, D, W)

#define do_vsrlr(E, T)                                  \
static T do_vsrlr_ ##E(T s1, int sh)                    \
{                                                       \
    if (sh == 0) {                                      \
        return s1;                                      \
    } else {                                            \
        return  (s1 >> sh)  + ((s1 >> (sh - 1)) & 0x1); \
    }                                                   \
}

do_vsrlr(B, uint8_t)
do_vsrlr(H, uint16_t)
do_vsrlr(W, uint32_t)
do_vsrlr(D, uint64_t)

#define VSRLR(NAME, BIT, T, E)                                  \
void HELPER(NAME)(CPULoongArchState *env,                       \
                  uint32_t vd, uint32_t vj, uint32_t vk)        \
{                                                               \
    int i;                                                      \
    VReg *Vd = &(env->fpr[vd].vreg);                            \
    VReg *Vj = &(env->fpr[vj].vreg);                            \
    VReg *Vk = &(env->fpr[vk].vreg);                            \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = do_vsrlr_ ## E(Vj->E(i), ((T)Vk->E(i))%BIT); \
    }                                                           \
}

VSRLR(vsrlr_b, 8,  uint8_t, B)
VSRLR(vsrlr_h, 16, uint16_t, H)
VSRLR(vsrlr_w, 32, uint32_t, W)
VSRLR(vsrlr_d, 64, uint64_t, D)

#define VSRLRI(NAME, BIT, E)                              \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i;                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                   \
        Vd->E(i) = do_vsrlr_ ## E(Vj->E(i), imm);         \
    }                                                     \
}

VSRLRI(vsrlri_b, 8, B)
VSRLRI(vsrlri_h, 16, H)
VSRLRI(vsrlri_w, 32, W)
VSRLRI(vsrlri_d, 64, D)

#define do_vsrar(E, T)                                  \
static T do_vsrar_ ##E(T s1, int sh)                    \
{                                                       \
    if (sh == 0) {                                      \
        return s1;                                      \
    } else {                                            \
        return  (s1 >> sh)  + ((s1 >> (sh - 1)) & 0x1); \
    }                                                   \
}

do_vsrar(B, int8_t)
do_vsrar(H, int16_t)
do_vsrar(W, int32_t)
do_vsrar(D, int64_t)

#define VSRAR(NAME, BIT, T, E)                                  \
void HELPER(NAME)(CPULoongArchState *env,                       \
                  uint32_t vd, uint32_t vj, uint32_t vk)        \
{                                                               \
    int i;                                                      \
    VReg *Vd = &(env->fpr[vd].vreg);                            \
    VReg *Vj = &(env->fpr[vj].vreg);                            \
    VReg *Vk = &(env->fpr[vk].vreg);                            \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = do_vsrar_ ## E(Vj->E(i), ((T)Vk->E(i))%BIT); \
    }                                                           \
}

VSRAR(vsrar_b, 8,  uint8_t, B)
VSRAR(vsrar_h, 16, uint16_t, H)
VSRAR(vsrar_w, 32, uint32_t, W)
VSRAR(vsrar_d, 64, uint64_t, D)

#define VSRARI(NAME, BIT, E)                              \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i;                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                   \
        Vd->E(i) = do_vsrar_ ## E(Vj->E(i), imm);         \
    }                                                     \
}

VSRARI(vsrari_b, 8, B)
VSRARI(vsrari_h, 16, H)
VSRARI(vsrari_w, 32, W)
VSRARI(vsrari_d, 64, D)

#define R_SHIFT(a, b) (a >> b)

#define VSRLN(NAME, BIT, T, E1, E2)                             \
void HELPER(NAME)(CPULoongArchState *env,                       \
                  uint32_t vd, uint32_t vj, uint32_t vk)        \
{                                                               \
    int i;                                                      \
    VReg *Vd = &(env->fpr[vd].vreg);                            \
    VReg *Vj = &(env->fpr[vj].vreg);                            \
    VReg *Vk = &(env->fpr[vk].vreg);                            \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E1(i) = R_SHIFT((T)Vj->E2(i),((T)Vk->E2(i)) % BIT); \
    }                                                           \
    Vd->D(1) = 0;                                               \
}

VSRLN(vsrln_b_h, 16, uint16_t, B, H)
VSRLN(vsrln_h_w, 32, uint32_t, H, W)
VSRLN(vsrln_w_d, 64, uint64_t, W, D)

#define VSRAN(NAME, BIT, T, E1, E2)                           \
void HELPER(NAME)(CPULoongArchState *env,                     \
                  uint32_t vd, uint32_t vj, uint32_t vk)      \
{                                                             \
    int i;                                                    \
    VReg *Vd = &(env->fpr[vd].vreg);                          \
    VReg *Vj = &(env->fpr[vj].vreg);                          \
    VReg *Vk = &(env->fpr[vk].vreg);                          \
                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                       \
        Vd->E1(i) = R_SHIFT(Vj->E2(i), ((T)Vk->E2(i)) % BIT); \
    }                                                         \
    Vd->D(1) = 0;                                             \
}

VSRAN(vsran_b_h, 16, uint16_t, B, H)
VSRAN(vsran_h_w, 32, uint32_t, H, W)
VSRAN(vsran_w_d, 64, uint64_t, W, D)

#define VSRLNI(NAME, BIT, T, E1, E2)                         \
void HELPER(NAME)(CPULoongArchState *env,                    \
                  uint32_t vd, uint32_t vj, uint32_t imm)    \
{                                                            \
    int i, max;                                              \
    VReg temp;                                               \
    VReg *Vd = &(env->fpr[vd].vreg);                         \
    VReg *Vj = &(env->fpr[vj].vreg);                         \
                                                             \
    temp.D(0) = 0;                                           \
    temp.D(1) = 0;                                           \
    max = LSX_LEN/BIT;                                       \
    for (i = 0; i < max; i++) {                              \
        temp.E1(i) = R_SHIFT((T)Vj->E2(i), imm);             \
        temp.E1(i + max) = R_SHIFT((T)Vd->E2(i), imm);       \
    }                                                        \
    Vd->D(0) = temp.D(0);                                    \
    Vd->D(1) = temp.D(1);                                    \
}

void HELPER(vsrlni_d_q)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t imm)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.D(0) = int128_urshift(Vj->Q(0), imm % 128);
    temp.D(1) = int128_urshift(Vd->Q(0), imm % 128);
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

VSRLNI(vsrlni_b_h, 16, uint16_t, B, H)
VSRLNI(vsrlni_h_w, 32, uint32_t, H, W)
VSRLNI(vsrlni_w_d, 64, uint64_t, W, D)

#define VSRANI(NAME, BIT, E1, E2)                         \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i, max;                                           \
    VReg temp;                                            \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
                                                          \
    temp.D(0) = 0;                                        \
    temp.D(1) = 0;                                        \
    max = LSX_LEN/BIT;                                    \
    for (i = 0; i < max; i++) {                           \
        temp.E1(i) = R_SHIFT(Vj->E2(i), imm);             \
        temp.E1(i + max) = R_SHIFT(Vd->E2(i), imm);       \
    }                                                     \
    Vd->D(0) = temp.D(0);                                 \
    Vd->D(1) = temp.D(1);                                 \
}

void HELPER(vsrani_d_q)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t imm)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.D(0) = int128_rshift(Vj->Q(0), imm % 128);
    temp.D(1) = int128_rshift(Vd->Q(0), imm % 128);
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

VSRANI(vsrani_b_h, 16, B, H)
VSRANI(vsrani_h_w, 32, H, W)
VSRANI(vsrani_w_d, 64, W, D)

#define VSRLRN(NAME, BIT, T, E1, E2)                                \
void HELPER(NAME)(CPULoongArchState *env,                           \
                  uint32_t vd, uint32_t vj, uint32_t vk)            \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
    VReg *Vk = &(env->fpr[vk].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E1(i) = do_vsrlr_ ## E2(Vj->E2(i), ((T)Vk->E2(i))%BIT); \
    }                                                               \
    Vd->D(1) = 0;                                                   \
}

VSRLRN(vsrlrn_b_h, 16, uint16_t, B, H)
VSRLRN(vsrlrn_h_w, 32, uint32_t, H, W)
VSRLRN(vsrlrn_w_d, 64, uint64_t, W, D)

#define VSRARN(NAME, BIT, T, E1, E2)                                \
void HELPER(NAME)(CPULoongArchState *env,                           \
                  uint32_t vd, uint32_t vj, uint32_t vk)            \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
    VReg *Vk = &(env->fpr[vk].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E1(i) = do_vsrar_ ## E2(Vj->E2(i), ((T)Vk->E2(i))%BIT); \
    }                                                               \
    Vd->D(1) = 0;                                                   \
}

VSRARN(vsrarn_b_h, 16, uint8_t,  B, H)
VSRARN(vsrarn_h_w, 32, uint16_t, H, W)
VSRARN(vsrarn_w_d, 64, uint32_t, W, D)

#define VSRLRNI(NAME, BIT, E1, E2)                          \
void HELPER(NAME)(CPULoongArchState *env,                   \
                  uint32_t vd, uint32_t vj, uint32_t imm)   \
{                                                           \
    int i, max;                                             \
    VReg temp;                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                        \
    VReg *Vj = &(env->fpr[vj].vreg);                        \
                                                            \
    temp.D(0) = 0;                                          \
    temp.D(1) = 0;                                          \
    max = LSX_LEN/BIT;                                      \
    for (i = 0; i < max; i++) {                             \
        temp.E1(i) = do_vsrlr_ ## E2(Vj->E2(i), imm);       \
        temp.E1(i + max) = do_vsrlr_ ## E2(Vd->E2(i), imm); \
    }                                                       \
    Vd->D(0) = temp.D(0);                                   \
    Vd->D(1) = temp.D(1);                                   \
}

void HELPER(vsrlrni_d_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    Int128 r1, r2;

    if (imm == 0) {
        temp.D(0) = int128_getlo(Vj->Q(0));
        temp.D(1) = int128_getlo(Vd->D(0));
    } else {
        r1 = int128_and(int128_urshift(Vj->Q(0), (imm -1)), int128_one());
        r2 = int128_and(int128_urshift(Vd->Q(0), (imm -1)), int128_one());

       temp.D(0) = int128_getlo(int128_add(int128_urshift(Vj->Q(0), imm), r1));
       temp.D(1) = int128_getlo(int128_add(int128_urshift(Vd->Q(0), imm), r2));
    }

    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

VSRLRNI(vsrlrni_b_h, 16, B, H)
VSRLRNI(vsrlrni_h_w, 32, H, W)
VSRLRNI(vsrlrni_w_d, 64, W, D)

#define VSRARNI(NAME, BIT, E1, E2)                          \
void HELPER(NAME)(CPULoongArchState *env,                   \
                  uint32_t vd, uint32_t vj, uint32_t imm)   \
{                                                           \
    int i, max;                                             \
    VReg temp;                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                        \
    VReg *Vj = &(env->fpr[vj].vreg);                        \
                                                            \
    temp.D(0) = 0;                                          \
    temp.D(1) = 0;                                          \
    max = LSX_LEN/BIT;                                      \
    for (i = 0; i < max; i++) {                             \
        temp.E1(i) = do_vsrar_ ## E2(Vj->E2(i), imm);       \
        temp.E1(i + max) = do_vsrar_ ## E2(Vd->E2(i), imm); \
    }                                                       \
    Vd->D(0) = temp.D(0);                                   \
    Vd->D(1) = temp.D(1);                                   \
}

void HELPER(vsrarni_d_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    Int128 r1, r2;

    if (imm == 0) {
        temp.D(0) = int128_getlo(Vj->Q(0));
        temp.D(1) = int128_getlo(Vd->D(0));
    } else {
        r1 = int128_and(int128_rshift(Vj->Q(0), (imm -1)), int128_one());
        r2 = int128_and(int128_rshift(Vd->Q(0), (imm -1)), int128_one());

       temp.D(0) = int128_getlo(int128_add(int128_rshift(Vj->Q(0), imm), r1));
       temp.D(1) = int128_getlo(int128_add(int128_rshift(Vd->Q(0), imm), r2));
    }

    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

VSRARNI(vsrarni_b_h, 16, B, H)
VSRARNI(vsrarni_h_w, 32, H, W)
VSRARNI(vsrarni_w_d, 64, W, D)

#define SSRLNS(NAME, T1, T2, T3)                    \
static T1 do_ssrlns_ ## NAME(T2 e2, int sa, int sh) \
{                                                   \
        T1 shft_res;                                \
        if (sa == 0) {                              \
            shft_res = e2;                          \
        } else {                                    \
            shft_res = (((T1)e2) >> sa);            \
        }                                           \
        T3 mask;                                    \
        mask = (1u << sh) -1;                       \
        if (shft_res > mask) {                      \
            return mask;                            \
        } else {                                    \
            return  shft_res;                       \
        }                                           \
}

SSRLNS(B, uint16_t, int16_t, uint8_t)
SSRLNS(H, uint32_t, int32_t, uint16_t)
SSRLNS(W, uint64_t, int64_t, uint32_t)

#define VSSRLN(NAME, BIT, T, E1, E2)                                          \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t vd, uint32_t vj, uint32_t vk)                      \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = &(env->fpr[vd].vreg);                                          \
    VReg *Vj = &(env->fpr[vj].vreg);                                          \
    VReg *Vk = &(env->fpr[vk].vreg);                                          \
                                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        Vd->E1(i) = do_ssrlns_ ## E1(Vj->E2(i), (T)Vk->E2(i)% BIT, BIT/2 -1); \
    }                                                                         \
    Vd->D(1) = 0;                                                             \
}

VSSRLN(vssrln_b_h, 16, uint16_t, B, H)
VSSRLN(vssrln_h_w, 32, uint32_t, H, W)
VSSRLN(vssrln_w_d, 64, uint64_t, W, D)

#define SSRANS(E, T1, T2)                        \
static T1 do_ssrans_ ## E(T1 e2, int sa, int sh) \
{                                                \
        T1 shft_res;                             \
        if (sa == 0) {                           \
            shft_res = e2;                       \
        } else {                                 \
            shft_res = e2 >> sa;                 \
        }                                        \
        T2 mask;                                 \
        mask = (1l << sh) -1;                    \
        if (shft_res > mask) {                   \
            return  mask;                        \
        } else if (shft_res < -(mask +1)) {      \
            return  ~mask;                       \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRANS(B, int16_t, int8_t)
SSRANS(H, int32_t, int16_t)
SSRANS(W, int64_t, int32_t)

#define VSSRAN(NAME, BIT, T, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t vd, uint32_t vj, uint32_t vk)                     \
{                                                                            \
    int i;                                                                   \
    VReg *Vd = &(env->fpr[vd].vreg);                                         \
    VReg *Vj = &(env->fpr[vj].vreg);                                         \
    VReg *Vk = &(env->fpr[vk].vreg);                                         \
                                                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                      \
        Vd->E1(i) = do_ssrans_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2 -1); \
    }                                                                        \
    Vd->D(1) = 0;                                                            \
}

VSSRAN(vssran_b_h, 16, uint16_t, B, H)
VSSRAN(vssran_h_w, 32, uint32_t, H, W)
VSSRAN(vssran_w_d, 64, uint64_t, W, D)

#define SSRLNU(E, T1, T2, T3)                    \
static T1 do_ssrlnu_ ## E(T3 e2, int sa, int sh) \
{                                                \
        T1 shft_res;                             \
        if (sa == 0) {                           \
            shft_res = e2;                       \
        } else {                                 \
            shft_res = (((T1)e2) >> sa);         \
        }                                        \
        T2 mask;                                 \
        mask = (1ul << sh) -1;                   \
        if (shft_res > mask) {                   \
            return mask;                         \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRLNU(B, uint16_t, uint8_t,  int16_t)
SSRLNU(H, uint32_t, uint16_t, int32_t)
SSRLNU(W, uint64_t, uint32_t, int64_t)

#define VSSRLNU(NAME, BIT, T, E1, E2)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                 \
                  uint32_t vd, uint32_t vj, uint32_t vk)                  \
{                                                                         \
    int i;                                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                                      \
    VReg *Vj = &(env->fpr[vj].vreg);                                      \
    VReg *Vk = &(env->fpr[vk].vreg);                                      \
                                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                   \
        Vd->E1(i) = do_ssrlnu_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2); \
    }                                                                     \
    Vd->D(1) = 0;                                                         \
}

VSSRLNU(vssrln_bu_h, 16, uint16_t, B, H)
VSSRLNU(vssrln_hu_w, 32, uint32_t, H, W)
VSSRLNU(vssrln_wu_d, 64, uint64_t, W, D)

#define SSRANU(E, T1, T2, T3)                    \
static T1 do_ssranu_ ## E(T3 e2, int sa, int sh) \
{                                                \
        T1 shft_res;                             \
        if (sa == 0) {                           \
            shft_res = e2;                       \
        } else {                                 \
            shft_res = e2 >> sa;                 \
        }                                        \
        if (e2 < 0) {                            \
            shft_res = 0;                        \
        }                                        \
        T2 mask;                                 \
        mask = (1ul << sh) -1;                   \
        if (shft_res > mask) {                   \
            return mask;                         \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRANU(B, uint16_t, uint8_t,  int16_t)
SSRANU(H, uint32_t, uint16_t, int32_t)
SSRANU(W, uint64_t, uint32_t, int64_t)

#define VSSRANU(NAME, BIT, T, E1, E2)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                 \
                  uint32_t vd, uint32_t vj, uint32_t vk)                  \
{                                                                         \
    int i;                                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                                      \
    VReg *Vj = &(env->fpr[vj].vreg);                                      \
    VReg *Vk = &(env->fpr[vk].vreg);                                      \
                                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                   \
        Vd->E1(i) = do_ssranu_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2); \
    }                                                                     \
    Vd->D(1) = 0;                                                         \
}

VSSRANU(vssran_bu_h, 16, uint16_t, B, H)
VSSRANU(vssran_hu_w, 32, uint32_t, H, W)
VSSRANU(vssran_wu_d, 64, uint64_t, W, D)

#define VSSRLNI(NAME, BIT, E1, E2)                                            \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t vd, uint32_t vj, uint32_t imm)                     \
{                                                                             \
    int i;                                                                    \
    VReg temp;                                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                                          \
    VReg *Vj = &(env->fpr[vj].vreg);                                          \
                                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        temp.E1(i) = do_ssrlns_ ## E1(Vj->E2(i), imm, BIT/2 -1);              \
        temp.E1(i + LSX_LEN/BIT) = do_ssrlns_ ## E1(Vd->E2(i), imm, BIT/2 -1);\
    }                                                                         \
    Vd->D(0) = temp.D(0);                                                     \
    Vd->D(1) = temp.D(1);                                                     \
}

void HELPER(vssrlni_d_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        shft_res1 = int128_urshift(Vj->Q(0), imm);
        shft_res2 = int128_urshift(Vd->Q(0), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());

    if (int128_ult(mask, shft_res1)) {
        Vd->D(0) = int128_getlo(mask);
    }else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(1) = int128_getlo(mask);
    }else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRLNI(vssrlni_b_h, 16, B, H)
VSSRLNI(vssrlni_h_w, 32, H, W)
VSSRLNI(vssrlni_w_d, 64, W, D)

#define VSSRANI(NAME, BIT, E1, E2)                                             \
void HELPER(NAME)(CPULoongArchState *env,                                      \
                  uint32_t vd, uint32_t vj, uint32_t imm)                      \
{                                                                              \
    int i;                                                                     \
    VReg temp;                                                                 \
    VReg *Vd = &(env->fpr[vd].vreg);                                           \
    VReg *Vj = &(env->fpr[vj].vreg);                                           \
                                                                               \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                        \
        temp.E1(i) = do_ssrans_ ## E1(Vj->E2(i), imm, BIT/2 -1);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrans_ ## E1(Vd->E2(i), imm, BIT/2 -1); \
    }                                                                          \
    Vd->D(0) = temp.D(0);                                                      \
    Vd->D(1) = temp.D(1);                                                      \
}

void HELPER(vssrani_d_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask, min;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        shft_res1 = int128_rshift(Vj->Q(0), imm);
        shft_res2 = int128_rshift(Vd->Q(0), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());
    min  = int128_lshift(int128_one(), 63);

    if (int128_gt(shft_res1,  mask)) {
        Vd->D(0) = int128_getlo(mask);
    } else if (int128_lt(shft_res1, int128_neg(min))) {
        Vd->D(0) = int128_getlo(min);
    } else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask)) {
        Vd->D(1) = int128_getlo(mask);
    } else if (int128_lt(shft_res2, int128_neg(min))) {
        Vd->D(1) = int128_getlo(min);
    } else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRANI(vssrani_b_h, 16, B, H)
VSSRANI(vssrani_h_w, 32, H, W)
VSSRANI(vssrani_w_d, 64, W, D)

#define VSSRLNUI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                   \
                  uint32_t vd, uint32_t vj, uint32_t imm)                   \
{                                                                           \
    int i;                                                                  \
    VReg temp;                                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                                        \
    VReg *Vj = &(env->fpr[vj].vreg);                                        \
                                                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                     \
        temp.E1(i) = do_ssrlnu_ ## E1(Vj->E2(i), imm, BIT/2);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrlnu_ ## E1(Vd->E2(i), imm, BIT/2); \
    }                                                                       \
    Vd->D(0) = temp.D(0);                                                   \
    Vd->D(1) = temp.D(1);                                                   \
}

void HELPER(vssrlni_du_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        shft_res1 = int128_urshift(Vj->Q(0), imm);
        shft_res2 = int128_urshift(Vd->Q(0), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    if (int128_ult(mask, shft_res1)) {
        Vd->D(0) = int128_getlo(mask);
    }else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(1) = int128_getlo(mask);
    }else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRLNUI(vssrlni_bu_h, 16, B, H)
VSSRLNUI(vssrlni_hu_w, 32, H, W)
VSSRLNUI(vssrlni_wu_d, 64, W, D)

#define VSSRANUI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                   \
                  uint32_t vd, uint32_t vj, uint32_t imm)                   \
{                                                                           \
    int i;                                                                  \
    VReg temp;                                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                                        \
    VReg *Vj = &(env->fpr[vj].vreg);                                        \
                                                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                     \
        temp.E1(i) = do_ssranu_ ## E1(Vj->E2(i), imm, BIT/2);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssranu_ ## E1(Vd->E2(i), imm, BIT/2); \
    }                                                                       \
    Vd->D(0) = temp.D(0);                                                   \
    Vd->D(1) = temp.D(1);                                                   \
}

void HELPER(vssrani_du_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        shft_res1 = int128_rshift(Vj->Q(0), imm);
        shft_res2 = int128_rshift(Vd->Q(0), imm);
    }

    if (int128_lt(Vj->Q(0), int128_zero())) {
        shft_res1 = int128_zero();
    }

    if (int128_lt(Vd->Q(0), int128_zero())) {
        shft_res2 = int128_zero();
    }

    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    if (int128_ult(mask, shft_res1)) {
        Vd->D(0) = int128_getlo(mask);
    }else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(1) = int128_getlo(mask);
    }else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRANUI(vssrani_bu_h, 16, B, H)
VSSRANUI(vssrani_hu_w, 32, H, W)
VSSRANUI(vssrani_wu_d, 64, W, D)

#define SSRLRNS(E1, E2, T1, T2, T3)                \
static T1 do_ssrlrns_ ## E1(T2 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    shft_res = do_vsrlr_ ## E2(e2, sa);            \
    T1 mask;                                       \
    mask = (1ul << sh) -1;                         \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return  shft_res;                          \
    }                                              \
}

SSRLRNS(B, H, uint16_t, int16_t, uint8_t)
SSRLRNS(H, W, uint32_t, int32_t, uint16_t)
SSRLRNS(W, D, uint64_t, int64_t, uint32_t)

#define VSSRLRN(NAME, BIT, T, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t vd, uint32_t vj, uint32_t vk)                      \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = &(env->fpr[vd].vreg);                                          \
    VReg *Vj = &(env->fpr[vj].vreg);                                          \
    VReg *Vk = &(env->fpr[vk].vreg);                                          \
                                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        Vd->E1(i) = do_ssrlrns_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2 -1); \
    }                                                                         \
    Vd->D(1) = 0;                                                             \
}

VSSRLRN(vssrlrn_b_h, 16, uint16_t, B, H)
VSSRLRN(vssrlrn_h_w, 32, uint32_t, H, W)
VSSRLRN(vssrlrn_w_d, 64, uint64_t, W, D)

#define SSRARNS(E1, E2, T1, T2)                    \
static T1 do_ssrarns_ ## E1(T1 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    shft_res = do_vsrar_ ## E2(e2, sa);            \
    T2 mask;                                       \
    mask = (1l << sh) -1;                          \
    if (shft_res > mask) {                         \
        return  mask;                              \
    } else if (shft_res < -(mask +1)) {            \
        return  ~mask;                             \
    } else {                                       \
        return shft_res;                           \
    }                                              \
}

SSRARNS(B, H, int16_t, int8_t)
SSRARNS(H, W, int32_t, int16_t)
SSRARNS(W, D, int64_t, int32_t)

#define VSSRARN(NAME, BIT, T, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t vd, uint32_t vj, uint32_t vk)                      \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = &(env->fpr[vd].vreg);                                          \
    VReg *Vj = &(env->fpr[vj].vreg);                                          \
    VReg *Vk = &(env->fpr[vk].vreg);                                          \
                                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        Vd->E1(i) = do_ssrarns_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2 -1); \
    }                                                                         \
    Vd->D(1) = 0;                                                             \
}

VSSRARN(vssrarn_b_h, 16, uint16_t, B, H)
VSSRARN(vssrarn_h_w, 32, uint32_t, H, W)
VSSRARN(vssrarn_w_d, 64, uint64_t, W, D)

#define SSRLRNU(E1, E2, T1, T2, T3)                \
static T1 do_ssrlrnu_ ## E1(T3 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    shft_res = do_vsrlr_ ## E2(e2, sa);            \
                                                   \
    T2 mask;                                       \
    mask = (1ul << sh) -1;                         \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return shft_res;                           \
    }                                              \
}

SSRLRNU(B, H, uint16_t, uint8_t, int16_t)
SSRLRNU(H, W, uint32_t, uint16_t, int32_t)
SSRLRNU(W, D, uint64_t, uint32_t, int64_t)

#define VSSRLRNU(NAME, BIT, T, E1, E2)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                  \
                  uint32_t vd, uint32_t vj, uint32_t vk)                   \
{                                                                          \
    int i;                                                                 \
    VReg *Vd = &(env->fpr[vd].vreg);                                       \
    VReg *Vj = &(env->fpr[vj].vreg);                                       \
    VReg *Vk = &(env->fpr[vk].vreg);                                       \
                                                                           \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                    \
        Vd->E1(i) = do_ssrlrnu_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2); \
    }                                                                      \
    Vd->D(1) = 0;                                                          \
}

VSSRLRNU(vssrlrn_bu_h, 16, uint16_t, B, H)
VSSRLRNU(vssrlrn_hu_w, 32, uint32_t, H, W)
VSSRLRNU(vssrlrn_wu_d, 64, uint64_t, W, D)

#define SSRARNU(E1, E2, T1, T2, T3)                \
static T1 do_ssrarnu_ ## E1(T3 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    if (e2 < 0) {                                  \
        shft_res = 0;                              \
    } else {                                       \
        shft_res = do_vsrar_ ## E2(e2, sa);        \
    }                                              \
    T2 mask;                                       \
    mask = (1ul << sh) -1;                         \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return shft_res;                           \
    }                                              \
}

SSRARNU(B, H, uint16_t, uint8_t, int16_t)
SSRARNU(H, W, uint32_t, uint16_t, int32_t)
SSRARNU(W, D, uint64_t, uint32_t, int64_t)

#define VSSRARNU(NAME, BIT, T, E1, E2)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                  \
                  uint32_t vd, uint32_t vj, uint32_t vk)                   \
{                                                                          \
    int i;                                                                 \
    VReg *Vd = &(env->fpr[vd].vreg);                                       \
    VReg *Vj = &(env->fpr[vj].vreg);                                       \
    VReg *Vk = &(env->fpr[vk].vreg);                                       \
                                                                           \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                    \
        Vd->E1(i) = do_ssrarnu_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2); \
    }                                                                      \
    Vd->D(1) = 0;                                                          \
}

VSSRARNU(vssrarn_bu_h, 16, uint16_t, B, H)
VSSRARNU(vssrarn_hu_w, 32, uint32_t, H, W)
VSSRARNU(vssrarn_wu_d, 64, uint64_t, W, D)

#define VSSRLRNI(NAME, BIT, E1, E2)                                            \
void HELPER(NAME)(CPULoongArchState *env,                                      \
                  uint32_t vd, uint32_t vj, uint32_t imm)                      \
{                                                                              \
    int i;                                                                     \
    VReg temp;                                                                 \
    VReg *Vd = &(env->fpr[vd].vreg);                                           \
    VReg *Vj = &(env->fpr[vj].vreg);                                           \
                                                                               \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                        \
        temp.E1(i) = do_ssrlrns_ ## E1(Vj->E2(i), imm, BIT/2 -1);              \
        temp.E1(i + LSX_LEN/BIT) = do_ssrlrns_ ## E1(Vd->E2(i), imm, BIT/2 -1);\
    }                                                                          \
    Vd->D(0) = temp.D(0);                                                      \
    Vd->D(1) = temp.D(1);                                                      \
}

#define VSSRLRNI_Q(NAME, sh)                                               \
void HELPER(NAME)(CPULoongArchState *env,                                  \
                          uint32_t vd, uint32_t vj, uint32_t imm)          \
{                                                                          \
    Int128 shft_res1, shft_res2, mask, r1, r2;                             \
    VReg *Vd = &(env->fpr[vd].vreg);                                       \
    VReg *Vj = &(env->fpr[vj].vreg);                                       \
                                                                           \
    if (imm == 0) {                                                        \
        shft_res1 = Vj->Q(0);                                              \
        shft_res2 = Vd->Q(0);                                              \
    } else {                                                               \
        r1 = int128_and(int128_urshift(Vj->Q(0), (imm -1)), int128_one()); \
        r2 = int128_and(int128_urshift(Vd->Q(0), (imm -1)), int128_one()); \
                                                                           \
        shft_res1 = (int128_add(int128_urshift(Vj->Q(0), imm), r1));       \
        shft_res2 = (int128_add(int128_urshift(Vd->Q(0), imm), r2));       \
    }                                                                      \
                                                                           \
    mask = int128_sub(int128_lshift(int128_one(), sh), int128_one());      \
                                                                           \
    if (int128_ult(mask, shft_res1)) {                                     \
        Vd->D(0) = int128_getlo(mask);                                     \
    }else {                                                                \
        Vd->D(0) = int128_getlo(shft_res1);                                \
    }                                                                      \
                                                                           \
    if (int128_ult(mask, shft_res2)) {                                     \
        Vd->D(1) = int128_getlo(mask);                                     \
    }else {                                                                \
        Vd->D(1) = int128_getlo(shft_res2);                                \
    }                                                                      \
}

VSSRLRNI(vssrlrni_b_h, 16, B, H)
VSSRLRNI(vssrlrni_h_w, 32, H, W)
VSSRLRNI(vssrlrni_w_d, 64, W, D)
VSSRLRNI_Q(vssrlrni_d_q, 63)

#define VSSRARNI(NAME, BIT, E1, E2)                                             \
void HELPER(NAME)(CPULoongArchState *env,                                       \
                  uint32_t vd, uint32_t vj, uint32_t imm)                       \
{                                                                               \
    int i;                                                                      \
    VReg temp;                                                                  \
    VReg *Vd = &(env->fpr[vd].vreg);                                            \
    VReg *Vj = &(env->fpr[vj].vreg);                                            \
                                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                         \
        temp.E1(i) = do_ssrarns_ ## E1(Vj->E2(i), imm, BIT/2 -1);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrarns_ ## E1(Vd->E2(i), imm, BIT/2 -1); \
    }                                                                           \
    Vd->D(0) = temp.D(0);                                                       \
    Vd->D(1) = temp.D(1);                                                       \
}

void HELPER(vssrarni_d_q)(CPULoongArchState *env,
                          uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask1, mask2, r1, r2;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        r1 = int128_and(int128_rshift(Vj->Q(0), (imm -1)), int128_one());
        r2 = int128_and(int128_rshift(Vd->Q(0), (imm -1)), int128_one());

        shft_res1 = int128_add(int128_rshift(Vj->Q(0), imm), r1);
        shft_res2 = int128_add(int128_rshift(Vd->Q(0), imm), r2);
    }

    mask1 = int128_sub(int128_lshift(int128_one(), 63), int128_one());
    mask2  = int128_lshift(int128_one(), 63);

    if (int128_gt(shft_res1,  mask1)) {
        Vd->D(0) = int128_getlo(mask1);
    } else if (int128_lt(shft_res1, int128_neg(mask2))) {
        Vd->D(0) = int128_getlo(mask2);
    } else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask1)) {
        Vd->D(1) = int128_getlo(mask1);
    } else if (int128_lt(shft_res2, int128_neg(mask2))) {
        Vd->D(1) = int128_getlo(mask2);
    } else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRARNI(vssrarni_b_h, 16, B, H)
VSSRARNI(vssrarni_h_w, 32, H, W)
VSSRARNI(vssrarni_w_d, 64, W, D)

#define VSSRLRNUI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t vd, uint32_t vj, uint32_t imm)                    \
{                                                                            \
    int i;                                                                   \
    VReg temp;                                                               \
    VReg *Vd = &(env->fpr[vd].vreg);                                         \
    VReg *Vj = &(env->fpr[vj].vreg);                                         \
                                                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                      \
        temp.E1(i) = do_ssrlrnu_ ## E1(Vj->E2(i), imm, BIT/2);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrlrnu_ ## E1(Vd->E2(i), imm, BIT/2); \
    }                                                                        \
    Vd->D(0) = temp.D(0);                                                    \
    Vd->D(1) = temp.D(1);                                                    \
}

VSSRLRNUI(vssrlrni_bu_h, 16, B, H)
VSSRLRNUI(vssrlrni_hu_w, 32, H, W)
VSSRLRNUI(vssrlrni_wu_d, 64, W, D)
VSSRLRNI_Q(vssrlrni_du_q, 64)

#define VSSRARNUI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t vd, uint32_t vj, uint32_t imm)                    \
{                                                                            \
    int i;                                                                   \
    VReg temp;                                                               \
    VReg *Vd = &(env->fpr[vd].vreg);                                         \
    VReg *Vj = &(env->fpr[vj].vreg);                                         \
                                                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                      \
        temp.E1(i) = do_ssrarnu_ ## E1(Vj->E2(i), imm, BIT/2);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrarnu_ ## E1(Vd->E2(i), imm, BIT/2); \
    }                                                                        \
    Vd->D(0) = temp.D(0);                                                    \
    Vd->D(1) = temp.D(1);                                                    \
}

void HELPER(vssrarni_du_q)(CPULoongArchState *env,
                           uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask1, mask2, r1, r2;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        r1 = int128_and(int128_rshift(Vj->Q(0), (imm -1)), int128_one());
        r2 = int128_and(int128_rshift(Vd->Q(0), (imm -1)), int128_one());

        shft_res1 = int128_add(int128_rshift(Vj->Q(0), imm), r1);
        shft_res2 = int128_add(int128_rshift(Vd->Q(0), imm), r2);
    }

    if (int128_lt(Vj->Q(0), int128_zero())) {
        shft_res1 = int128_zero();
    }
    if (int128_lt(Vd->Q(0), int128_zero())) {
        shft_res2 = int128_zero();
    }

    mask1 = int128_sub(int128_lshift(int128_one(), 64), int128_one());
    mask2  = int128_lshift(int128_one(), 64);

    if (int128_gt(shft_res1,  mask1)) {
        Vd->D(0) = int128_getlo(mask1);
    } else if (int128_lt(shft_res1, int128_neg(mask2))) {
        Vd->D(0) = int128_getlo(mask2);
    } else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask1)) {
        Vd->D(1) = int128_getlo(mask1);
    } else if (int128_lt(shft_res2, int128_neg(mask2))) {
        Vd->D(1) = int128_getlo(mask2);
    } else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRARNUI(vssrarni_bu_h, 16, B, H)
VSSRARNUI(vssrarni_hu_w, 32, H, W)
VSSRARNUI(vssrarni_wu_d, 64, W, D)

#define DO_2OP(NAME, BIT, E, T, DO_OP)                              \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++)                               \
    {                                                               \
        Vd->E(i) = DO_OP((T)Vj->E(i));                              \
    }                                                               \
}

#define DO_CLO_B(N)  (clz32((uint8_t)~N) - 24)
#define DO_CLO_H(N)  (clz32((uint16_t)~N) - 16)
#define DO_CLO_W(N)  (clz32((uint32_t)~N))
#define DO_CLO_D(N)  (clz64((uint64_t)~N))
#define DO_CLZ_B(N)  (clz32(N) - 24)
#define DO_CLZ_H(N)  (clz32(N) - 16)
#define DO_CLZ_W(N)  (clz32(N))
#define DO_CLZ_D(N)  (clz64(N))

DO_2OP(vclo_b, 8, B, uint8_t, DO_CLO_B)
DO_2OP(vclo_h, 16, H, uint16_t, DO_CLO_H)
DO_2OP(vclo_w, 32, W, uint32_t, DO_CLO_W)
DO_2OP(vclo_d, 64, D, uint64_t, DO_CLO_D)
DO_2OP(vclz_b, 8, B, uint8_t, DO_CLZ_B)
DO_2OP(vclz_h, 16, H, uint16_t, DO_CLZ_H)
DO_2OP(vclz_w, 32, W, uint32_t, DO_CLZ_W)
DO_2OP(vclz_d, 64, D, uint64_t, DO_CLZ_D)

static uint64_t do_vpcnt(uint64_t u1)
{
    u1 = (u1 & 0x5555555555555555ULL) + ((u1 >>  1) & 0x5555555555555555ULL);
    u1 = (u1 & 0x3333333333333333ULL) + ((u1 >>  2) & 0x3333333333333333ULL);
    u1 = (u1 & 0x0F0F0F0F0F0F0F0FULL) + ((u1 >>  4) & 0x0F0F0F0F0F0F0F0FULL);
    u1 = (u1 & 0x00FF00FF00FF00FFULL) + ((u1 >>  8) & 0x00FF00FF00FF00FFULL);
    u1 = (u1 & 0x0000FFFF0000FFFFULL) + ((u1 >> 16) & 0x0000FFFF0000FFFFULL);
    u1 = (u1 & 0x00000000FFFFFFFFULL) + ((u1 >> 32));

    return u1;
}

#define VPCNT(NAME, BIT, E, T)                                      \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++)                               \
    {                                                               \
        Vd->E(i) = do_vpcnt((T)Vj->E(i));                           \
    }                                                               \
}

VPCNT(vpcnt_b, 8, B, uint8_t)
VPCNT(vpcnt_h, 16, H, uint16_t)
VPCNT(vpcnt_w, 32, W, uint32_t)
VPCNT(vpcnt_d, 64, D, uint64_t)

#define DO_BITCLR(a, bit) (a & ~(1ul << bit))
#define DO_BITSET(a, bit) (a | 1ul << bit)
#define DO_BITREV(a, bit) (a ^ (1ul << bit))

#define DO_BIT(NAME, BIT, T, E, DO_OP)                   \
void HELPER(NAME)(CPULoongArchState *env,                \
                  uint32_t vd, uint32_t vj, uint32_t vk) \
{                                                        \
    int i;                                               \
    VReg *Vd = &(env->fpr[vd].vreg);                     \
    VReg *Vj = &(env->fpr[vj].vreg);                     \
    VReg *Vk = &(env->fpr[vk].vreg);                     \
                                                         \
    for (i = 0; i < LSX_LEN/BIT; i++) {                  \
        Vd->E(i) = DO_OP((T)Vj->E(i), (T)Vk->E(i)%BIT);  \
    }                                                    \
}

DO_BIT(vbitclr_b, 8, uint8_t, B, DO_BITCLR)
DO_BIT(vbitclr_h, 16, uint16_t, H, DO_BITCLR)
DO_BIT(vbitclr_w, 32, uint32_t, W, DO_BITCLR)
DO_BIT(vbitclr_d, 64, uint64_t, D, DO_BITCLR)
DO_BIT(vbitset_b, 8, uint8_t, B, DO_BITSET)
DO_BIT(vbitset_h, 16, uint16_t, H, DO_BITSET)
DO_BIT(vbitset_w, 32, uint32_t, W, DO_BITSET)
DO_BIT(vbitset_d, 64, uint64_t, D, DO_BITSET)
DO_BIT(vbitrev_b, 8, uint8_t, B, DO_BITREV)
DO_BIT(vbitrev_h, 16, uint16_t, H, DO_BITREV)
DO_BIT(vbitrev_w, 32, uint32_t, W, DO_BITREV)
DO_BIT(vbitrev_d, 64, uint64_t, D, DO_BITREV)

#define DO_BITI(NAME, BIT, T, E, DO_OP)                   \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i;                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                   \
        Vd->E(i) = DO_OP((T)Vj->E(i), imm);               \
    }                                                     \
}

DO_BITI(vbitclri_b, 8, uint8_t, B, DO_BITCLR)
DO_BITI(vbitclri_h, 16, uint16_t, H, DO_BITCLR)
DO_BITI(vbitclri_w, 32, uint32_t, W, DO_BITCLR)
DO_BITI(vbitclri_d, 64, uint64_t, D, DO_BITCLR)
DO_BITI(vbitseti_b, 8, uint8_t, B, DO_BITSET)
DO_BITI(vbitseti_h, 16, uint16_t, H, DO_BITSET)
DO_BITI(vbitseti_w, 32, uint32_t, W, DO_BITSET)
DO_BITI(vbitseti_d, 64, uint64_t, D, DO_BITSET)
DO_BITI(vbitrevi_b, 8, uint8_t, B, DO_BITREV)
DO_BITI(vbitrevi_h, 16, uint16_t, H, DO_BITREV)
DO_BITI(vbitrevi_w, 32, uint32_t, W, DO_BITREV)
DO_BITI(vbitrevi_d, 64, uint64_t, D, DO_BITREV)

#define VFRSTP(NAME, BIT, MASK, E)                       \
void HELPER(NAME)(CPULoongArchState *env,                \
                  uint32_t vd, uint32_t vj, uint32_t vk) \
{                                                        \
    int i, m;                                            \
    VReg *Vd = &(env->fpr[vd].vreg);                     \
    VReg *Vj = &(env->fpr[vj].vreg);                     \
    VReg *Vk = &(env->fpr[vk].vreg);                     \
                                                         \
    for (i = 0; i < LSX_LEN/BIT; i++) {                  \
        if (Vj->E(i) < 0) {                              \
            break;                                       \
        }                                                \
    }                                                    \
    m = Vk->E(0) & MASK;                                 \
    Vd->E(m) = i;                                        \
}

VFRSTP(vfrstp_b, 8, 0xf, B)
VFRSTP(vfrstp_h, 16, 0x7, H)

#define VFRSTPI(NAME, BIT, E)                             \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i, m;                                             \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                   \
        if (Vj->E(i) < 0) {                               \
            break;                                        \
        }                                                 \
    }                                                     \
    m = imm % (LSX_LEN/BIT);                              \
    Vd->E(m) = i;                                         \
}

VFRSTPI(vfrstpi_b, 8,  B)
VFRSTPI(vfrstpi_h, 16, H)

static void vec_update_fcsr0_mask(CPULoongArchState *env,
                                  uintptr_t pc, int mask)
{
    int flags = get_float_exception_flags(&env->fp_status);

    set_float_exception_flags(0, &env->fp_status);

    flags &= ~mask;

    if (flags) {
        flags = ieee_ex_to_loongarch(flags);
        UPDATE_FP_CAUSE(env->fcsr0, flags);
    }

    if (GET_FP_ENABLES(env->fcsr0) & flags) {
        do_raise_exception(env, EXCCODE_FPE, pc);
    } else {
        UPDATE_FP_FLAGS(env->fcsr0, flags);
    }
}

static void vec_update_fcsr0(CPULoongArchState *env, uintptr_t pc)
{
    vec_update_fcsr0_mask(env, pc, 0);
}

static inline void vec_clear_cause(CPULoongArchState *env)
{
    SET_FP_CAUSE(env->fcsr0, 0);
}

#define DO_3OP_F(NAME, BIT, T, E, FN)                             \
void HELPER(NAME)(CPULoongArchState *env,                         \
                  uint32_t vd, uint32_t vj, uint32_t vk)          \
{                                                                 \
    int i;                                                        \
    VReg *Vd = &(env->fpr[vd].vreg);                              \
    VReg *Vj = &(env->fpr[vj].vreg);                              \
    VReg *Vk = &(env->fpr[vk].vreg);                              \
                                                                  \
    vec_clear_cause(env);                                         \
    for (i = 0; i < LSX_LEN/BIT; i++) {                           \
        Vd->E(i) = FN((T)Vj->E(i), (T)Vk->E(i), &env->fp_status); \
        vec_update_fcsr0(env, GETPC());                           \
    }                                                             \
}

DO_3OP_F(vfadd_s, 32, uint32_t, W, float32_add)
DO_3OP_F(vfadd_d, 64, uint64_t, D, float64_add)
DO_3OP_F(vfsub_s, 32, uint32_t, W, float32_sub)
DO_3OP_F(vfsub_d, 64, uint64_t, D, float64_sub)
DO_3OP_F(vfmul_s, 32, uint32_t, W, float32_mul)
DO_3OP_F(vfmul_d, 64, uint64_t, D, float64_mul)
DO_3OP_F(vfdiv_s, 32, uint32_t, W, float32_div)
DO_3OP_F(vfdiv_d, 64, uint64_t, D, float64_div)
DO_3OP_F(vfmax_s, 32, uint32_t, W, float32_maxnum)
DO_3OP_F(vfmax_d, 64, uint64_t, D, float64_maxnum)
DO_3OP_F(vfmin_s, 32, uint32_t, W, float32_minnum)
DO_3OP_F(vfmin_d, 64, uint64_t, D, float64_minnum)
DO_3OP_F(vfmaxa_s, 32, uint32_t, W, float32_maxnummag)
DO_3OP_F(vfmaxa_d, 64, uint64_t, D, float64_maxnummag)
DO_3OP_F(vfmina_s, 32, uint32_t, W, float32_minnummag)
DO_3OP_F(vfmina_d, 64, uint64_t, D, float64_minnummag)

#define DO_4OP_F(NAME, BIT, T, E, FN, flags)                          \
void HELPER(NAME)(CPULoongArchState *env,                             \
                  uint32_t vd, uint32_t vj, uint32_t vk, uint32_t va) \
{                                                                     \
    int i;                                                            \
    VReg *Vd = &(env->fpr[vd].vreg);                                  \
    VReg *Vj = &(env->fpr[vj].vreg);                                  \
    VReg *Vk = &(env->fpr[vk].vreg);                                  \
    VReg *Va = &(env->fpr[va].vreg);                                  \
                                                                      \
    vec_clear_cause(env);                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                               \
        Vd->E(i) = FN((T)Vj->E(i), (T)Vk->E(i), (T)Va->E(i),          \
                      flags, &env->fp_status);                        \
        vec_update_fcsr0(env, GETPC());                               \
    }                                                                 \
}

DO_4OP_F(vfmadd_s, 32, uint32_t, W, float32_muladd, 0)
DO_4OP_F(vfmadd_d, 64, uint64_t, D, float64_muladd, 0)
DO_4OP_F(vfmsub_s, 32, uint32_t, W, float32_muladd, float_muladd_negate_c)
DO_4OP_F(vfmsub_d, 64, uint64_t, D, float64_muladd, float_muladd_negate_c)
DO_4OP_F(vfnmadd_s, 32, uint32_t, W, float32_muladd, float_muladd_negate_result)
DO_4OP_F(vfnmadd_d, 64, uint64_t, D, float64_muladd, float_muladd_negate_result)
DO_4OP_F(vfnmsub_s, 32, uint32_t, W, float32_muladd,
         float_muladd_negate_c | float_muladd_negate_result)
DO_4OP_F(vfnmsub_d, 64, uint64_t, D, float64_muladd,
         float_muladd_negate_c | float_muladd_negate_result)

#define DO_2OP_F(NAME, BIT, T, E, FN)                               \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    vec_clear_cause(env);                                           \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E(i) = FN(env, (T)Vj->E(i));                            \
    }                                                               \
}

#define FLOGB(BIT, T)                                            \
static T do_flogb_## BIT(CPULoongArchState *env, T fj)           \
{                                                                \
    T fp, fd;                                                    \
    float_status *status = &env->fp_status;                      \
    FloatRoundMode old_mode = get_float_rounding_mode(status);   \
                                                                 \
    set_float_rounding_mode(float_round_down, status);           \
    fp = float ## BIT ##_log2(fj, status);                       \
    fd = float ## BIT ##_round_to_int(fp, status);               \
    set_float_rounding_mode(old_mode, status);                   \
    vec_update_fcsr0_mask(env, GETPC(), float_flag_inexact);     \
    return fd;                                                   \
}

FLOGB(32, uint32_t)
FLOGB(64, uint64_t)

#define FCLASS(NAME, BIT, T, E, FN)                                 \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E(i) = FN(env, (T)Vj->E(i));                            \
    }                                                               \
}

FCLASS(vfclass_s, 32, uint32_t, W, helper_fclass_s)
FCLASS(vfclass_d, 64, uint64_t, D, helper_fclass_d)

#define FSQRT(BIT, T)                                  \
static T do_fsqrt_## BIT(CPULoongArchState *env, T fj) \
{                                                      \
    T fd;                                              \
    fd = float ## BIT ##_sqrt(fj, &env->fp_status);    \
    vec_update_fcsr0(env, GETPC());                    \
    return fd;                                         \
}

FSQRT(32, uint32_t)
FSQRT(64, uint64_t)

#define FRECIP(BIT, T)                                                  \
static T do_frecip_## BIT(CPULoongArchState *env, T fj)                 \
{                                                                       \
    T fd;                                                               \
    fd = float ## BIT ##_div(float ## BIT ##_one, fj, &env->fp_status); \
    vec_update_fcsr0(env, GETPC());                                     \
    return fd;                                                          \
}

FRECIP(32, uint32_t)
FRECIP(64, uint64_t)

#define FRSQRT(BIT, T)                                                  \
static T do_frsqrt_## BIT(CPULoongArchState *env, T fj)                 \
{                                                                       \
    T fd, fp;                                                           \
    fp = float ## BIT ##_sqrt(fj, &env->fp_status);                     \
    fd = float ## BIT ##_div(float ## BIT ##_one, fp, &env->fp_status); \
    vec_update_fcsr0(env, GETPC());                                     \
    return fd;                                                          \
}

FRSQRT(32, uint32_t)
FRSQRT(64, uint64_t)

DO_2OP_F(vflogb_s, 32, uint32_t, W, do_flogb_32)
DO_2OP_F(vflogb_d, 64, uint64_t, D, do_flogb_64)
DO_2OP_F(vfsqrt_s, 32, uint32_t, W, do_fsqrt_32)
DO_2OP_F(vfsqrt_d, 64, uint64_t, D, do_fsqrt_64)
DO_2OP_F(vfrecip_s, 32, uint32_t, W, do_frecip_32)
DO_2OP_F(vfrecip_d, 64, uint64_t, D, do_frecip_64)
DO_2OP_F(vfrsqrt_s, 32, uint32_t, W, do_frsqrt_32)
DO_2OP_F(vfrsqrt_d, 64, uint64_t, D, do_frsqrt_64)

static uint32_t float16_cvt_float32(int16_t h, float_status *status)
{
    uint32_t t;
    t = float16_to_float32((uint16_t)h, true, status);
    return  h < 0 ? (t | (1 << 31)) : t;
}
static uint64_t float32_cvt_float64(int32_t s, float_status *status)
{
    uint64_t t;
    t = float32_to_float64((uint32_t)s, status);
    return s < 0 ? (t | (1ULL << 63)) : t;
}

static uint16_t float32_cvt_float16(int32_t s, float_status *status)
{
    uint16_t t;
    t = float32_to_float16((uint32_t)s, true, status);
    return s < 0 ? (t | (1 << 15)) : t;
}
static uint32_t float64_cvt_float32(int64_t d, float_status *status)
{
    uint32_t t;
    t = float64_to_float32((uint64_t)d, status);
    return d < 0 ? (t | (1ULL << 63)) : t;
}

void HELPER(vfcvtl_s_h)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    vec_clear_cause(env);
    for (i = 0; i < LSX_LEN/32; i++) {
        temp.W(i) = float16_cvt_float32(Vj->H(i), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

void HELPER(vfcvtl_d_s)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    vec_clear_cause(env);
    for (i = 0; i < LSX_LEN/64; i++) {
        temp.D(i) = float32_cvt_float64(Vj->W(i), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

void HELPER(vfcvth_s_h)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    vec_clear_cause(env);
    for (i = 0; i < LSX_LEN/32; i++) {
        temp.W(i) = float16_cvt_float32(Vj->H(i + 4), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

void HELPER(vfcvth_d_s)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    vec_clear_cause(env);
    for (i = 0; i < LSX_LEN/64; i++) {
        temp.D(i) = float32_cvt_float64(Vj->W(i + 2), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

void HELPER(vfcvt_h_s)(CPULoongArchState *env,
                       uint32_t vd, uint32_t vj, uint32_t vk)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    vec_clear_cause(env);
    for(i = 0; i < LSX_LEN/32; i++) {
        temp.H(i + 4) = float32_cvt_float16(Vj->W(i), &env->fp_status);
        temp.H(i)  = float32_cvt_float16(Vk->W(i), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

void HELPER(vfcvt_s_d)(CPULoongArchState *env,
                       uint32_t vd, uint32_t vj, uint32_t vk)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    vec_clear_cause(env);
    for(i = 0; i < LSX_LEN/64; i++) {
        temp.W(i + 2) = float64_cvt_float32(Vj->D(i), &env->fp_status);
        temp.W(i)  = float64_cvt_float32(Vk->D(i), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

#define FCVT_2OP(NAME, BIT, T, E, FN)                               \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    vec_clear_cause(env);                                           \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E(i) = FN((T)Vj->E(i), &env->fp_status);                \
        vec_update_fcsr0(env, GETPC());                             \
    }                                                               \
}

FCVT_2OP(vfrint_s, 32, uint32_t, W, float32_round_to_int)
FCVT_2OP(vfrint_d, 64, uint64_t, D, float64_round_to_int)
FCVT_2OP(vfrintrne_s, 32, uint32_t, W, float32_round_to_int_rne)
FCVT_2OP(vfrintrne_d, 64, uint64_t, D, float64_round_to_int_rne)
FCVT_2OP(vfrintrz_s, 32, uint32_t, W, float32_round_to_int_rz)
FCVT_2OP(vfrintrz_d, 64, uint64_t, D, float64_round_to_int_rz)
FCVT_2OP(vfrintrp_s, 32, uint32_t, W, float32_round_to_int_rp)
FCVT_2OP(vfrintrp_d, 64, uint64_t, D, float64_round_to_int_rp)
FCVT_2OP(vfrintrm_s, 32, uint32_t, W, float32_round_to_int_rm)
FCVT_2OP(vfrintrm_d, 64, uint64_t, D, float64_round_to_int_rm)

#define FTINT(NAME, FMT1, FMT2, T1, T2,  MODE)                          \
static T2 do_ftint ## NAME(CPULoongArchState *env, T1 fj)               \
{                                                                       \
    T2 fd;                                                              \
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status); \
                                                                        \
    set_float_rounding_mode(MODE, &env->fp_status);                     \
    fd = do_## FMT1 ##_to_## FMT2(env, fj);                             \
    set_float_rounding_mode(old_mode, &env->fp_status);                 \
    return fd;                                                          \
}

#define DO_FTINT(FMT1, FMT2, T1, T2)                                         \
static T2 do_## FMT1 ##_to_## FMT2(CPULoongArchState *env, T1 fj)            \
{                                                                            \
    T2 fd;                                                                   \
                                                                             \
    fd = FMT1 ##_to_## FMT2(fj, &env->fp_status);                            \
    if (get_float_exception_flags(&env->fp_status) & (float_flag_invalid)) { \
        if (FMT1 ##_is_any_nan(fj)) {                                        \
            fd = 0;                                                          \
        }                                                                    \
    }                                                                        \
    vec_update_fcsr0(env, GETPC());                                          \
    return fd;                                                               \
}

DO_FTINT(float32, int32, uint32_t, uint32_t)
DO_FTINT(float64, int64, uint64_t, uint64_t)
DO_FTINT(float32, uint32, uint32_t, uint32_t)
DO_FTINT(float64, uint64, uint64_t, uint64_t)
DO_FTINT(float64, int32, uint64_t, uint32_t)
DO_FTINT(float32, int64, uint32_t, uint64_t)

FTINT(rne_w_s, float32, int32, uint32_t, uint32_t, float_round_nearest_even)
FTINT(rne_l_d, float64, int64, uint64_t, uint64_t, float_round_nearest_even)
FTINT(rp_w_s, float32, int32, uint32_t, uint32_t, float_round_up)
FTINT(rp_l_d, float64, int64, uint64_t, uint64_t, float_round_up)
FTINT(rz_w_s, float32, int32, uint32_t, uint32_t, float_round_to_zero)
FTINT(rz_l_d, float64, int64, uint64_t, uint64_t, float_round_to_zero)
FTINT(rm_w_s, float32, int32, uint32_t, uint32_t, float_round_down)
FTINT(rm_l_d, float64, int64, uint64_t, uint64_t, float_round_down)

DO_2OP_F(vftintrne_w_s, 32, uint32_t, W, do_ftintrne_w_s)
DO_2OP_F(vftintrne_l_d, 64, uint64_t, D, do_ftintrne_l_d)
DO_2OP_F(vftintrp_w_s, 32, uint32_t, W, do_ftintrp_w_s)
DO_2OP_F(vftintrp_l_d, 64, uint64_t, D, do_ftintrp_l_d)
DO_2OP_F(vftintrz_w_s, 32, uint32_t, W, do_ftintrz_w_s)
DO_2OP_F(vftintrz_l_d, 64, uint64_t, D, do_ftintrz_l_d)
DO_2OP_F(vftintrm_w_s, 32, uint32_t, W, do_ftintrm_w_s)
DO_2OP_F(vftintrm_l_d, 64, uint64_t, D, do_ftintrm_l_d)
DO_2OP_F(vftint_w_s, 32, uint32_t, W, do_float32_to_int32)
DO_2OP_F(vftint_l_d, 64, uint64_t, D, do_float64_to_int64)

FTINT(rz_wu_s, float32, uint32, uint32_t, uint32_t, float_round_to_zero)
FTINT(rz_lu_d, float64, uint64, uint64_t, uint64_t, float_round_to_zero)

DO_2OP_F(vftintrz_wu_s, 32, uint32_t, W, do_ftintrz_wu_s)
DO_2OP_F(vftintrz_lu_d, 64, uint64_t, D, do_ftintrz_lu_d)
DO_2OP_F(vftint_wu_s, 32, uint32_t, W, do_float32_to_uint32)
DO_2OP_F(vftint_lu_d, 64, uint64_t, D, do_float64_to_uint64)

FTINT(rm_w_d, float64, int32, uint64_t, uint32_t, float_round_down)
FTINT(rp_w_d, float64, int32, uint64_t, uint32_t, float_round_up)
FTINT(rz_w_d, float64, int32, uint64_t, uint32_t, float_round_to_zero)
FTINT(rne_w_d, float64, int32, uint64_t, uint32_t, float_round_nearest_even)

#define FTINT_W_D(NAME, FN)                              \
void HELPER(NAME)(CPULoongArchState *env,                \
                  uint32_t vd, uint32_t vj, uint32_t vk) \
{                                                        \
    int i;                                               \
    VReg temp;                                           \
    VReg *Vd = &(env->fpr[vd].vreg);                     \
    VReg *Vj = &(env->fpr[vj].vreg);                     \
    VReg *Vk = &(env->fpr[vk].vreg);                     \
                                                         \
    vec_clear_cause(env);                                \
    for (i = 0; i < 2; i++) {                            \
        temp.W(i + 2) = FN(env, (uint64_t)Vj->D(i));     \
        temp.W(i) = FN(env, (uint64_t)Vk->D(i));         \
    }                                                    \
    Vd->D(0) = temp.D(0);                                \
    Vd->D(1) = temp.D(1);                                \
}

FTINT_W_D(vftint_w_d, do_float64_to_int32)
FTINT_W_D(vftintrm_w_d, do_ftintrm_w_d)
FTINT_W_D(vftintrp_w_d, do_ftintrp_w_d)
FTINT_W_D(vftintrz_w_d, do_ftintrz_w_d)
FTINT_W_D(vftintrne_w_d, do_ftintrne_w_d)

FTINT(rml_l_s, float32, int64, uint32_t, uint64_t, float_round_down)
FTINT(rpl_l_s, float32, int64, uint32_t, uint64_t, float_round_up)
FTINT(rzl_l_s, float32, int64, uint32_t, uint64_t, float_round_to_zero)
FTINT(rnel_l_s, float32, int64, uint32_t, uint64_t, float_round_nearest_even)
FTINT(rmh_l_s, float32, int64, uint32_t, uint64_t, float_round_down)
FTINT(rph_l_s, float32, int64, uint32_t, uint64_t, float_round_up)
FTINT(rzh_l_s, float32, int64, uint32_t, uint64_t, float_round_to_zero)
FTINT(rneh_l_s, float32, int64, uint32_t, uint64_t, float_round_nearest_even)

#define FTINTL_L_S(NAME, FN)                                        \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg temp;                                                      \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    vec_clear_cause(env);                                           \
    for (i = 0; i < 2; i++) {                                       \
        temp.D(i) = FN(env, (uint32_t)Vj->W(i));                    \
    }                                                               \
    Vd->D(0) = temp.D(0);                                           \
    Vd->D(1) = temp.D(1);                                           \
}

FTINTL_L_S(vftintl_l_s, do_float32_to_int64)
FTINTL_L_S(vftintrml_l_s, do_ftintrml_l_s)
FTINTL_L_S(vftintrpl_l_s, do_ftintrpl_l_s)
FTINTL_L_S(vftintrzl_l_s, do_ftintrzl_l_s)
FTINTL_L_S(vftintrnel_l_s, do_ftintrnel_l_s)

#define FTINTH_L_S(NAME, FN)                                        \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg temp;                                                      \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    vec_clear_cause(env);                                           \
    for (i = 0; i < 2; i++) {                                       \
        temp.D(i) = FN(env, (uint32_t)Vj->W(i + 2));                \
    }                                                               \
    Vd->D(0) = temp.D(0);                                           \
    Vd->D(1) = temp.D(1);                                           \
}

FTINTH_L_S(vftinth_l_s, do_float32_to_int64)
FTINTH_L_S(vftintrmh_l_s, do_ftintrmh_l_s)
FTINTH_L_S(vftintrph_l_s, do_ftintrph_l_s)
FTINTH_L_S(vftintrzh_l_s, do_ftintrzh_l_s)
FTINTH_L_S(vftintrneh_l_s, do_ftintrneh_l_s)

#define FFINT(NAME, FMT1, FMT2, T1, T2)                    \
static T2 do_ffint_ ## NAME(CPULoongArchState *env, T1 fj) \
{                                                          \
    T2 fd;                                                 \
                                                           \
    fd = FMT1 ##_to_## FMT2(fj, &env->fp_status);          \
    vec_update_fcsr0(env, GETPC());                        \
    return fd;                                             \
}

FFINT(s_w, int32, float32, int32_t, uint32_t)
FFINT(d_l, int64, float64, int64_t, uint64_t)
FFINT(s_wu, uint32, float32, uint32_t, uint32_t)
FFINT(d_lu, uint64, float64, uint64_t, uint64_t)

DO_2OP_F(vffint_s_w, 32, int32_t, W, do_ffint_s_w)
DO_2OP_F(vffint_d_l, 64, int64_t, D, do_ffint_d_l)
DO_2OP_F(vffint_s_wu, 32, uint32_t, W, do_ffint_s_wu)
DO_2OP_F(vffint_d_lu, 64, uint64_t, D, do_ffint_d_lu)

void HELPER(vffintl_d_w)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    vec_clear_cause(env);
    for (i = 0; i < 2; i++) {
        temp.D(i) = int32_to_float64(Vj->W(i), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

void HELPER(vffinth_d_w)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    vec_clear_cause(env);
    for (i = 0; i < 2; i++) {
        temp.D(i) = int32_to_float64(Vj->W(i + 2), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

void HELPER(vffint_s_l)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t vk)
{
    int i;
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    vec_clear_cause(env);
    for (i = 0; i < 2; i++) {
        temp.W(i + 2) = int64_to_float32(Vj->D(i), &env->fp_status);
        temp.W(i) = int64_to_float32(Vk->D(i), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
    Vd->D(0) = temp.D(0);
    Vd->D(1) = temp.D(1);
}

#define VSEQ(a, b) (a == b ? -1 : 0)
#define VSLE(a, b) (a <= b ? -1 : 0)
#define VSLT(a, b) (a < b ? -1 : 0)

#define VCMPI(NAME, BIT, T, E, DO_OP)                           \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t v) \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = DO_OP((T)Vj->E(i), (T)imm);                  \
    }                                                           \
}

VCMPI(vseqi_b, 8, int8_t, B, VSEQ)
VCMPI(vseqi_h, 16, int16_t, H, VSEQ)
VCMPI(vseqi_w, 32, int32_t, W, VSEQ)
VCMPI(vseqi_d, 64, int64_t, D, VSEQ)
VCMPI(vslei_b, 8, int8_t, B, VSLE)
VCMPI(vslei_h, 16, int16_t, H, VSLE)
VCMPI(vslei_w, 32, int32_t, W, VSLE)
VCMPI(vslei_d, 64, int64_t, D, VSLE)
VCMPI(vslei_bu, 8, uint8_t, B, VSLE)
VCMPI(vslei_hu, 16, uint16_t, H, VSLE)
VCMPI(vslei_wu, 32, uint32_t, W, VSLE)
VCMPI(vslei_du, 64, uint64_t, D, VSLE)
VCMPI(vslti_b, 8, int8_t, B, VSLT)
VCMPI(vslti_h, 16, int16_t, H, VSLT)
VCMPI(vslti_w, 32, int32_t, W, VSLT)
VCMPI(vslti_d, 64, int64_t, D, VSLT)
VCMPI(vslti_bu, 8, uint8_t, B, VSLT)
VCMPI(vslti_hu, 16, uint16_t, H, VSLT)
VCMPI(vslti_wu, 32, uint32_t, W, VSLT)
VCMPI(vslti_du, 64, uint64_t, D, VSLT)

static uint64_t vfcmp_common(CPULoongArchState *env,
                             FloatRelation cmp, uint32_t flags)
{
    bool ret;

    switch (cmp) {
    case float_relation_less:
        ret = (flags & FCMP_LT);
        break;
    case float_relation_equal:
        ret = (flags & FCMP_EQ);
        break;
    case float_relation_greater:
        ret = (flags & FCMP_GT);
        break;
    case float_relation_unordered:
        ret = (flags & FCMP_UN);
        break;
    default:
        g_assert_not_reached();
    }

    return ret;
}

#define VFCMP(NAME, BIT, T, E, FN)                                       \
void HELPER(NAME)(CPULoongArchState *env,                                \
                  uint32_t vd, uint32_t vj, uint32_t vk, uint32_t flags) \
{                                                                        \
    int i;                                                               \
    VReg t;                                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                                     \
    VReg *Vj = &(env->fpr[vj].vreg);                                     \
    VReg *Vk = &(env->fpr[vk].vreg);                                     \
                                                                         \
    vec_clear_cause(env);                                                \
    for (i = 0; i < LSX_LEN/BIT ; i++) {                                 \
        FloatRelation cmp;                                               \
        cmp = FN(Vj->E(i), Vk->E(i), &env->fp_status);                   \
        t.E(i) = (vfcmp_common(env, cmp, flags)) ? -1 : 0;               \
        vec_update_fcsr0(env, GETPC());                                  \
    }                                                                    \
    Vd->D(0) = t.D(0);                                                   \
    Vd->D(1) = t.D(1);                                                   \
}

VFCMP(vfcmp_c_s, 32, uint32_t, W, float32_compare_quiet)
VFCMP(vfcmp_s_s, 32, uint32_t, W, float32_compare)
VFCMP(vfcmp_c_d, 64, uint64_t, D, float64_compare_quiet)
VFCMP(vfcmp_s_d, 64, uint64_t, D, float64_compare)

void HELPER(vbitseli_b)(void *vd, void *vj,  uint64_t imm, uint32_t v)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    for (i = 0; i < 16; i++) {
        Vd->B(i) = (~Vd->B(i) & Vj->B(i)) | (Vd->B(i) & imm);
    }
}

void HELPER(vseteqz_v)(CPULoongArchState *env, uint32_t cd, uint32_t vj)
{
    VReg *Vj = &(env->fpr[vj].vreg);
    env->cf[cd & 0x7] = (Vj->Q(0) == 0);
}

void HELPER(vsetnez_v)(CPULoongArchState *env, uint32_t cd, uint32_t vj)
{
    VReg *Vj = &(env->fpr[vj].vreg);
    env->cf[cd & 0x7] = (Vj->Q(0) != 0);
}

#define SETANYEQZ(NAME, BIT, E)                                     \
void HELPER(NAME)(CPULoongArchState *env, uint32_t cd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    bool ret = false;                                               \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        ret |= (Vj->E(i) == 0);                                     \
    }                                                               \
    env->cf[cd & 0x7] = ret;                                        \
}
SETANYEQZ(vsetanyeqz_b, 8, B)
SETANYEQZ(vsetanyeqz_h, 16, H)
SETANYEQZ(vsetanyeqz_w, 32, W)
SETANYEQZ(vsetanyeqz_d, 64, D)

#define SETALLNEZ(NAME, BIT, E)                                     \
void HELPER(NAME)(CPULoongArchState *env, uint32_t cd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    bool ret = true;                                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        ret &= (Vj->E(i) != 0);                                     \
    }                                                               \
    env->cf[cd & 0x7] = ret;                                        \
}
SETALLNEZ(vsetallnez_b, 8, B)
SETALLNEZ(vsetallnez_h, 16, H)
SETALLNEZ(vsetallnez_w, 32, W)
SETALLNEZ(vsetallnez_d, 64, D)
