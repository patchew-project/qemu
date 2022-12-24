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
