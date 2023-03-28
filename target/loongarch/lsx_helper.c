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
