/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch LSX helper functions.
 *
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

#define DO_HELPER_VVV(NAME, BIT, FUNC, ...)                   \
    void helper_##NAME(CPULoongArchState *env,                \
                       uint32_t vd, uint32_t vj, uint32_t vk) \
    { FUNC(env, vd, vj, vk, BIT, __VA_ARGS__); }

#define DO_HELPER_VV_I(NAME, BIT, FUNC, ...)                   \
    void helper_##NAME(CPULoongArchState *env,                 \
                       uint32_t vd, uint32_t vj, uint32_t imm) \
    { FUNC(env, vd, vj, imm, BIT, __VA_ARGS__ ); }

#define DO_HELPER_VV(NAME, BIT, FUNC, ...)                               \
    void helper_##NAME(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
    { FUNC(env, vd, vj, BIT, __VA_ARGS__); }

static void helper_vvv(CPULoongArchState *env,
                       uint32_t vd, uint32_t vj, uint32_t vk, int bit,
                       void (*func)(vec_t*, vec_t*, vec_t*, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t *Vk = &(env->fpr[vk].vec);

    for (i = 0; i < LSX_LEN/bit; i++) {
        func(Vd, Vj, Vk, bit, i);
    }
}

static  void helper_vv_i(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm, int bit,
                         void (*func)(vec_t*, vec_t*, uint32_t, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    for (i = 0; i < LSX_LEN/bit; i++) {
        func(Vd, Vj, imm, bit, i);
    }
}

static void helper_vv(CPULoongArchState *env,
                      uint32_t vd, uint32_t vj, int bit,
                      void (*func)(vec_t*, vec_t*, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    for (i = 0; i < LSX_LEN/bit; i++) {
        func(Vd, Vj, bit, i);
    }
}

static void do_vadd(vec_t *Vd, vec_t *Vj, vec_t *Vk,  int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] + Vk->B[n];
        break;
    case 16:
        Vd->H[n] = Vj->H[n] + Vk->H[n];
        break;
    case 32:
        Vd->W[n] = Vj->W[n] + Vk->W[n];
        break;
    case 64:
        Vd->D[n] = Vj->D[n] + Vk->D[n];
        break;
    case 128:
        Vd->Q[n] = Vj->Q[n] + Vk->Q[n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsub(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] - Vk->B[n];
        break;
    case 16:
        Vd->H[n] = Vj->H[n] - Vk->H[n];
        break;
    case 32:
        Vd->W[n] = Vj->W[n] - Vk->W[n];
        break;
    case 64:
        Vd->D[n] = Vj->D[n] - Vk->D[n];
        break;
    case 128:
        Vd->Q[n] = Vj->Q[n] - Vk->Q[n];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vadd_b, 8, helper_vvv, do_vadd)
DO_HELPER_VVV(vadd_h, 16, helper_vvv, do_vadd)
DO_HELPER_VVV(vadd_w, 32, helper_vvv, do_vadd)
DO_HELPER_VVV(vadd_d, 64, helper_vvv, do_vadd)
DO_HELPER_VVV(vadd_q, 128, helper_vvv, do_vadd)
DO_HELPER_VVV(vsub_b, 8, helper_vvv, do_vsub)
DO_HELPER_VVV(vsub_h, 16, helper_vvv, do_vsub)
DO_HELPER_VVV(vsub_w, 32, helper_vvv, do_vsub)
DO_HELPER_VVV(vsub_d, 64, helper_vvv, do_vsub)
DO_HELPER_VVV(vsub_q, 128, helper_vvv, do_vsub)

static void do_vaddi(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] + imm;
        break;
    case 16:
        Vd->H[n] = Vj->H[n] + imm;
        break;
    case 32:
        Vd->W[n] = Vj->W[n] + imm;
        break;
    case 64:
        Vd->D[n] = Vj->D[n] + imm;
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsubi(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] - imm;
        break;
    case 16:
        Vd->H[n] = Vj->H[n] - imm;
        break;
    case 32:
        Vd->W[n] = Vj->W[n] - imm;
        break;
    case 64:
        Vd->D[n] = Vd->D[n] - imm;
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_I(vaddi_bu, 8, helper_vv_i, do_vaddi)
DO_HELPER_VV_I(vaddi_hu, 16, helper_vv_i, do_vaddi)
DO_HELPER_VV_I(vaddi_wu, 32, helper_vv_i, do_vaddi)
DO_HELPER_VV_I(vaddi_du, 64, helper_vv_i, do_vaddi)
DO_HELPER_VV_I(vsubi_bu, 8, helper_vv_i, do_vsubi)
DO_HELPER_VV_I(vsubi_hu, 16, helper_vv_i, do_vsubi)
DO_HELPER_VV_I(vsubi_wu, 32, helper_vv_i, do_vsubi)
DO_HELPER_VV_I(vsubi_du, 64, helper_vv_i, do_vsubi)

static void do_vneg(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = -Vj->B[n];
        break;
    case 16:
        Vd->H[n] = -Vj->H[n];
        break;
    case 32:
        Vd->W[n] = -Vj->W[n];
        break;
    case 64:
        Vd->D[n] = -Vj->D[n];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV(vneg_b, 8, helper_vv, do_vneg)
DO_HELPER_VV(vneg_h, 16, helper_vv, do_vneg)
DO_HELPER_VV(vneg_w, 32, helper_vv, do_vneg)
DO_HELPER_VV(vneg_d, 64, helper_vv, do_vneg)

static int64_t s_add_s(int64_t s1, int64_t s2, int bit)
{
    int64_t smax = MAKE_64BIT_MASK(0, (bit -1));
    int64_t smin = MAKE_64BIT_MASK((bit -1), 64);

    if (s1 < 0) {
        return (smin - s1 < s2) ? s1 + s2 : smin;
    } else {
        return (s2 < smax - s1) ? s1 + s2 : smax;
    }
}

static void do_vsadd(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = s_add_s(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = s_add_s(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = s_add_s(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = s_add_s(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t u_add_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;

    return (u1 <  umax - u2) ? u1 + u2 : umax;
}

static void do_vsadd_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = u_add_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = u_add_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = u_add_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = u_add_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static int64_t s_sub_s(int64_t s1, int64_t s2, int bit)
{
    int64_t smax = MAKE_64BIT_MASK(0, (bit -1));
    int64_t smin = MAKE_64BIT_MASK((bit -1), 64);

    if (s2 > 0) {
        return (smin + s2 < s1) ? s1 - s2 : smin;
    } else {
        return (s1 < smax + s2) ? s1 - s2 : smax;
    }
}

static void do_vssub(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = s_sub_s(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = s_sub_s(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = s_sub_s(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = s_sub_s(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t u_sub_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t u1 = s1 & MAKE_64BIT_MASK(0, bit);
    uint64_t u2 = s2 & MAKE_64BIT_MASK(0, bit);

    return (u1 > u2) ?  u1 -u2 : 0;
}

static void do_vssub_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = u_sub_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = u_sub_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = u_sub_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = u_sub_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsadd_b, 8, helper_vvv, do_vsadd)
DO_HELPER_VVV(vsadd_h, 16, helper_vvv, do_vsadd)
DO_HELPER_VVV(vsadd_w, 32, helper_vvv, do_vsadd)
DO_HELPER_VVV(vsadd_d, 64, helper_vvv, do_vsadd)
DO_HELPER_VVV(vsadd_bu, 8, helper_vvv, do_vsadd_u)
DO_HELPER_VVV(vsadd_hu, 16, helper_vvv, do_vsadd_u)
DO_HELPER_VVV(vsadd_wu, 32, helper_vvv, do_vsadd_u)
DO_HELPER_VVV(vsadd_du, 64, helper_vvv, do_vsadd_u)
DO_HELPER_VVV(vssub_b, 8, helper_vvv, do_vssub)
DO_HELPER_VVV(vssub_h, 16, helper_vvv, do_vssub)
DO_HELPER_VVV(vssub_w, 32, helper_vvv, do_vssub)
DO_HELPER_VVV(vssub_d, 64, helper_vvv, do_vssub)
DO_HELPER_VVV(vssub_bu, 8, helper_vvv, do_vssub_u)
DO_HELPER_VVV(vssub_hu, 16, helper_vvv, do_vssub_u)
DO_HELPER_VVV(vssub_wu, 32, helper_vvv, do_vssub_u)
DO_HELPER_VVV(vssub_du, 64, helper_vvv, do_vssub_u)

#define S_EVEN(a, bit) \
        ((((int64_t)(a)) << (64 - bit / 2)) >> (64 - bit / 2))

#define U_EVEN(a, bit) \
        ((((uint64_t)(a)) << (64 - bit / 2)) >> (64 - bit / 2))

#define S_ODD(a, bit) \
        ((((int64_t)(a)) << (64 - bit)) >> (64 - bit/ 2))

#define U_ODD(a, bit) \
        ((((uint64_t)(a)) << (64 - bit)) >> (64 - bit / 2))

#define S_EVEN_Q(a, bit) \
        ((((__int128)(a)) << (128 - bit / 2)) >> (128 - bit / 2))

#define U_EVEN_Q(a, bit) \
        ((((unsigned __int128)(a)) << (128 - bit / 2)) >> (128 - bit / 2))

#define S_ODD_Q(a, bit) \
        ((((__int128)(a)) << (128 - bit)) >> (128 - bit/ 2))

#define U_ODD_Q(a, bit) \
        ((((unsigned __int128)(a)) << (128 - bit)) >> (128 - bit / 2))

static int64_t s_haddw_s(int64_t s1, int64_t s2,  int bit)
{
    return S_ODD(s1, bit) + S_EVEN(s2, bit);
}

static void do_vhaddw_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = s_haddw_s(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = s_haddw_s(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = s_haddw_s(Vj->D[n], Vk->D[n], bit);
        break;
    case 128:
        Vd->Q[n] = S_ODD_Q(Vj->Q[n], bit) + S_EVEN_Q(Vk->Q[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t u_haddw_u(int64_t s1, int64_t s2, int bit)
{
    return U_ODD(s1, bit) + U_EVEN(s2, bit);
}

static void do_vhaddw_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = u_haddw_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = u_haddw_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = u_haddw_u(Vj->D[n], Vk->D[n], bit);
        break;
    case 128:
        Vd->Q[n] = U_ODD_Q(Vj->Q[n], bit) + U_EVEN_Q(Vk->Q[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static int64_t s_hsubw_s(int64_t s1, int64_t s2, int bit)
{
    return S_ODD(s1, bit) - S_EVEN(s2, bit);
}

static void do_vhsubw_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = s_hsubw_s(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = s_hsubw_s(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = s_hsubw_s(Vj->D[n], Vk->D[n], bit);
        break;
    case 128:
        Vd->Q[n] = S_ODD_Q(Vj->Q[n], bit) - S_EVEN_Q(Vk->Q[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t u_hsubw_u(int64_t s1, int64_t s2, int bit)
{
    return U_ODD(s1, bit) - U_EVEN(s2, bit);
}

static void do_vhsubw_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = u_hsubw_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = u_hsubw_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = u_hsubw_u(Vj->D[n], Vk->D[n], bit);
        break;
    case 128:
        Vd->Q[n] = U_ODD_Q(Vj->Q[n], bit) - U_EVEN_Q(Vk->Q[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vhaddw_h_b, 16, helper_vvv, do_vhaddw_s)
DO_HELPER_VVV(vhaddw_w_h, 32, helper_vvv, do_vhaddw_s)
DO_HELPER_VVV(vhaddw_d_w, 64, helper_vvv, do_vhaddw_s)
DO_HELPER_VVV(vhaddw_q_d, 128, helper_vvv, do_vhaddw_s)
DO_HELPER_VVV(vhaddw_hu_bu, 16, helper_vvv, do_vhaddw_u)
DO_HELPER_VVV(vhaddw_wu_hu, 32, helper_vvv, do_vhaddw_u)
DO_HELPER_VVV(vhaddw_du_wu, 64, helper_vvv, do_vhaddw_u)
DO_HELPER_VVV(vhaddw_qu_du, 128, helper_vvv, do_vhaddw_u)
DO_HELPER_VVV(vhsubw_h_b, 16, helper_vvv, do_vhsubw_s)
DO_HELPER_VVV(vhsubw_w_h, 32, helper_vvv, do_vhsubw_s)
DO_HELPER_VVV(vhsubw_d_w, 64, helper_vvv, do_vhsubw_s)
DO_HELPER_VVV(vhsubw_q_d, 128, helper_vvv, do_vhsubw_s)
DO_HELPER_VVV(vhsubw_hu_bu, 16, helper_vvv, do_vhsubw_u)
DO_HELPER_VVV(vhsubw_wu_hu, 32, helper_vvv, do_vhsubw_u)
DO_HELPER_VVV(vhsubw_du_wu, 64, helper_vvv, do_vhsubw_u)
DO_HELPER_VVV(vhsubw_qu_du, 128, helper_vvv, do_vhsubw_u)

static void do_vaddwev_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (int16_t)Vj->B[2 * n] + (int16_t)Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] = (int32_t)Vj->H[2 * n] + (int32_t)Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] = (int64_t)Vj->W[2 * n] + (int64_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] = (__int128)Vj->D[2 * n] + (__int128)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vaddwod_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (int16_t)Vj->B[2 * n + 1] + (int16_t)Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] = (int32_t)Vj->H[2 * n + 1] + (int32_t)Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] = (int64_t)Vj->W[2 * n + 1] + (int64_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] = (__int128)Vj->D[2 * n + 1] + (__int128)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsubwev_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (int16_t)Vj->B[2 * n] - (int16_t)Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] = (int32_t)Vj->H[2 * n] - (int32_t)Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] = (int64_t)Vj->W[2 * n] - (int64_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] = (__int128)Vj->D[2 * n] - (__int128)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsubwod_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (int16_t)Vj->B[2 * n + 1] - (int16_t)Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] = (int32_t)Vj->H[2 * n + 1] - (int32_t)Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] = (int64_t)Vj->W[2 * n + 1] - (int64_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] = (__int128)Vj->D[2 * n + 1] - (__int128)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vaddwev_h_b, 16, helper_vvv, do_vaddwev_s)
DO_HELPER_VVV(vaddwev_w_h, 32, helper_vvv, do_vaddwev_s)
DO_HELPER_VVV(vaddwev_d_w, 64, helper_vvv, do_vaddwev_s)
DO_HELPER_VVV(vaddwev_q_d, 128, helper_vvv, do_vaddwev_s)
DO_HELPER_VVV(vaddwod_h_b, 16, helper_vvv, do_vaddwod_s)
DO_HELPER_VVV(vaddwod_w_h, 32, helper_vvv, do_vaddwod_s)
DO_HELPER_VVV(vaddwod_d_w, 64, helper_vvv, do_vaddwod_s)
DO_HELPER_VVV(vaddwod_q_d, 128, helper_vvv, do_vaddwod_s)
DO_HELPER_VVV(vsubwev_h_b, 16, helper_vvv, do_vsubwev_s)
DO_HELPER_VVV(vsubwev_w_h, 32, helper_vvv, do_vsubwev_s)
DO_HELPER_VVV(vsubwev_d_w, 64, helper_vvv, do_vsubwev_s)
DO_HELPER_VVV(vsubwev_q_d, 128, helper_vvv, do_vsubwev_s)
DO_HELPER_VVV(vsubwod_h_b, 16, helper_vvv, do_vsubwod_s)
DO_HELPER_VVV(vsubwod_w_h, 32, helper_vvv, do_vsubwod_s)
DO_HELPER_VVV(vsubwod_d_w, 64, helper_vvv, do_vsubwod_s)
DO_HELPER_VVV(vsubwod_q_d, 128, helper_vvv, do_vsubwod_s)

static void do_vaddwev_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint16_t)(uint8_t)Vj->B[2 * n] + (uint16_t)(uint8_t)Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] = (uint32_t)(uint16_t)Vj->H[2 * n] + (uint32_t)(uint16_t)Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] = (uint64_t)(uint32_t)Vj->W[2 * n] + (uint64_t)(uint32_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] = (__uint128_t)(uint64_t)Vj->D[2 * n] + (__uint128_t)(uint64_t)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vaddwod_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint16_t)(uint8_t)Vj->B[2 * n + 1] + (uint16_t)(uint8_t)Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] = (uint32_t)(uint16_t)Vj->H[2 * n + 1] + (uint32_t)(uint16_t)Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] = (uint64_t)(uint32_t)Vj->W[2 * n + 1] + (uint64_t)(uint32_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] = (__uint128_t)(uint64_t)Vj->D[2 * n + 1] + (__uint128_t)(uint64_t )Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsubwev_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint16_t)(uint8_t)Vj->B[2 * n] - (uint16_t)(uint8_t)Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] = (uint32_t)(uint16_t)Vj->H[2 * n] - (uint32_t)(uint16_t)Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] = (uint64_t)(uint32_t)Vj->W[2 * n] - (uint64_t)(uint32_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] = (__uint128_t)(uint64_t)Vj->D[2 * n] - (__uint128_t)(uint64_t)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsubwod_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint16_t)(uint8_t)Vj->B[2 * n + 1] - (uint16_t)(uint8_t)Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] = (uint32_t)(uint16_t)Vj->H[2 * n + 1] - (uint32_t)(uint16_t)Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] = (uint64_t)(uint32_t)Vj->W[2 * n + 1] - (uint64_t)(uint32_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] = (__uint128_t)(uint64_t)Vj->D[2 * n + 1] - (__uint128_t)(uint64_t)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vaddwev_h_bu, 16, helper_vvv, do_vaddwev_u)
DO_HELPER_VVV(vaddwev_w_hu, 32, helper_vvv, do_vaddwev_u)
DO_HELPER_VVV(vaddwev_d_wu, 64, helper_vvv, do_vaddwev_u)
DO_HELPER_VVV(vaddwev_q_du, 128, helper_vvv, do_vaddwev_u)
DO_HELPER_VVV(vaddwod_h_bu, 16, helper_vvv, do_vaddwod_u)
DO_HELPER_VVV(vaddwod_w_hu, 32, helper_vvv, do_vaddwod_u)
DO_HELPER_VVV(vaddwod_d_wu, 64, helper_vvv, do_vaddwod_u)
DO_HELPER_VVV(vaddwod_q_du, 128, helper_vvv, do_vaddwod_u)
DO_HELPER_VVV(vsubwev_h_bu, 16, helper_vvv, do_vsubwev_u)
DO_HELPER_VVV(vsubwev_w_hu, 32, helper_vvv, do_vsubwev_u)
DO_HELPER_VVV(vsubwev_d_wu, 64, helper_vvv, do_vsubwev_u)
DO_HELPER_VVV(vsubwev_q_du, 128, helper_vvv, do_vsubwev_u)
DO_HELPER_VVV(vsubwod_h_bu, 16, helper_vvv, do_vsubwod_u)
DO_HELPER_VVV(vsubwod_w_hu, 32, helper_vvv, do_vsubwod_u)
DO_HELPER_VVV(vsubwod_d_wu, 64, helper_vvv, do_vsubwod_u)
DO_HELPER_VVV(vsubwod_q_du, 128, helper_vvv, do_vsubwod_u)

static void do_vaddwev_u_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint16_t)(uint8_t)Vj->B[2 * n] + (int16_t)Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] = (uint32_t)(uint16_t)Vj->H[2 * n] + (int32_t)Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] = (uint64_t)(uint32_t)Vj->W[2 * n] + (int64_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] = (__uint128_t)(uint64_t)Vj->D[2 * n] + (__int128)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vaddwod_u_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint16_t)(uint8_t)Vj->B[2 * n + 1] + (int16_t)Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] = (uint32_t)(uint16_t)Vj->H[2 * n + 1] + (int32_t)Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] = (uint64_t)(uint32_t)Vj->W[2 * n + 1] + (int64_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] = (__uint128_t)(uint64_t)Vj->D[2 * n + 1] + (__int128)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vaddwev_h_bu_b, 16, helper_vvv, do_vaddwev_u_s)
DO_HELPER_VVV(vaddwev_w_hu_h, 32, helper_vvv, do_vaddwev_u_s)
DO_HELPER_VVV(vaddwev_d_wu_w, 64, helper_vvv, do_vaddwev_u_s)
DO_HELPER_VVV(vaddwev_q_du_d, 128, helper_vvv, do_vaddwev_u_s)
DO_HELPER_VVV(vaddwod_h_bu_b, 16, helper_vvv, do_vaddwod_u_s)
DO_HELPER_VVV(vaddwod_w_hu_h, 32, helper_vvv, do_vaddwod_u_s)
DO_HELPER_VVV(vaddwod_d_wu_w, 64, helper_vvv, do_vaddwod_u_s)
DO_HELPER_VVV(vaddwod_q_du_d, 128, helper_vvv, do_vaddwod_u_s)


static int64_t vavg_s(int64_t s1, int64_t s2)
{
    return (s1 >> 1) + (s2 >> 1) + (s1 & s2 & 1);
}

static void do_vavg_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vavg_s(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vavg_s(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vavg_s(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vavg_s(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t vavg_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;
    return (u1 >> 1) + (u2 >> 1) + (u1 & u2 & 1);
}

static void do_vavg_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vavg_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vavg_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vavg_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vavg_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static int64_t vavgr_s(int64_t s1, int64_t s2)
{
    return (s1 >> 1) + (s2 >> 1) + ((s1 | s2) & 1);
}

static void do_vavgr_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vavgr_s(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vavgr_s(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vavgr_s(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vavgr_s(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t vavgr_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;

    return (u1 >> 1) + (u2 >> 1) + ((u1 | u2) & 1);
}

static void do_vavgr_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vavgr_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vavgr_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vavgr_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vavgr_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vavg_b, 8, helper_vvv, do_vavg_s)
DO_HELPER_VVV(vavg_h, 16, helper_vvv, do_vavg_s)
DO_HELPER_VVV(vavg_w, 32, helper_vvv, do_vavg_s)
DO_HELPER_VVV(vavg_d, 64, helper_vvv, do_vavg_s)
DO_HELPER_VVV(vavg_bu, 8, helper_vvv, do_vavg_u)
DO_HELPER_VVV(vavg_hu, 16, helper_vvv, do_vavg_u)
DO_HELPER_VVV(vavg_wu, 32, helper_vvv, do_vavg_u)
DO_HELPER_VVV(vavg_du, 64, helper_vvv, do_vavg_u)
DO_HELPER_VVV(vavgr_b, 8, helper_vvv, do_vavgr_s)
DO_HELPER_VVV(vavgr_h, 16, helper_vvv, do_vavgr_s)
DO_HELPER_VVV(vavgr_w, 32, helper_vvv, do_vavgr_s)
DO_HELPER_VVV(vavgr_d, 64, helper_vvv, do_vavgr_s)
DO_HELPER_VVV(vavgr_bu, 8, helper_vvv, do_vavgr_u)
DO_HELPER_VVV(vavgr_hu, 16, helper_vvv, do_vavgr_u)
DO_HELPER_VVV(vavgr_wu, 32, helper_vvv, do_vavgr_u)
DO_HELPER_VVV(vavgr_du, 64, helper_vvv, do_vavgr_u)

static int64_t vabsd_s(int64_t s1, int64_t s2)
{
    return s1 < s2 ? s2- s1 : s1 -s2;
}

static void do_vabsd_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vabsd_s(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vabsd_s(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vabsd_s(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vabsd_s(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t vabsd_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;

    return u1 < u2 ? u2 - u1 : u1 -u2;
}

static void do_vabsd_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vabsd_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vabsd_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vabsd_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vabsd_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vabsd_b, 8, helper_vvv, do_vabsd_s)
DO_HELPER_VVV(vabsd_h, 16, helper_vvv, do_vabsd_s)
DO_HELPER_VVV(vabsd_w, 32, helper_vvv, do_vabsd_s)
DO_HELPER_VVV(vabsd_d, 64, helper_vvv, do_vabsd_s)
DO_HELPER_VVV(vabsd_bu, 8, helper_vvv, do_vabsd_u)
DO_HELPER_VVV(vabsd_hu, 16, helper_vvv, do_vabsd_u)
DO_HELPER_VVV(vabsd_wu, 32, helper_vvv, do_vabsd_u)
DO_HELPER_VVV(vabsd_du, 64, helper_vvv, do_vabsd_u)

static int64_t vadda_s(int64_t s1, int64_t s2)
{
    int64_t abs_s1 = s1 >= 0 ? s1 : -s1;
    int64_t abs_s2 = s2 >= 0 ? s2 : -s2;
    return abs_s1 + abs_s2;
}

static void do_vadda_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vadda_s(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vadda_s(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vadda_s(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vadda_s(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vadda_b, 8, helper_vvv, do_vadda_s)
DO_HELPER_VVV(vadda_h, 16, helper_vvv, do_vadda_s)
DO_HELPER_VVV(vadda_w, 32, helper_vvv, do_vadda_s)
DO_HELPER_VVV(vadda_d, 64, helper_vvv, do_vadda_s)

static int64_t vmax_s(int64_t s1, int64_t s2)
{
    return s1 > s2 ? s1 : s2;
}

static void do_vmax_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vmax_s(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vmax_s(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vmax_s(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vmax_s(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmaxi_s(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit , int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vmax_s(Vj->B[n], imm);
        break;
    case 16:
        Vd->H[n] = vmax_s(Vj->H[n], imm);
        break;
    case 32:
        Vd->W[n] = vmax_s(Vj->W[n], imm);
        break;
    case 64:
        Vd->D[n] = vmax_s(Vj->D[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t vmax_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;
    return u1 > u2 ? u1 : u2;
}

static void do_vmax_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vmax_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vmax_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vmax_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vmax_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmaxi_u(vec_t *Vd, vec_t *Vj, uint32_t imm , int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vmax_u(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vmax_u(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vmax_u(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vmax_u(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static int64_t vmin_s(int64_t s1, int64_t s2)
{
    return s1 < s2 ? s1 : s2;
}

static void do_vmin_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vmin_s(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vmin_s(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vmin_s(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vmin_s(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmini_s(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vmin_s(Vj->B[n], imm);
        break;
    case 16:
        Vd->H[n] = vmin_s(Vj->H[n], imm);
        break;
    case 32:
        Vd->W[n] = vmin_s(Vj->W[n], imm);
        break;
    case 64:
        Vd->D[n] = vmin_s(Vj->D[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t vmin_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;
    return u1 < u2 ? u1 : u2;
}

static void do_vmin_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vmin_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vmin_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vmin_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vmin_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmini_u(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vmin_u(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vmin_u(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vmin_u(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vmin_u(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vmax_b, 8, helper_vvv, do_vmax_s)
DO_HELPER_VVV(vmax_h, 16, helper_vvv, do_vmax_s)
DO_HELPER_VVV(vmax_w, 32, helper_vvv, do_vmax_s)
DO_HELPER_VVV(vmax_d, 64, helper_vvv, do_vmax_s)
DO_HELPER_VV_I(vmaxi_b, 8, helper_vv_i, do_vmaxi_s)
DO_HELPER_VV_I(vmaxi_h, 16, helper_vv_i, do_vmaxi_s)
DO_HELPER_VV_I(vmaxi_w, 32, helper_vv_i, do_vmaxi_s)
DO_HELPER_VV_I(vmaxi_d, 64, helper_vv_i, do_vmaxi_s)
DO_HELPER_VVV(vmax_bu, 8, helper_vvv, do_vmax_u)
DO_HELPER_VVV(vmax_hu, 16, helper_vvv, do_vmax_u)
DO_HELPER_VVV(vmax_wu, 32, helper_vvv, do_vmax_u)
DO_HELPER_VVV(vmax_du, 64, helper_vvv, do_vmax_u)
DO_HELPER_VV_I(vmaxi_bu, 8, helper_vv_i, do_vmaxi_u)
DO_HELPER_VV_I(vmaxi_hu, 16, helper_vv_i, do_vmaxi_u)
DO_HELPER_VV_I(vmaxi_wu, 32, helper_vv_i, do_vmaxi_u)
DO_HELPER_VV_I(vmaxi_du, 64, helper_vv_i, do_vmaxi_u)
DO_HELPER_VVV(vmin_b, 8, helper_vvv, do_vmin_s)
DO_HELPER_VVV(vmin_h, 16, helper_vvv, do_vmin_s)
DO_HELPER_VVV(vmin_w, 32, helper_vvv, do_vmin_s)
DO_HELPER_VVV(vmin_d, 64, helper_vvv, do_vmin_s)
DO_HELPER_VV_I(vmini_b, 8, helper_vv_i, do_vmini_s)
DO_HELPER_VV_I(vmini_h, 16, helper_vv_i, do_vmini_s)
DO_HELPER_VV_I(vmini_w, 32, helper_vv_i, do_vmini_s)
DO_HELPER_VV_I(vmini_d, 64, helper_vv_i, do_vmini_s)
DO_HELPER_VVV(vmin_bu, 8, helper_vvv, do_vmin_u)
DO_HELPER_VVV(vmin_hu, 16, helper_vvv, do_vmin_u)
DO_HELPER_VVV(vmin_wu, 32, helper_vvv, do_vmin_u)
DO_HELPER_VVV(vmin_du, 64, helper_vvv, do_vmin_u)
DO_HELPER_VV_I(vmini_bu, 8, helper_vv_i, do_vmini_u)
DO_HELPER_VV_I(vmini_hu, 16, helper_vv_i, do_vmini_u)
DO_HELPER_VV_I(vmini_wu, 32, helper_vv_i, do_vmini_u)
DO_HELPER_VV_I(vmini_du, 64, helper_vv_i, do_vmini_u)

static void do_vmul(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] * Vk->B[n];
        break;
    case 16:
        Vd->H[n] = Vj->H[n] * Vk->H[n];
        break;
    case 32:
        Vd->W[n] = Vj->W[n] * Vk->W[n];
        break;
    case 64:
        Vd->D[n] = Vj->D[n] * Vk->D[n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmuh_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = ((int16_t)(Vj->B[n] * Vk->B[n])) >> 8;
        break;
    case 16:
        Vd->H[n] = ((int32_t)(Vj->H[n] * Vk->H[n])) >> 16;
        break;
    case 32:
        Vd->W[n] = ((int64_t)(Vj->W[n] * (int64_t)Vk->W[n])) >> 32;
        break;
    case 64:
        Vd->D[n] = ((__int128_t)(Vj->D[n] * (__int128_t)Vk->D[n])) >> 64;
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmuh_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = ((uint16_t)(((uint8_t)Vj->B[n]) * ((uint8_t)Vk->B[n]))) >> 8;
        break;
    case 16:
        Vd->H[n] = ((uint32_t)(((uint16_t)Vj->H[n]) * ((uint16_t)Vk->H[n]))) >> 16;
        break;
    case 32:
        Vd->W[n] = ((uint64_t)(((uint32_t)Vj->W[n]) * ((uint64_t)(uint32_t)Vk->W[n]))) >> 32;
        break;
    case 64:
        Vd->D[n] = ((__int128_t)(((uint64_t)Vj->D[n]) * ((__int128_t)(uint64_t)Vk->D[n]))) >> 64;
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vmul_b, 8, helper_vvv, do_vmul)
DO_HELPER_VVV(vmul_h, 16, helper_vvv, do_vmul)
DO_HELPER_VVV(vmul_w, 32, helper_vvv, do_vmul)
DO_HELPER_VVV(vmul_d, 64, helper_vvv, do_vmul)
DO_HELPER_VVV(vmuh_b, 8, helper_vvv, do_vmuh_s)
DO_HELPER_VVV(vmuh_h, 16, helper_vvv, do_vmuh_s)
DO_HELPER_VVV(vmuh_w, 32, helper_vvv, do_vmuh_s)
DO_HELPER_VVV(vmuh_d, 64, helper_vvv, do_vmuh_s)
DO_HELPER_VVV(vmuh_bu, 8, helper_vvv, do_vmuh_u)
DO_HELPER_VVV(vmuh_hu, 16, helper_vvv, do_vmuh_u)
DO_HELPER_VVV(vmuh_wu, 32, helper_vvv, do_vmuh_u)
DO_HELPER_VVV(vmuh_du, 64, helper_vvv, do_vmuh_u)

static void do_vmulwev_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = Vj->B[2 * n] * Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] = Vj->H[2 * n] * Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] = (int64_t)Vj->W[2 * n] * (int64_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] = (__int128_t)Vj->D[2 * n] * (__int128_t)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmulwod_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = Vj->B[2 * n + 1] * Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] = Vj->H[2 * n + 1] * Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] = (int64_t)Vj->W[2 * n + 1] * (int64_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] = (__int128_t)Vj->D[2 * n + 1] * (__int128_t)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmulwev_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint8_t)Vj->B[2 * n] * (uint8_t)Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] = (uint16_t)Vj->H[2 * n] * (uint16_t)Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] = (uint64_t)(uint32_t)Vj->W[2 * n] * (uint64_t)(uint32_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] = (__uint128_t)(uint64_t)Vj->D[2 * n] * (__uint128_t)(uint64_t)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmulwod_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint8_t)Vj->B[2 * n + 1] * (uint8_t)Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] = (uint16_t)Vj->H[2 * n + 1] * (uint16_t)Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] = (uint64_t)(uint32_t)Vj->W[2 * n + 1] * (uint64_t)(uint32_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] = (__uint128_t)(uint64_t)Vj->D[2 * n + 1] * (__uint128_t)(uint64_t)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmulwev_u_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint8_t)Vj->B[2 * n] * Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] = (uint16_t)Vj->H[2 * n] * Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] = (int64_t)(uint32_t)Vj->W[2 * n] * (int64_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] = (__int128_t)(uint64_t)Vj->D[2 * n] * (__int128_t)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmulwod_u_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint8_t)Vj->B[2 * n + 1] * Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] = (uint16_t)Vj->H[2 * n + 1] * Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] = (int64_t)(uint32_t)Vj->W[2 * n + 1] * (int64_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] = (__int128_t)(uint64_t)Vj->D[2 * n + 1] * (__int128_t)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vmulwev_h_b, 16, helper_vvv, do_vmulwev_s)
DO_HELPER_VVV(vmulwev_w_h, 32, helper_vvv, do_vmulwev_s)
DO_HELPER_VVV(vmulwev_d_w, 64, helper_vvv, do_vmulwev_s)
DO_HELPER_VVV(vmulwev_q_d, 128, helper_vvv, do_vmulwev_s)
DO_HELPER_VVV(vmulwod_h_b, 16, helper_vvv, do_vmulwod_s)
DO_HELPER_VVV(vmulwod_w_h, 32, helper_vvv, do_vmulwod_s)
DO_HELPER_VVV(vmulwod_d_w, 64, helper_vvv, do_vmulwod_s)
DO_HELPER_VVV(vmulwod_q_d, 128, helper_vvv, do_vmulwod_s)
DO_HELPER_VVV(vmulwev_h_bu, 16, helper_vvv, do_vmulwev_u)
DO_HELPER_VVV(vmulwev_w_hu, 32, helper_vvv, do_vmulwev_u)
DO_HELPER_VVV(vmulwev_d_wu, 64, helper_vvv, do_vmulwev_u)
DO_HELPER_VVV(vmulwev_q_du, 128, helper_vvv, do_vmulwev_u)
DO_HELPER_VVV(vmulwod_h_bu, 16, helper_vvv, do_vmulwod_u)
DO_HELPER_VVV(vmulwod_w_hu, 32, helper_vvv, do_vmulwod_u)
DO_HELPER_VVV(vmulwod_d_wu, 64, helper_vvv, do_vmulwod_u)
DO_HELPER_VVV(vmulwod_q_du, 128, helper_vvv, do_vmulwod_u)
DO_HELPER_VVV(vmulwev_h_bu_b, 16, helper_vvv, do_vmulwev_u_s)
DO_HELPER_VVV(vmulwev_w_hu_h, 32, helper_vvv, do_vmulwev_u_s)
DO_HELPER_VVV(vmulwev_d_wu_w, 64, helper_vvv, do_vmulwev_u_s)
DO_HELPER_VVV(vmulwev_q_du_d, 128, helper_vvv, do_vmulwev_u_s)
DO_HELPER_VVV(vmulwod_h_bu_b, 16, helper_vvv, do_vmulwod_u_s)
DO_HELPER_VVV(vmulwod_w_hu_h, 32, helper_vvv, do_vmulwod_u_s)
DO_HELPER_VVV(vmulwod_d_wu_w, 64, helper_vvv, do_vmulwod_u_s)
DO_HELPER_VVV(vmulwod_q_du_d, 128, helper_vvv, do_vmulwod_u_s)

static void do_vmadd(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] += Vj->B[n] * Vk->B[n];
        break;
    case 16:
        Vd->H[n] += Vj->H[n] * Vk->H[n];
        break;
    case 32:
        Vd->W[n] += Vj->W[n] * Vk->W[n];
        break;
    case 64:
        Vd->D[n] += Vj->D[n] * Vk->D[n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmsub(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] -= Vj->B[n] * Vk->B[n];
        break;
    case 16:
        Vd->H[n] -= Vj->H[n] * Vk->H[n];
        break;
    case 32:
        Vd->W[n] -= Vj->W[n] * Vk->W[n];
        break;
    case 64:
        Vd->D[n] -= Vj->D[n] * Vk->D[n];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vmadd_b, 8, helper_vvv, do_vmadd)
DO_HELPER_VVV(vmadd_h, 16, helper_vvv, do_vmadd)
DO_HELPER_VVV(vmadd_w, 32, helper_vvv, do_vmadd)
DO_HELPER_VVV(vmadd_d, 64, helper_vvv, do_vmadd)
DO_HELPER_VVV(vmsub_b, 8, helper_vvv, do_vmsub)
DO_HELPER_VVV(vmsub_h, 16, helper_vvv, do_vmsub)
DO_HELPER_VVV(vmsub_w, 32, helper_vvv, do_vmsub)
DO_HELPER_VVV(vmsub_d, 64, helper_vvv, do_vmsub)

static void do_vmaddwev_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] += Vj->B[2 * n] * Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] += Vj->H[2 * n] * Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] += (int64_t)Vj->W[2 * n] * (int64_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] += (__int128_t)Vj->D[2 * n] * (__int128_t)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmaddwod_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] += Vj->B[2 * n + 1] * Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] += Vj->H[2 * n + 1] * Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] += (int64_t)Vj->W[2 * n + 1] * (int64_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] += (__int128_t)((__int128_t)Vj->D[2 * n + 1] *
                    (__int128_t)Vk->D[2 * n + 1]);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmaddwev_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] += (uint8_t)Vj->B[2 * n] * (uint8_t)Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] += (uint16_t)Vj->H[2 * n] * (uint16_t)Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] += (uint64_t)(uint32_t)Vj->W[2 * n] *
                    (uint64_t)(uint32_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] += (__uint128_t)(uint64_t)Vj->D[2 * n] *
                    (__uint128_t)(uint64_t)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmaddwod_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] += (uint8_t)Vj->B[2 * n + 1] * (uint8_t)Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] += (uint16_t)Vj->H[2 * n + 1] * (uint16_t)Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] += (uint64_t)(uint32_t)Vj->W[2 * n + 1] *
                    (uint64_t)(uint32_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] += (__uint128_t)(uint64_t)Vj->D[2 * n + 1] *
                    (__uint128_t)(uint64_t)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmaddwev_u_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] += (uint8_t)Vj->B[2 * n] * Vk->B[2 * n];
        break;
    case 32:
        Vd->W[n] += (uint16_t)Vj->H[2 * n] * Vk->H[2 * n];
        break;
    case 64:
        Vd->D[n] += (int64_t)(uint32_t)Vj->W[2 * n] * (int64_t)Vk->W[2 * n];
        break;
    case 128:
        Vd->Q[n] += (__int128_t)(uint64_t)Vj->D[2 * n] *
                    (__int128_t)Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmaddwod_u_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] += (uint8_t)Vj->B[2 * n + 1] * Vk->B[2 * n + 1];
        break;
    case 32:
        Vd->W[n] += (uint16_t)Vj->H[2 * n + 1] * Vk->H[2 * n + 1];
        break;
    case 64:
        Vd->D[n] += (int64_t)(uint32_t)Vj->W[2 * n + 1] *
                    (int64_t)Vk->W[2 * n + 1];
        break;
    case 128:
        Vd->Q[n] += (__int128_t)(uint64_t)Vj->D[2 * n + 1] *
                    (__int128_t)Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vmaddwev_h_b, 16, helper_vvv, do_vmaddwev_s)
DO_HELPER_VVV(vmaddwev_w_h, 32, helper_vvv, do_vmaddwev_s)
DO_HELPER_VVV(vmaddwev_d_w, 64, helper_vvv, do_vmaddwev_s)
DO_HELPER_VVV(vmaddwev_q_d, 128, helper_vvv, do_vmaddwev_s)
DO_HELPER_VVV(vmaddwod_h_b, 16, helper_vvv, do_vmaddwod_s)
DO_HELPER_VVV(vmaddwod_w_h, 32, helper_vvv, do_vmaddwod_s)
DO_HELPER_VVV(vmaddwod_d_w, 64, helper_vvv, do_vmaddwod_s)
DO_HELPER_VVV(vmaddwod_q_d, 128, helper_vvv, do_vmaddwod_s)
DO_HELPER_VVV(vmaddwev_h_bu, 16, helper_vvv, do_vmaddwev_u)
DO_HELPER_VVV(vmaddwev_w_hu, 32, helper_vvv, do_vmaddwev_u)
DO_HELPER_VVV(vmaddwev_d_wu, 64, helper_vvv, do_vmaddwev_u)
DO_HELPER_VVV(vmaddwev_q_du, 128, helper_vvv, do_vmaddwev_u)
DO_HELPER_VVV(vmaddwod_h_bu, 16, helper_vvv, do_vmaddwod_u)
DO_HELPER_VVV(vmaddwod_w_hu, 32, helper_vvv, do_vmaddwod_u)
DO_HELPER_VVV(vmaddwod_d_wu, 64, helper_vvv, do_vmaddwod_u)
DO_HELPER_VVV(vmaddwod_q_du, 128, helper_vvv, do_vmaddwod_u)
DO_HELPER_VVV(vmaddwev_h_bu_b, 16, helper_vvv, do_vmaddwev_u_s)
DO_HELPER_VVV(vmaddwev_w_hu_h, 32, helper_vvv, do_vmaddwev_u_s)
DO_HELPER_VVV(vmaddwev_d_wu_w, 64, helper_vvv, do_vmaddwev_u_s)
DO_HELPER_VVV(vmaddwev_q_du_d, 128, helper_vvv, do_vmaddwev_u_s)
DO_HELPER_VVV(vmaddwod_h_bu_b, 16, helper_vvv, do_vmaddwod_u_s)
DO_HELPER_VVV(vmaddwod_w_hu_h, 32, helper_vvv, do_vmaddwod_u_s)
DO_HELPER_VVV(vmaddwod_d_wu_w, 64, helper_vvv, do_vmaddwod_u_s)
DO_HELPER_VVV(vmaddwod_q_du_d, 128, helper_vvv, do_vmaddwod_u_s)

static int64_t s_div_s(int64_t s1, int64_t s2, int bit)
{
    int64_t smin = MAKE_64BIT_MASK((bit -1), 64);

    if (s1 == smin && s2 == -1) {
        return smin;
    }
    return s2 ? s1 / s2 : s1 >= 0 ? -1 : 1;
}

static void do_vdiv_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = s_div_s(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = s_div_s(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = s_div_s(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = s_div_s(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t u_div_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;

    return u2 ? u1 / u2 : -1;
}

static void do_vdiv_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = u_div_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = u_div_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = u_div_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = u_div_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static int64_t s_mod_s(int64_t s1, int64_t s2, int bit)
{
    int64_t smin = MAKE_64BIT_MASK((bit -1), 64);

    if (s1 == smin && s2 == -1) {
        return 0;
    }
    return s2 ? s1 % s2 : s1;
}

static void do_vmod_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = s_mod_s(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = s_mod_s(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = s_mod_s(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = s_mod_s(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t u_mod_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;

    return u2 ? u1 % u2 : u1;
}

static void do_vmod_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = u_mod_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = u_mod_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = u_mod_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = u_mod_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vdiv_b, 8, helper_vvv, do_vdiv_s)
DO_HELPER_VVV(vdiv_h, 16, helper_vvv, do_vdiv_s)
DO_HELPER_VVV(vdiv_w, 32, helper_vvv, do_vdiv_s)
DO_HELPER_VVV(vdiv_d, 64, helper_vvv, do_vdiv_s)
DO_HELPER_VVV(vdiv_bu, 8, helper_vvv, do_vdiv_u)
DO_HELPER_VVV(vdiv_hu, 16, helper_vvv, do_vdiv_u)
DO_HELPER_VVV(vdiv_wu, 32, helper_vvv, do_vdiv_u)
DO_HELPER_VVV(vdiv_du, 64, helper_vvv, do_vdiv_u)
DO_HELPER_VVV(vmod_b, 8, helper_vvv, do_vmod_s)
DO_HELPER_VVV(vmod_h, 16, helper_vvv, do_vmod_s)
DO_HELPER_VVV(vmod_w, 32, helper_vvv, do_vmod_s)
DO_HELPER_VVV(vmod_d, 64, helper_vvv, do_vmod_s)
DO_HELPER_VVV(vmod_bu, 8, helper_vvv, do_vmod_u)
DO_HELPER_VVV(vmod_hu, 16, helper_vvv, do_vmod_u)
DO_HELPER_VVV(vmod_wu, 32, helper_vvv, do_vmod_u)
DO_HELPER_VVV(vmod_du, 64, helper_vvv, do_vmod_u)

static int64_t sat_s(int64_t s1, uint32_t imm)
{
    int64_t max = MAKE_64BIT_MASK(0, imm);
    int64_t min = MAKE_64BIT_MASK(imm, 64);

    if (s1 > max -1) {
        return max;
    } else if (s1 < - max) {
        return min;
    } else {
        return s1;
    }
}

static void do_vsat_s(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = sat_s(Vj->B[n], imm);
        break;
    case 16:
        Vd->H[n] = sat_s(Vj->H[n], imm);
        break;
    case 32:
        Vd->W[n] = sat_s(Vj->W[n], imm);
        break;
    case 64:
        Vd->D[n] = sat_s(Vj->D[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t sat_u(uint64_t u1, uint32_t imm)
{
    uint64_t umax_imm = MAKE_64BIT_MASK(0, imm + 1);

    return u1 < umax_imm ? u1 : umax_imm;
}

static void do_vsat_u(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = sat_u((uint8_t)Vj->B[n], imm);
        break;
    case 16:
        Vd->H[n] = sat_u((uint16_t)Vj->H[n], imm);
        break;
    case 32:
        Vd->W[n] = sat_u((uint32_t)Vj->W[n], imm);
        break;
    case 64:
        Vd->D[n] = sat_u((uint64_t)Vj->D[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_I(vsat_b, 8, helper_vv_i, do_vsat_s)
DO_HELPER_VV_I(vsat_h, 16, helper_vv_i, do_vsat_s)
DO_HELPER_VV_I(vsat_w, 32, helper_vv_i, do_vsat_s)
DO_HELPER_VV_I(vsat_d, 64, helper_vv_i, do_vsat_s)
DO_HELPER_VV_I(vsat_bu, 8, helper_vv_i, do_vsat_u)
DO_HELPER_VV_I(vsat_hu, 16, helper_vv_i, do_vsat_u)
DO_HELPER_VV_I(vsat_wu, 32, helper_vv_i, do_vsat_u)
DO_HELPER_VV_I(vsat_du, 64, helper_vv_i, do_vsat_u)

static void do_vexth_s(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = Vj->B[n + LSX_LEN/bit];
        break;
    case 32:
        Vd->W[n] = Vj->H[n + LSX_LEN/bit];
        break;
    case 64:
        Vd->D[n] = Vj->W[n + LSX_LEN/bit];
        break;
    case 128:
        Vd->Q[n] = Vj->D[n + LSX_LEN/bit];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vexth_u(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = (uint8_t)Vj->B[n + LSX_LEN/bit];
        break;
    case 32:
        Vd->W[n] = (uint16_t)Vj->H[n + LSX_LEN/bit];
        break;
    case 64:
        Vd->D[n] = (uint32_t)Vj->W[n + LSX_LEN/bit];
        break;
    case 128:
        Vd->Q[n] = (uint64_t)Vj->D[n + LSX_LEN/bit];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV(vexth_h_b, 16, helper_vv, do_vexth_s)
DO_HELPER_VV(vexth_w_h, 32, helper_vv, do_vexth_s)
DO_HELPER_VV(vexth_d_w, 64, helper_vv, do_vexth_s)
DO_HELPER_VV(vexth_q_d, 128, helper_vv, do_vexth_s)
DO_HELPER_VV(vexth_hu_bu, 16, helper_vv, do_vexth_u)
DO_HELPER_VV(vexth_wu_hu, 32, helper_vv, do_vexth_u)
DO_HELPER_VV(vexth_du_wu, 64, helper_vv, do_vexth_u)
DO_HELPER_VV(vexth_qu_du, 128, helper_vv, do_vexth_u)

static void do_vsigncov(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = (Vj->B[n] == 0x0) ? 0 :
                   (Vj->B[n] < 0) ? -Vk->B[n] : Vk->B[n];
        break;
    case 16:
        Vd->H[n] = (Vj->H[n] == 0x0) ? 0 :
                   (Vj->H[n] < 0) ? -Vk->H[n] : Vk->H[n];
        break;
    case 32:
        Vd->W[n] = (Vj->W[n] == 0x0) ? 0 :
                   (Vj->W[n] < 0) ? -Vk->W[n] : Vk->W[n];
        break;
    case 64:
        Vd->D[n] = (Vj->D[n] == 0x0) ? 0 :
                   (Vj->D[n] < 0) ? -Vk->D[n] : Vk->W[n];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsigncov_b, 8, helper_vvv, do_vsigncov)
DO_HELPER_VVV(vsigncov_h, 16, helper_vvv, do_vsigncov)
DO_HELPER_VVV(vsigncov_w, 32, helper_vvv, do_vsigncov)
DO_HELPER_VVV(vsigncov_d, 64, helper_vvv, do_vsigncov)

/* Vd, Vj, vd = 0 */
static void helper_vv_z(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, int bit,
                        void (*func)(vec_t*, vec_t*, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    Vd->D[0] = 0;
    Vd->D[1] = 0;

    for (i = 0; i < LSX_LEN/bit; i++) {
        func(Vd, Vj, bit, i);
    }
}

static void do_vmskltz(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->H[0] |= ((0x80 & Vj->B[n]) == 0) << n;
        break;
    case 16:
        Vd->H[0] |= ((0x8000 & Vj->H[n]) == 0) << n;
        break;
    case 32:
        Vd->H[0] |= ((0x80000000 & Vj->W[n]) == 0) << n;
        break;
    case 64:
        Vd->H[0] |= ((0x8000000000000000 & Vj->D[n]) == 0) << n;
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vmskgez(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    Vd->H[0] |= !((0x80 & Vj->B[n]) == 0) << n;
}

static void do_vmsknz(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    Vd->H[0] |=  (Vj->B[n] == 0) << n;
}

DO_HELPER_VV(vmskltz_b, 8, helper_vv_z, do_vmskltz)
DO_HELPER_VV(vmskltz_h, 16, helper_vv_z, do_vmskltz)
DO_HELPER_VV(vmskltz_w, 32, helper_vv_z, do_vmskltz)
DO_HELPER_VV(vmskltz_d, 64, helper_vv_z, do_vmskltz)
DO_HELPER_VV(vmskgez_b, 8, helper_vv_z, do_vmskgez)
DO_HELPER_VV(vmsknz_b, 8, helper_vv_z, do_vmsknz)

static void do_vand_v(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    Vd->D[n] = Vj->D[n] & Vk->D[n];
}

static void do_vor_v(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    Vd->D[n] = Vj->D[n] | Vk->D[n];
}

static void do_vxor_v(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    Vd->D[n] = Vj->D[n] ^ Vk->D[n];
}

static void do_vnor_v(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    Vd->D[n] = ~(Vj->D[n] | Vk->D[n]);
}

static void do_vandn_v(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    Vd->D[n] = ~Vj->D[n] & Vk->D[n];
}

static void do_vorn_v(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    Vd->D[n] = Vj->D[n] | ~Vk->D[n];
}

DO_HELPER_VVV(vand_v, 64, helper_vvv, do_vand_v)
DO_HELPER_VVV(vor_v, 64, helper_vvv, do_vor_v)
DO_HELPER_VVV(vxor_v, 64, helper_vvv, do_vxor_v)
DO_HELPER_VVV(vnor_v, 64, helper_vvv, do_vnor_v)
DO_HELPER_VVV(vandn_v, 64, helper_vvv, do_vandn_v)
DO_HELPER_VVV(vorn_v, 64, helper_vvv, do_vorn_v)

static void do_vandi_b(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    Vd->B[n] = Vj->B[n] & imm;
}

static void do_vori_b(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    Vd->B[n] = Vj->B[n] | imm;
}

static void do_vxori_b(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    Vd->B[n] = Vj->B[n] ^ imm;
}

static void do_vnori_b(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    Vd->B[n] = ~(Vj->B[n] | imm);
}

DO_HELPER_VV_I(vandi_b, 8, helper_vv_i, do_vandi_b)
DO_HELPER_VV_I(vori_b, 8, helper_vv_i, do_vori_b)
DO_HELPER_VV_I(vxori_b, 8, helper_vv_i, do_vxori_b)
DO_HELPER_VV_I(vnori_b, 8, helper_vv_i, do_vnori_b)

static void do_vsll(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] << ((uint64_t)(Vk->B[n]) % bit);
        break;
    case 16:
        Vd->H[n] = Vj->H[n] << ((uint64_t)(Vk->H[n]) % bit);
        break;
    case 32:
        Vd->W[n] = Vj->W[n] << ((uint64_t)(Vk->W[n]) % bit);
        break;
    case 64:
        Vd->D[n] = Vj->D[n] << ((uint64_t)(Vk->D[n]) % bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vslli(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] << ((uint64_t)(imm) % bit);
        break;
    case 16:
        Vd->H[n] = Vj->H[n] << ((uint64_t)(imm) % bit);
        break;
    case 32:
        Vd->W[n] = Vj->W[n] << ((uint64_t)(imm) % bit);
        break;
    case 64:
        Vd->D[n] = Vj->D[n] << ((uint64_t)(imm) % bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsll_b, 8, helper_vvv, do_vsll)
DO_HELPER_VVV(vsll_h, 16, helper_vvv, do_vsll)
DO_HELPER_VVV(vsll_w, 32, helper_vvv, do_vsll)
DO_HELPER_VVV(vsll_d, 64, helper_vvv, do_vsll)
DO_HELPER_VV_I(vslli_b, 8, helper_vv_i, do_vslli)
DO_HELPER_VV_I(vslli_h, 16, helper_vv_i, do_vslli)
DO_HELPER_VV_I(vslli_w, 32, helper_vv_i, do_vslli)
DO_HELPER_VV_I(vslli_d, 64, helper_vv_i, do_vslli)

static int64_t vsrl(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;

    return u1 >> ((uint64_t)(s2) % bit);
}

static void do_vsrl(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsrl(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vsrl(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vsrl(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vsrl(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsrli(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsrl(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vsrl(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vsrl(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vsrl(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsrl_b, 8, helper_vvv, do_vsrl)
DO_HELPER_VVV(vsrl_h, 16, helper_vvv, do_vsrl)
DO_HELPER_VVV(vsrl_w, 32, helper_vvv, do_vsrl)
DO_HELPER_VVV(vsrl_d, 64, helper_vvv, do_vsrl)
DO_HELPER_VV_I(vsrli_b, 8, helper_vv_i, do_vsrli)
DO_HELPER_VV_I(vsrli_h, 16, helper_vv_i, do_vsrli)
DO_HELPER_VV_I(vsrli_w, 32, helper_vv_i, do_vsrli)
DO_HELPER_VV_I(vsrli_d, 64, helper_vv_i, do_vsrli)

static void do_vsra(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] >> ((uint64_t)(Vk->B[n]) % bit);
        break;
    case 16:
        Vd->H[n] = Vj->H[n] >> ((uint64_t)(Vk->H[n]) % bit);
        break;
    case 32:
        Vd->W[n] = Vj->W[n] >> ((uint64_t)(Vk->W[n]) % bit);
        break;
    case 64:
        Vd->D[n] = Vj->D[n] >> ((uint64_t)(Vk->D[n]) % bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsrai(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[n] >> ((uint64_t)(imm) % bit);
        break;
    case 16:
        Vd->H[n] = Vj->H[n] >> ((uint64_t)(imm) % bit);
        break;
    case 32:
        Vd->W[n] = Vj->W[n] >> ((uint64_t)(imm) % bit);
        break;
    case 64:
        Vd->D[n] = Vj->D[n] >> ((uint64_t)(imm) % bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsra_b, 8, helper_vvv, do_vsra)
DO_HELPER_VVV(vsra_h, 16, helper_vvv, do_vsra)
DO_HELPER_VVV(vsra_w, 32, helper_vvv, do_vsra)
DO_HELPER_VVV(vsra_d, 64, helper_vvv, do_vsra)
DO_HELPER_VV_I(vsrai_b, 8, helper_vv_i, do_vsrai)
DO_HELPER_VV_I(vsrai_h, 16, helper_vv_i, do_vsrai)
DO_HELPER_VV_I(vsrai_w, 32, helper_vv_i, do_vsrai)
DO_HELPER_VV_I(vsrai_d, 64, helper_vv_i, do_vsrai)

static uint64_t vrotr(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    int32_t n = (uint64_t)(s2) % bit;

    return u1 >> n | u1 << (bit - n);
}

static void do_vrotr(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vrotr(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vrotr(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vrotr(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vrotr(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vrotri(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vrotr(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vrotr(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vrotr(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vrotr(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vrotr_b, 8, helper_vvv, do_vrotr)
DO_HELPER_VVV(vrotr_h, 16, helper_vvv, do_vrotr)
DO_HELPER_VVV(vrotr_w, 32, helper_vvv, do_vrotr)
DO_HELPER_VVV(vrotr_d, 64, helper_vvv, do_vrotr)
DO_HELPER_VV_I(vrotri_b, 8, helper_vv_i, do_vrotri)
DO_HELPER_VV_I(vrotri_h, 16, helper_vv_i, do_vrotri)
DO_HELPER_VV_I(vrotri_w, 32, helper_vv_i, do_vrotri)
DO_HELPER_VV_I(vrotri_d, 64, helper_vv_i, do_vrotri)
