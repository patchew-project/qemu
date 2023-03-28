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
