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
#include "fpu/softfloat.h"
#include "internals.h"

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

#define DO_HELPER_VVVV(NAME, BIT, FUNC, ...)                               \
    void helper_##NAME(CPULoongArchState *env,                             \
                       uint32_t vd, uint32_t vj, uint32_t vk, uint32_t va) \
    { FUNC(env, vd, vj, vk, va, BIT, __VA_ARGS__); }

#define DO_HELPER_CV(NAME, BIT, FUNC, ...)                               \
    void helper_##NAME(CPULoongArchState *env, uint32_t cd, uint32_t vj) \
    { FUNC(env, cd, vj, BIT, __VA_ARGS__); }

#define DO_HELPER_VR_I(NAME, BIT, FUNC, ...)                   \
    void helper_##NAME(CPULoongArchState *env,                 \
                       uint32_t vd, uint32_t rj, uint32_t imm) \
    { FUNC(env, vd, rj, imm, BIT, __VA_ARGS__ ); }

#define DO_HELPER_RV_I(NAME, BIT, FUNC, ...)                   \
    void helper_##NAME(CPULoongArchState *env,                 \
                       uint32_t rd, uint32_t vj, uint32_t imm) \
    { FUNC(env, rd, vj, imm, BIT, __VA_ARGS__ ); }

#define DO_HELPER_VR(NAME, BIT, FUNC, ...)       \
    void helper_##NAME(CPULoongArchState *env,   \
                       uint32_t vd, uint32_t rj) \
    { FUNC(env, vd, rj, BIT, __VA_ARGS__ ); }

#define DO_HELPER_VV_R(NAME, BIT, FUNC, ...)                  \
    void helper_##NAME(CPULoongArchState *env,                \
                       uint32_t vd, uint32_t vj, uint32_t rk) \
    { FUNC(env, vd, vj, rk, BIT, __VA_ARGS__); }

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

static void helper_vv_i_c(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm, int bit,
                         void (*func)(vec_t*, vec_t*, uint32_t, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    vec_t dest;
    dest.D[0] = 0;
    dest.D[1] = 0;
    for (i = 0; i < LSX_LEN/bit; i++) {
         func(&dest, Vj, imm, bit, i);
    }
    Vd->D[0] = dest.D[0];
    Vd->D[1] = dest.D[1];
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

static void do_vsllwil_s(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = ((int8_t)Vj->B[n]) << ((uint64_t)(imm) % bit);
        break;
    case 32:
        Vd->W[n] = ((int16_t)Vj->H[n]) << ((uint64_t)(imm) % bit);
        break;
    case 64:
        Vd->D[n] = ((int64_t)(int32_t)Vj->W[n]) << ((uint64_t)(imm) % bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vextl_q_d(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    Vd->Q[0] = (__int128_t)Vj->D[0];
}

static void do_vsllwil_u(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->H[n] = ((uint8_t)Vj->B[n]) << ((uint64_t)(imm) % bit);
        break;
    case 32:
        Vd->W[n] = ((uint16_t)Vj->H[n]) << ((uint64_t)(imm) % bit);
        break;
    case 64:
        Vd->D[n] = ((uint64_t)(uint32_t)Vj->W[n]) << ((uint64_t)(imm) % bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vextl_qu_du(vec_t *Vd, vec_t *Vj, int bit, int n)
{
     Vd->Q[0] = (uint64_t)Vj->D[0];
}

DO_HELPER_VV_I(vsllwil_h_b, 16, helper_vv_i_c, do_vsllwil_s)
DO_HELPER_VV_I(vsllwil_w_h, 32, helper_vv_i_c, do_vsllwil_s)
DO_HELPER_VV_I(vsllwil_d_w, 64, helper_vv_i_c, do_vsllwil_s)
DO_HELPER_VV(vextl_q_d, 128, helper_vv, do_vextl_q_d)
DO_HELPER_VV_I(vsllwil_hu_bu, 16, helper_vv_i_c, do_vsllwil_u)
DO_HELPER_VV_I(vsllwil_wu_hu, 32, helper_vv_i_c, do_vsllwil_u)
DO_HELPER_VV_I(vsllwil_du_wu, 64, helper_vv_i_c, do_vsllwil_u)
DO_HELPER_VV(vextl_qu_du, 128, helper_vv, do_vextl_qu_du)

static int64_t vsrlr(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    int32_t n = (uint64_t)(s2 % bit);

    if (n == 0) {
        return u1;
    } else {
        uint64_t r_bit = (u1 >> (n  -1)) & 1;
        return (u1 >> n) + r_bit;
    }
}

static void do_vsrlr(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsrlr(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vsrlr(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vsrlr(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vsrlr(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsrlri(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsrlr(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vsrlr(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vsrlr(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vsrlr(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsrlr_b, 8, helper_vvv, do_vsrlr)
DO_HELPER_VVV(vsrlr_h, 16, helper_vvv, do_vsrlr)
DO_HELPER_VVV(vsrlr_w, 32, helper_vvv, do_vsrlr)
DO_HELPER_VVV(vsrlr_d, 64, helper_vvv, do_vsrlr)
DO_HELPER_VVV(vsrlri_b, 8, helper_vv_i, do_vsrlri)
DO_HELPER_VVV(vsrlri_h, 16, helper_vv_i, do_vsrlri)
DO_HELPER_VVV(vsrlri_w, 32, helper_vv_i, do_vsrlri)
DO_HELPER_VVV(vsrlri_d, 64, helper_vv_i, do_vsrlri)

static int64_t vsrar(int64_t s1, int64_t s2, int bit)
{
    int32_t n = (uint64_t)(s2 % bit);

    if (n == 0) {
        return s1;
    } else {
        uint64_t r_bit = (s1 >> (n  -1)) & 1;
        return (s1 >> n) + r_bit;
    }
}

static void do_vsrar(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsrar(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vsrar(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vsrar(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vsrar(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsrari(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsrar(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vsrar(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vsrar(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vsrar(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsrar_b, 8, helper_vvv, do_vsrar)
DO_HELPER_VVV(vsrar_h, 16, helper_vvv, do_vsrar)
DO_HELPER_VVV(vsrar_w, 32, helper_vvv, do_vsrar)
DO_HELPER_VVV(vsrar_d, 64, helper_vvv, do_vsrar)
DO_HELPER_VVV(vsrari_b, 8, helper_vv_i, do_vsrari)
DO_HELPER_VVV(vsrari_h, 16, helper_vv_i, do_vsrari)
DO_HELPER_VVV(vsrari_w, 32, helper_vv_i, do_vsrari)
DO_HELPER_VVV(vsrari_d, 64, helper_vv_i, do_vsrari)

static void helper_vvv_hz(CPULoongArchState *env,
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
    Vd->D[1] = 0;
}

static void do_vsrln(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = (uint16_t)Vj->H[n] >> (Vk->H[n] & 0xf);
        break;
    case 32:
        Vd->H[n] = (uint32_t)Vj->W[n] >> (Vk->W[n] & 0x1f);
        break;
    case 64:
        Vd->W[n] = (uint64_t)Vj->D[n] >> (Vk->D[n] & 0x3f);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsran(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = Vj->H[n] >> (Vk->H[n] & 0xf);
        break;
    case 32:
        Vd->H[n] = Vj->W[n] >> (Vk->W[n] & 0x1f);
        break;
    case 64:
        Vd->W[n] = Vj->D[n] >> (Vk->D[n] & 0x3f);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsrln_b_h, 16, helper_vvv_hz, do_vsrln)
DO_HELPER_VVV(vsrln_h_w, 32, helper_vvv_hz, do_vsrln)
DO_HELPER_VVV(vsrln_w_d, 64, helper_vvv_hz, do_vsrln)
DO_HELPER_VVV(vsran_b_h, 16, helper_vvv_hz, do_vsran)
DO_HELPER_VVV(vsran_h_w, 32, helper_vvv_hz, do_vsran)
DO_HELPER_VVV(vsran_w_d, 64, helper_vvv_hz, do_vsran)

static void helper_vv_ni_c(CPULoongArchState *env,
                           uint32_t vd, uint32_t vj, uint32_t imm, int bit,
                           void (*func)(vec_t*, vec_t*, vec_t*,
                                        uint32_t, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    vec_t dest;
    dest.D[0] = 0;
    dest.D[1] = 0;
    for (i = 0; i < LSX_LEN/bit; i++) {
         func(&dest, Vd, Vj, imm, bit, i);
    }
    Vd->D[0] = dest.D[0];
    Vd->D[1] = dest.D[1];
}

static void do_vsrlni(vec_t *dest, vec_t *Vd, vec_t *Vj,
                      uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = (uint16_t)Vj->H[n] >> imm;
        dest->B[n + 128/bit] = (uint16_t)Vd->H[n] >> imm;
        break;
    case 32:
        dest->H[n] = (uint32_t)Vj->W[n] >> imm;
        dest->H[n + 128/bit] = (uint32_t)Vd->W[n] >> imm;
        break;
    case 64:
        dest->W[n] = (uint64_t)Vj->D[n] >> imm;
        dest->W[n + 128/bit] = (uint64_t)Vd->D[n] >> imm;
        break;
    case 128:
        dest->D[n] = (__uint128_t)Vj->Q[n] >> imm;
        dest->D[n + 128/bit] = (__uint128_t)Vd->Q[n] >> imm;
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsrani(vec_t *dest, vec_t *Vd, vec_t *Vj,
                      uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = Vj->H[n] >> imm;
        dest->B[n + 128/bit] = Vd->H[n] >> imm;
        break;
    case 32:
        dest->H[n] = Vj->W[n] >> imm;
        dest->H[n + 128/bit] = Vd->W[n] >> imm;
        break;
    case 64:
        dest->W[n] = Vj->D[n] >> imm;
        dest->W[n + 128/bit] = Vd->D[n] >> imm;
        break;
    case 128:
        dest->D[n] = (__int128_t)Vj->Q[n] >> imm;
        dest->D[n + 128/bit] = (__int128_t)Vd->Q[n] >> imm;
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_I(vsrlni_b_h, 16, helper_vv_ni_c, do_vsrlni)
DO_HELPER_VV_I(vsrlni_h_w, 32, helper_vv_ni_c, do_vsrlni)
DO_HELPER_VV_I(vsrlni_w_d, 64, helper_vv_ni_c, do_vsrlni)
DO_HELPER_VV_I(vsrlni_d_q, 128, helper_vv_ni_c, do_vsrlni)
DO_HELPER_VV_I(vsrani_b_h, 16, helper_vv_ni_c, do_vsrani)
DO_HELPER_VV_I(vsrani_h_w, 32, helper_vv_ni_c, do_vsrani)
DO_HELPER_VV_I(vsrani_w_d, 64, helper_vv_ni_c, do_vsrani)
DO_HELPER_VV_I(vsrani_d_q, 128, helper_vv_ni_c, do_vsrani)

static void do_vsrlrn(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = vsrlr((uint16_t)Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->H[n] = vsrlr((uint32_t)Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->W[n] = vsrlr((uint64_t)Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsrarn(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = vsrar(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->H[n] = vsrar(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->W[n] = vsrar(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsrlrn_b_h, 16, helper_vvv_hz, do_vsrlrn)
DO_HELPER_VVV(vsrlrn_h_w, 32, helper_vvv_hz, do_vsrlrn)
DO_HELPER_VVV(vsrlrn_w_d, 64, helper_vvv_hz, do_vsrlrn)
DO_HELPER_VVV(vsrarn_b_h, 16, helper_vvv_hz, do_vsrarn)
DO_HELPER_VVV(vsrarn_h_w, 32, helper_vvv_hz, do_vsrarn)
DO_HELPER_VVV(vsrarn_w_d, 64, helper_vvv_hz, do_vsrarn)

static __int128_t vsrlrn(__int128_t s1, uint32_t imm)
{
    if (imm == 0) {
        return s1;
    } else {
        __uint128_t t1 = (__uint128_t)1 << (imm -1);
        return (s1 + t1) >> imm;
    }
}

static void do_vsrlrni(vec_t *dest, vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = vsrlrn((uint16_t)Vj->H[n], imm);
        dest->B[n + 128 / bit] = vsrlrn((uint16_t)Vd->H[n], imm);
        break;
    case 32:
        dest->H[n] = vsrlrn((uint32_t)Vj->W[n], imm);
        dest->H[n + 128 / bit] = vsrlrn((uint32_t)Vd->W[n], imm);
        break;
    case 64:
        dest->W[n] = vsrlrn((uint64_t)Vj->D[n], imm);
        dest->W[n + 128 / bit] = vsrlrn((uint64_t)Vd->D[n], imm);
        break;
    case 128:
        dest->D[n] = vsrlrn((__uint128_t)Vj->Q[n], imm);
        dest->D[n + 128 / bit] = vsrlrn((__uint128_t)Vd->Q[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vsrarni(vec_t *dest, vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = vsrlrn(Vj->H[n], imm);
        dest->B[n + 128 / bit] = vsrlrn(Vd->H[n], imm);
        break;
    case 32:
        dest->H[n] = vsrlrn(Vj->W[n], imm);
        dest->H[n + 128 / bit] = vsrlrn(Vd->W[n], imm);
        break;
    case 64:
        dest->W[n] = vsrlrn(Vj->D[n], imm);
        dest->W[n + 128 / bit] = vsrlrn(Vd->D[n], imm);
        break;
    case 128:
        dest->D[n] = vsrlrn(Vj->Q[n], imm);
        dest->D[n + 128 / bit] = vsrlrn(Vd->Q[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_I(vsrlrni_b_h, 16, helper_vv_ni_c, do_vsrlrni)
DO_HELPER_VV_I(vsrlrni_h_w, 32, helper_vv_ni_c, do_vsrlrni)
DO_HELPER_VV_I(vsrlrni_w_d, 64, helper_vv_ni_c, do_vsrlrni)
DO_HELPER_VV_I(vsrlrni_d_q, 128, helper_vv_ni_c, do_vsrlrni)
DO_HELPER_VV_I(vsrarni_b_h, 16, helper_vv_ni_c, do_vsrarni)
DO_HELPER_VV_I(vsrarni_h_w, 32, helper_vv_ni_c, do_vsrarni)
DO_HELPER_VV_I(vsrarni_w_d, 64, helper_vv_ni_c, do_vsrarni)
DO_HELPER_VV_I(vsrarni_d_q, 128, helper_vv_ni_c, do_vsrarni)

static int64_t vsra(int64_t s1, int64_t s2, int bit)
{
    return (s1 >> ((uint64_t)(s2) % bit));
}

static void do_vssrln(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = sat_s(vsrl((uint16_t)Vj->H[n], Vk->H[n], bit), bit/2 - 1);
        break;
    case 32:
        Vd->H[n] = sat_s(vsrl((uint32_t)Vj->W[n], Vk->W[n], bit), bit/2 - 1);
        break;
    case 64:
        Vd->W[n] = sat_s(vsrl((uint64_t)Vj->D[n], Vk->D[n], bit), bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vssran(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = sat_s(vsra(Vj->H[n], Vk->H[n], bit), bit/2 - 1);
        break;
    case 32:
        Vd->H[n] = sat_s(vsra(Vj->W[n], Vk->W[n], bit), bit/2 - 1);
        break;
    case 64:
        Vd->W[n] = sat_s(vsra(Vj->D[n], Vk->D[n], bit), bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vssrln_b_h, 16, helper_vvv_hz, do_vssrln)
DO_HELPER_VVV(vssrln_h_w, 32, helper_vvv_hz, do_vssrln)
DO_HELPER_VVV(vssrln_w_d, 64, helper_vvv_hz, do_vssrln)
DO_HELPER_VVV(vssran_b_h, 16, helper_vvv_hz, do_vssran)
DO_HELPER_VVV(vssran_h_w, 32, helper_vvv_hz, do_vssran)
DO_HELPER_VVV(vssran_w_d, 64, helper_vvv_hz, do_vssran)

static void do_vssrln_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = sat_u(vsrl((uint16_t)Vj->H[n], Vk->H[n], bit), bit/2 - 1);
        break;
    case 32:
        Vd->H[n] = sat_u(vsrl((uint32_t)Vj->W[n], Vk->W[n], bit), bit/2 - 1);
        break;
    case 64:
        Vd->W[n] = sat_u(vsrl((uint64_t)Vj->D[n], Vk->D[n], bit), bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vssran_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = sat_u(vsra(Vj->H[n], Vk->H[n], bit), bit/2 - 1);
        if (Vd->B[n] < 0) {
            Vd->B[n] = 0;
        }
        break;
    case 32:
        Vd->H[n] = sat_u(vsra(Vj->W[n], Vk->W[n], bit), bit/2 - 1);
        if (Vd->H[n] < 0) {
            Vd->H[n] = 0;
        }
        break;
    case 64:
        Vd->W[n] = sat_u(vsra(Vj->D[n], Vk->D[n], bit), bit/2 - 1);
        if (Vd->W[n] < 0) {
            Vd->W[n] = 0;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vssrln_bu_h, 16, helper_vvv_hz, do_vssrln_u)
DO_HELPER_VVV(vssrln_hu_w, 32, helper_vvv_hz, do_vssrln_u)
DO_HELPER_VVV(vssrln_wu_d, 64, helper_vvv_hz, do_vssrln_u)
DO_HELPER_VVV(vssran_bu_h, 16, helper_vvv_hz, do_vssran_u)
DO_HELPER_VVV(vssran_hu_w, 32, helper_vvv_hz, do_vssran_u)
DO_HELPER_VVV(vssran_wu_d, 64, helper_vvv_hz, do_vssran_u)

static int64_t sat_s_128u(__uint128_t u1, uint32_t imm)
{
    uint64_t max = MAKE_64BIT_MASK(0, imm);
    return u1 < max ? u1: max;
}

static void do_vssrlni(vec_t *dest, vec_t *Vd, vec_t *Vj,
                       uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = sat_s((((uint16_t)Vj->H[n]) >> imm), bit/2 - 1);
        dest->B[n + 128/bit] = sat_s((((uint16_t)Vd->H[n]) >> imm), bit/2 -1);
        break;
    case 32:
        dest->H[n] = sat_s((((uint32_t)Vj->W[n]) >> imm), bit/2 - 1);
        dest->H[n + 128/bit] = sat_s((((uint32_t)Vd->W[n]) >> imm), bit/2 - 1);
        break;
    case 64:
        dest->W[n] = sat_s((((uint64_t)Vj->D[n]) >> imm), bit/2 - 1);
        dest->W[n + 128/bit] = sat_s((((uint64_t)Vd->D[n]) >> imm), bit/2 - 1);
        break;
    case 128:
        dest->D[n] = sat_s_128u((((__uint128_t)Vj->Q[n]) >> imm), bit/2 - 1);
        dest->D[n + 128/bit] = sat_s_128u((((__uint128_t)Vd->Q[n]) >> imm),
                                          bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

static int64_t sat_s_128(__int128_t s1, uint32_t imm)
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

static void do_vssrani(vec_t *dest, vec_t *Vd, vec_t *Vj,
                       uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = sat_s((Vj->H[n] >> imm), bit/2 - 1);
        dest->B[n + 128/bit] = sat_s((Vd->H[n] >> imm), bit/2 - 1);
        break;
    case 32:
        dest->H[n] = sat_s((Vj->W[n] >> imm), bit/2 - 1);
        dest->H[n + 128/bit] = sat_s((Vd->W[n] >> imm), bit/2 - 1);
        break;
    case 64:
        dest->W[n] = sat_s((Vj->D[n] >> imm), bit/2 - 1);
        dest->W[n + 128/bit] = sat_s((Vd->D[n] >> imm), bit/2 - 1);
        break;
    case 128:
        dest->D[n] = sat_s_128(((__int128_t)Vj->Q[n] >> imm), bit/2 - 1);
        dest->D[n + 128/bit] = sat_s_128(((__int128_t)Vd->Q[n] >> imm),
                                         bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_I(vssrlni_b_h, 16, helper_vv_ni_c, do_vssrlni)
DO_HELPER_VV_I(vssrlni_h_w, 32, helper_vv_ni_c, do_vssrlni)
DO_HELPER_VV_I(vssrlni_w_d, 64, helper_vv_ni_c, do_vssrlni)
DO_HELPER_VV_I(vssrlni_d_q, 128, helper_vv_ni_c, do_vssrlni)
DO_HELPER_VV_I(vssrani_b_h, 16, helper_vv_ni_c, do_vssrani)
DO_HELPER_VV_I(vssrani_h_w, 32, helper_vv_ni_c, do_vssrani)
DO_HELPER_VV_I(vssrani_w_d, 64, helper_vv_ni_c, do_vssrani)
DO_HELPER_VV_I(vssrani_d_q, 128, helper_vv_ni_c, do_vssrani)

static int64_t sat_u_128(__uint128_t u1, uint32_t imm)
{
    uint64_t max = MAKE_64BIT_MASK(0, imm + 1);
    return u1 < max ? u1 : max;
}

static void do_vssrlni_u(vec_t *dest, vec_t *Vd, vec_t *Vj,
                         uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = sat_u((((uint16_t)Vj->H[n]) >> imm),  bit/2 -1);
        dest->B[n + 128/bit] = sat_u((((uint16_t)Vd->H[n]) >> imm), bit/2 -1);
        break;
    case 32:
        dest->H[n] = sat_u((((uint32_t)Vj->W[n]) >> imm), imm);
        dest->H[n + 128/bit] = sat_u((((uint32_t)Vd->W[n]) >> imm), bit/2 -1);
        break;
    case 64:
        dest->W[n] = sat_u((((uint64_t)Vj->D[n]) >> imm), bit/2 - 1);
        dest->W[n + 128/bit] = sat_u((((uint64_t)Vd->D[n]) >> imm), bit/2 -1);
        break;
    case 128:
        dest->D[n] = sat_u_128((((__uint128_t)Vj->Q[n]) >> imm), bit/2 - 1);
        dest->D[n + 128/bit] = sat_u_128((((__uint128_t)Vd->Q[n]) >> imm),
                                         bit/2 -1);
        break;
    default:
        g_assert_not_reached();
    }
}

static void  do_vssrani_u(vec_t *dest, vec_t *Vd, vec_t *Vj,
                          uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = sat_u((Vj->H[n] >> imm), bit/2 - 1);
        if (dest->B[n] < 0) {
            dest->B[n] = 0;
        }
        dest->B[n + 128/bit] = sat_u((Vd->H[n] >> imm), bit/2 - 1);
        if (dest->B[n + 128/bit] < 0) {
            dest->B[n + 128/bit] = 0;
        }
        break;
    case 32:
        dest->H[n] = sat_u((Vj->W[n] >> imm), bit/2 - 1);
        if (dest->H[n] < 0) {
            dest->H[n] = 0;
        }
        dest->H[n + 128/bit] = sat_u((Vd->W[n] >> imm), bit/2 - 1);
        if (dest->H[n + 128/bit] < 0) {
            dest->H[n + 128/bit] = 0;
        }
        break;
    case 64:
        dest->W[n] = sat_u((Vj->D[n] >> imm), bit/2 - 1);
        if (dest->W[n] < 0) {
            dest->W[n] = 0;
        }
        dest->W[n + 128/bit] = sat_u((Vd->D[n] >> imm), bit/2 - 1);
        if (dest->W[n + 128/bit] < 0) {
            dest->W[n + 128/bit] = 0;
        }
        break;
    case 128:
        dest->D[n] = sat_u_128((Vj->Q[n] >> imm), bit/2 - 1);
        if (dest->D[n] < 0) {
            dest->D[n] = 0;
        }
        dest->D[n + 128/bit] = sat_u_128((Vd->Q[n] >> imm), bit/2 - 1);
        if (dest->D[n + 128/bit] < 0) {
            dest->D[n + 128/bit] = 0;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_I(vssrlni_bu_h, 16, helper_vv_ni_c, do_vssrlni_u)
DO_HELPER_VV_I(vssrlni_hu_w, 32, helper_vv_ni_c, do_vssrlni_u)
DO_HELPER_VV_I(vssrlni_wu_d, 64, helper_vv_ni_c, do_vssrlni_u)
DO_HELPER_VV_I(vssrlni_du_q, 128, helper_vv_ni_c, do_vssrlni_u)
DO_HELPER_VV_I(vssrani_bu_h, 16, helper_vv_ni_c, do_vssrani_u)
DO_HELPER_VV_I(vssrani_hu_w, 32, helper_vv_ni_c, do_vssrani_u)
DO_HELPER_VV_I(vssrani_wu_d, 64, helper_vv_ni_c, do_vssrani_u)
DO_HELPER_VV_I(vssrani_du_q, 128, helper_vv_ni_c, do_vssrani_u)

static void do_vssrlrn(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = sat_s(vsrlr((uint16_t)Vj->H[n], Vk->H[n], bit), bit/2 - 1);
        break;
    case 32:
        Vd->H[n] = sat_s(vsrlr((uint32_t)Vj->W[n], Vk->W[n], bit), bit/2 - 1);
        break;
    case 64:
        Vd->W[n] = sat_s(vsrlr((uint64_t)Vj->D[n], Vk->D[n], bit), bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vssrarn(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = sat_s(vsrar(Vj->H[n], Vk->H[n], bit), bit/2 - 1);
        break;
    case 32:
        Vd->H[n] = sat_s(vsrar(Vj->W[n], Vk->W[n], bit), bit/2 - 1);
        break;
    case 64:
        Vd->W[n] = sat_s(vsrar(Vj->D[n], Vk->D[n], bit), bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vssrlrn_b_h, 16, helper_vvv_hz, do_vssrlrn)
DO_HELPER_VVV(vssrlrn_h_w, 32, helper_vvv_hz, do_vssrlrn)
DO_HELPER_VVV(vssrlrn_w_d, 64, helper_vvv_hz, do_vssrlrn)
DO_HELPER_VVV(vssrarn_b_h, 16, helper_vvv_hz, do_vssrarn)
DO_HELPER_VVV(vssrarn_h_w, 32, helper_vvv_hz, do_vssrarn)
DO_HELPER_VVV(vssrarn_w_d, 64, helper_vvv_hz, do_vssrarn)

static void do_vssrlrn_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = sat_u(vsrlr((uint16_t)Vj->H[n], Vk->H[n], bit), bit/2 - 1);
        break;
    case 32:
        Vd->H[n] = sat_u(vsrlr((uint32_t)Vj->W[n], Vk->W[n], bit), bit/2 - 1);
        break;
    case 64:
        Vd->W[n] = sat_u(vsrlr((uint64_t)Vj->D[n], Vk->D[n], bit), bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vssrarn_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 16:
        Vd->B[n] = sat_u(vsrar(Vj->H[n], Vk->H[n], bit), bit/2 - 1);
        if (Vd->B[n] < 0) {
            Vd->B[n] = 0;
        }
        break;
    case 32:
        Vd->H[n] = sat_u(vsrar(Vj->W[n], Vk->W[n], bit), bit/2 - 1);
        if (Vd->H[n] < 0) {
            Vd->H[n] = 0;
        }
        break;
    case 64:
        Vd->W[n] = sat_u(vsrar(Vj->D[n], Vk->W[n], bit), bit/2 - 1);
        if (Vd->W[n] < 0) {
            Vd->W[n] = 0;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vssrlrn_bu_h, 16, helper_vvv_hz, do_vssrlrn_u)
DO_HELPER_VVV(vssrlrn_hu_w, 32, helper_vvv_hz, do_vssrlrn_u)
DO_HELPER_VVV(vssrlrn_wu_d, 64, helper_vvv_hz, do_vssrlrn_u)
DO_HELPER_VVV(vssrarn_bu_h, 16, helper_vvv_hz, do_vssrarn_u)
DO_HELPER_VVV(vssrarn_hu_w, 32, helper_vvv_hz, do_vssrarn_u)
DO_HELPER_VVV(vssrarn_wu_d, 64, helper_vvv_hz, do_vssrarn_u)

static __int128_t vsrarn(__int128_t s1, int64_t s2, int bit)
{
    int32_t n = (uint64_t)(s2 % bit);

    if (n == 0) {
        return s1;
    } else {
        uint64_t r_bit = (s1 >> (n  - 1)) & 1;
        return (s1 >> n) + r_bit;
    }
}

static void do_vssrlrni(vec_t *dest, vec_t *Vd, vec_t *Vj,
                        uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = sat_s(vsrlr((uint16_t)Vj->H[n], imm, bit), bit/2 - 1);
        dest->B[n + 128/bit] = sat_s(vsrlr((uint16_t)Vd->H[n], imm, bit),
	                             bit/2 -1);
        break;
    case 32:
        dest->H[n] = sat_s(vsrlr((uint32_t)Vj->W[n], imm, bit), bit/2 - 1);
        dest->H[n + 128/bit] = sat_s(vsrlr((uint32_t)Vd->W[n], imm, bit),
                                     bit/2 - 1);
        break;
    case 64:
        dest->W[n] = sat_s(vsrlr((uint64_t)Vj->D[n], imm, bit), bit/2 - 1);
        dest->W[n + 128/bit] = sat_s(vsrlr((uint64_t)Vd->D[n], imm, bit),
                                     bit/2 - 1);
        break;
    case 128:
        dest->D[n] = sat_s_128u(vsrlrn((__uint128_t)Vj->Q[n], imm), bit/2 - 1);
        dest->D[n + 128/bit] = sat_s_128u(vsrlrn((__uint128_t)Vd->Q[n], imm),
                                          bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vssrarni(vec_t *dest, vec_t *Vd, vec_t *Vj,
                        uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = sat_s(vsrar(Vj->H[n], imm, bit), bit/2 - 1);
        dest->B[n + 128/bit] = sat_s(vsrar(Vd->H[n], imm, bit), bit/2 - 1);
        break;
    case 32:
        dest->H[n] = sat_s(vsrar(Vj->W[n], imm, bit), bit/2 - 1);
        dest->H[n + 128/bit] = sat_s(vsrar(Vd->W[n], imm, bit), bit/2 - 1);
        break;
    case 64:
        dest->W[n] = sat_s(vsrar(Vj->D[n], imm, bit), bit/2 - 1);
        dest->W[n + 128/bit] = sat_s(vsrar(Vd->D[n], imm, bit), bit/2 - 1);
        break;
    case 128:
        dest->D[n] = sat_s_128(vsrarn((__int128_t)Vj->Q[n], imm, bit),
                               bit/2 - 1);
        dest->D[n + 128/bit] = sat_s_128(vsrarn((__int128_t)Vd->Q[n], imm, bit),
                                         bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_I(vssrlrni_b_h, 16, helper_vv_ni_c, do_vssrlrni)
DO_HELPER_VV_I(vssrlrni_h_w, 32, helper_vv_ni_c, do_vssrlrni)
DO_HELPER_VV_I(vssrlrni_w_d, 64, helper_vv_ni_c, do_vssrlrni)
DO_HELPER_VV_I(vssrlrni_d_q, 128, helper_vv_ni_c, do_vssrlrni)
DO_HELPER_VV_I(vssrarni_b_h, 16, helper_vv_ni_c, do_vssrarni)
DO_HELPER_VV_I(vssrarni_h_w, 32, helper_vv_ni_c, do_vssrarni)
DO_HELPER_VV_I(vssrarni_w_d, 64, helper_vv_ni_c, do_vssrarni)
DO_HELPER_VV_I(vssrarni_d_q, 128, helper_vv_ni_c, do_vssrarni)

static void do_vssrlrni_u(vec_t *dest, vec_t *Vd, vec_t *Vj,
                          uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = sat_u(vsrlr((uint16_t)Vj->H[n], imm, bit), bit/2 - 1);
        dest->B[n + 128/bit] = sat_u(vsrlr((uint16_t)Vd->H[n], imm, bit),
                                     bit/2 - 1);
        break;
    case 32:
        dest->H[n] = sat_u(vsrlr((uint32_t)Vj->W[n], imm, bit), bit/2 - 1);
        dest->H[n + 128/bit] = sat_u(vsrlr((uint32_t)Vd->W[n], imm, bit),
                                     bit/2 - 1);
        break;
    case 64:
        dest->W[n] = sat_u(vsrlr((uint64_t)Vj->D[n], imm, bit), bit/2 - 1);
        dest->W[n + 128/bit] = sat_u(vsrlr((uint64_t)Vd->D[n], imm, bit),
                                     bit/2 - 1);
        break;
    case 128:
        dest->D[n] = sat_u_128(vsrlrn((__uint128_t)Vj->Q[n], imm), bit/2 - 1);
        dest->D[n + 128/bit] = sat_u_128(vsrlrn((__uint128_t)Vd->Q[n], imm),
                                         bit/2 - 1);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vssrarni_u(vec_t *dest, vec_t *Vd, vec_t *Vj,
                          uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 16:
        dest->B[n] = sat_u(vsrar(Vj->H[n], imm, bit), bit/2 - 1);
        if (dest->B[n] < 0) {
            dest->B[n] = 0;
        }
        dest->B[n + 128/bit] = sat_u(vsrar(Vd->H[n], imm, bit), bit/2 - 1);
        if (dest->B[n + 128/bit] < 0) {
            dest->B[n + 128/bit] = 0;
        }
        break;
    case 32:
        dest->H[n] = sat_u(vsrar(Vj->W[n],imm, bit), bit/2 - 1);
        if (dest->H[n] < 0) {
            dest->H[n] = 0;
        }
        dest->H[n + 128/bit] = sat_u(vsrar(Vd->W[n], imm, bit), bit/2 - 1);
        if (dest->H[n + 128/bit] < 0) {
            dest->H[n + 128/bit] = 0;
        }
        break;
    case 64:
        dest->W[n] = sat_u(vsrar(Vj->D[n], imm, bit), bit/2 - 1);
        if (dest->W[n] < 0) {
            dest->W[n] = 0;
        }
        dest->W[n + 128/bit] = sat_u(vsrar(Vd->D[n], imm, bit), bit/2 - 1);
        if (dest->W[n + 128/bit] < 0) {
            dest->W[n + 128/bit] = 0;
        }
        break;
    case 128:
        dest->D[n] = sat_u_128(vsrarn((__int128_t)Vj->Q[n], imm, bit),
                               bit/2 - 1);
        if (dest->D[n] < 0) {
            dest->D[n] = 0;
        }
        dest->D[n + 128/bit] = sat_u_128(vsrarn((__int128_t)Vd->Q[n], imm, bit),
                                         bit/2 - 1);
        if (dest->D[n + 128/bit] < 0) {
            dest->D[n + 128/bit] = 0;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_I(vssrlrni_bu_h, 16, helper_vv_ni_c, do_vssrlrni_u)
DO_HELPER_VV_I(vssrlrni_hu_w, 32, helper_vv_ni_c, do_vssrlrni_u)
DO_HELPER_VV_I(vssrlrni_wu_d, 64, helper_vv_ni_c, do_vssrlrni_u)
DO_HELPER_VV_I(vssrlrni_du_q, 128, helper_vv_ni_c, do_vssrlrni_u)
DO_HELPER_VV_I(vssrarni_bu_h, 16, helper_vv_ni_c, do_vssrarni_u)
DO_HELPER_VV_I(vssrarni_hu_w, 32, helper_vv_ni_c, do_vssrarni_u)
DO_HELPER_VV_I(vssrarni_wu_d, 64, helper_vv_ni_c, do_vssrarni_u)
DO_HELPER_VV_I(vssrarni_du_q, 128, helper_vv_ni_c, do_vssrarni_u)

static void do_vclo(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = clz32((uint8_t)(~Vj->B[n])) - 24;
        break;
    case 16:
        Vd->H[n] = clz32((uint16_t)(~Vj->H[n])) - 16;
        break;
    case 32:
        Vd->W[n] = clz32((uint32_t)(~Vj->W[n]));
        break;
    case 64:
        Vd->D[n] = clz64((uint64_t)(~Vj->D[n]));
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vclz(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = clz32((uint8_t)Vj->B[n]) - 24;
        break;
    case 16:
        Vd->H[n] = clz32((uint16_t)Vj->H[n]) - 16;
        break;
    case 32:
        Vd->W[n] = clz32((uint32_t)Vj->W[n]);
        break;
    case 64:
        Vd->D[n] = clz64((uint64_t)Vj->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV(vclo_b, 8, helper_vv, do_vclo)
DO_HELPER_VV(vclo_h, 16, helper_vv, do_vclo)
DO_HELPER_VV(vclo_w, 32, helper_vv, do_vclo)
DO_HELPER_VV(vclo_d, 64, helper_vv, do_vclo)
DO_HELPER_VV(vclz_b, 8, helper_vv, do_vclz)
DO_HELPER_VV(vclz_h, 16, helper_vv, do_vclz)
DO_HELPER_VV(vclz_w, 32, helper_vv, do_vclz)
DO_HELPER_VV(vclz_d, 64, helper_vv, do_vclz)

static uint64_t vpcnt(int64_t s1, int bit)
{
    uint64_t u1 = s1 & MAKE_64BIT_MASK(0, bit);

    u1 = (u1 & 0x5555555555555555ULL) + ((u1 >>  1) & 0x5555555555555555ULL);
    u1 = (u1 & 0x3333333333333333ULL) + ((u1 >>  2) & 0x3333333333333333ULL);
    u1 = (u1 & 0x0F0F0F0F0F0F0F0FULL) + ((u1 >>  4) & 0x0F0F0F0F0F0F0F0FULL);
    u1 = (u1 & 0x00FF00FF00FF00FFULL) + ((u1 >>  8) & 0x00FF00FF00FF00FFULL);
    u1 = (u1 & 0x0000FFFF0000FFFFULL) + ((u1 >> 16) & 0x0000FFFF0000FFFFULL);
    u1 = (u1 & 0x00000000FFFFFFFFULL) + ((u1 >> 32));

    return u1;
}

static void do_vpcnt(vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vpcnt(Vj->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vpcnt(Vj->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vpcnt(Vj->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vpcnt(Vj->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV(vpcnt_b, 8, helper_vv, do_vpcnt)
DO_HELPER_VV(vpcnt_h, 16, helper_vv, do_vpcnt)
DO_HELPER_VV(vpcnt_w, 32, helper_vv, do_vpcnt)
DO_HELPER_VV(vpcnt_d, 64, helper_vv, do_vpcnt)

static int64_t vbitclr(int64_t s1, int64_t imm, int bit)
{
    return (s1 & (~(1LL << (imm % bit)))) & MAKE_64BIT_MASK(0, bit);
}

static void do_vbitclr(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vbitclr(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vbitclr(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vbitclr(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vbitclr(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vbitclr_i(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vbitclr(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vbitclr(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vbitclr(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vbitclr(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vbitclr_b, 8, helper_vvv, do_vbitclr)
DO_HELPER_VVV(vbitclr_h, 16, helper_vvv, do_vbitclr)
DO_HELPER_VVV(vbitclr_w, 32, helper_vvv, do_vbitclr)
DO_HELPER_VVV(vbitclr_d, 64, helper_vvv, do_vbitclr)
DO_HELPER_VV_I(vbitclri_b, 8, helper_vv_i, do_vbitclr_i)
DO_HELPER_VV_I(vbitclri_h, 16, helper_vv_i, do_vbitclr_i)
DO_HELPER_VV_I(vbitclri_w, 32, helper_vv_i, do_vbitclr_i)
DO_HELPER_VV_I(vbitclri_d, 64, helper_vv_i, do_vbitclr_i)

static int64_t vbitset(int64_t s1, int64_t imm, int bit)
{
    return (s1 | (1LL << (imm % bit))) & MAKE_64BIT_MASK(0, bit);
}

static void do_vbitset(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vbitset(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vbitset(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vbitset(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vbitset(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vbitset_i(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vbitset(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vbitset(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vbitset(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vbitset(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vbitset_b, 8, helper_vvv, do_vbitset)
DO_HELPER_VVV(vbitset_h, 16, helper_vvv, do_vbitset)
DO_HELPER_VVV(vbitset_w, 32, helper_vvv, do_vbitset)
DO_HELPER_VVV(vbitset_d, 64, helper_vvv, do_vbitset)
DO_HELPER_VV_I(vbitseti_b, 8, helper_vv_i, do_vbitset_i)
DO_HELPER_VV_I(vbitseti_h, 16, helper_vv_i, do_vbitset_i)
DO_HELPER_VV_I(vbitseti_w, 32, helper_vv_i, do_vbitset_i)
DO_HELPER_VV_I(vbitseti_d, 64, helper_vv_i, do_vbitset_i)

static int64_t vbitrev(int64_t s1, int64_t imm, int bit)
{
    return (s1 ^ (1LL << (imm % bit))) & MAKE_64BIT_MASK(0, bit);
}

static void do_vbitrev(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vbitrev(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vbitrev(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vbitrev(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vbitrev(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vbitrev_i(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vbitrev(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vbitrev(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vbitrev(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vbitrev(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vbitrev_b, 8, helper_vvv, do_vbitrev)
DO_HELPER_VVV(vbitrev_h, 16, helper_vvv, do_vbitrev)
DO_HELPER_VVV(vbitrev_w, 32, helper_vvv, do_vbitrev)
DO_HELPER_VVV(vbitrev_d, 64, helper_vvv, do_vbitrev)
DO_HELPER_VV_I(vbitrevi_b, 8, helper_vv_i, do_vbitrev_i)
DO_HELPER_VV_I(vbitrevi_h, 16, helper_vv_i, do_vbitrev_i)
DO_HELPER_VV_I(vbitrevi_w, 32, helper_vv_i, do_vbitrev_i)
DO_HELPER_VV_I(vbitrevi_d, 64, helper_vv_i, do_vbitrev_i)

void helper_vfrstp_b(CPULoongArchState *env,
                     uint32_t vd, uint32_t vj, uint32_t vk)
{
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t *Vk = &(env->fpr[vk].vec);

    int i;
    int m;
    for (i = 0; i < 128/8; i++) {
        if (Vj->B[i] < 0) {
            break;
        }
    }
    m = Vk->B[0] % 16;
    Vd->B[m] = (int8_t)i;
}

void helper_vfrstp_h(CPULoongArchState *env,
                     uint32_t vd, uint32_t vj, uint32_t vk)
{
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t *Vk = &(env->fpr[vk].vec);

    int i;
    int m;
    for (i = 0; i < 128/16; i++) {
        if (Vj->H[i] < 0) {
            break;
        }
    }
    m = Vk->H[0] % 8;
    Vd->H[m] = (int16_t)i;
}

void helper_vfrstpi_b(CPULoongArchState *env,
                      uint32_t vd, uint32_t vj, uint32_t imm)
{
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    int i;
    int m;
    for (i = 0; i < 128/8; i++) {
        if (Vj->B[i] < 0) {
            break;
        }
    }
    m = imm % 16;
    Vd->B[m] = (int8_t)i;
}

void helper_vfrstpi_h(CPULoongArchState *env,
                      uint32_t vd, uint32_t vj, uint32_t imm)
{
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    int i;
    int m;
    for (i = 0; i < 128/16; i++) {
        if (Vj->H[i] < 0){
            break;
        }
    }
    m = imm % 8;
    Vd->H[m] = (int16_t)i;
}

static void helper_vvv_f(CPULoongArchState *env,
                uint32_t vd, uint32_t vj, uint32_t vk, int bit,
                void (*func)(float_status*, vec_t*, vec_t*, vec_t*, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t *Vk = &(env->fpr[vk].vec);

    vec_t dest;
    dest.D[0] = 0;
    dest.D[1] = 0;
    for (i = 0; i < LSX_LEN/bit; i++) {
        func(&env->fp_status, &dest, Vj, Vk, bit, i);
    }
    Vd->D[0] = dest.D[0];
    Vd->D[1] = dest.D[1];
    update_fcsr0(env, GETPC());
}

#define LSX_DO_FARITH(name)                                           \
static void do_vf## name (float_status *status,                       \
                     vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n) \
{                                                                     \
    switch (bit) {                                                    \
    case 32:                                                          \
        Vd->W[n] = float32_## name (Vj->W[n], Vk->W[n], status);      \
        break;                                                        \
    case 64:                                                          \
        Vd->D[n] = float64_## name (Vj->D[n], Vk->D[n], status);      \
        break;                                                        \
    default:                                                          \
        g_assert_not_reached();                                       \
    }                                                                 \
}

LSX_DO_FARITH(add)
LSX_DO_FARITH(sub)
LSX_DO_FARITH(mul)
LSX_DO_FARITH(div)
LSX_DO_FARITH(maxnum)
LSX_DO_FARITH(minnum)
LSX_DO_FARITH(maxnummag)
LSX_DO_FARITH(minnummag)

DO_HELPER_VVV(vfadd_s, 32, helper_vvv_f, do_vfadd)
DO_HELPER_VVV(vfadd_d, 64, helper_vvv_f, do_vfadd)
DO_HELPER_VVV(vfsub_s, 32, helper_vvv_f, do_vfsub)
DO_HELPER_VVV(vfsub_d, 64, helper_vvv_f, do_vfsub)
DO_HELPER_VVV(vfmul_s, 32, helper_vvv_f, do_vfmul)
DO_HELPER_VVV(vfmul_d, 64, helper_vvv_f, do_vfmul)
DO_HELPER_VVV(vfdiv_s, 32, helper_vvv_f, do_vfdiv)
DO_HELPER_VVV(vfdiv_d, 64, helper_vvv_f, do_vfdiv)

static void helper_vvvv_f(CPULoongArchState *env,
                uint32_t vd, uint32_t vj, uint32_t vk, uint32_t va, int bit,
                void (*func)(float_status*, vec_t*, vec_t*, vec_t*,
                             vec_t*, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t *Vk = &(env->fpr[vk].vec);
    vec_t *Va = &(env->fpr[va].vec);

    vec_t dest;
    dest.D[0] = 0;
    dest.D[1] = 0;
    for (i = 0; i < LSX_LEN/bit; i++) {
        func(&env->fp_status, &dest, Vj, Vk, Va, bit, i);
    }
    Vd->D[0] = dest.D[0];
    Vd->D[1] = dest.D[1];
    update_fcsr0(env, GETPC());
}

#define LSX_DO_FMULADD(name, flags)                         \
static void do_vf## name (float_status *status,             \
                          vec_t *Vd, vec_t *Vj, vec_t *Vk,  \
                          vec_t *Va, int bit, int n)        \
{                                                           \
    switch (bit) {                                          \
    case 32:                                                \
        Vd->W[n] = float32_muladd(Vj->W[n], Vk->W[n],       \
                                  Va->W[n], flags, status); \
        break;                                              \
    case 64:                                                \
        Vd->D[n] = float64_muladd(Vj->D[n], Vk->D[n],       \
                                  Va->D[n], flags,status);  \
        break;                                              \
    default:                                                \
        g_assert_not_reached();                             \
    }                                                       \
}

LSX_DO_FMULADD(madd, 0)
LSX_DO_FMULADD(msub, float_muladd_negate_c)
LSX_DO_FMULADD(nmadd, float_muladd_negate_product | float_muladd_negate_c)
LSX_DO_FMULADD(nmsub, float_muladd_negate_product)

DO_HELPER_VVVV(vfmadd_s, 32, helper_vvvv_f, do_vfmadd)
DO_HELPER_VVVV(vfmadd_d, 64, helper_vvvv_f, do_vfmadd)
DO_HELPER_VVVV(vfmsub_s, 32, helper_vvvv_f, do_vfmsub)
DO_HELPER_VVVV(vfmsub_d, 64, helper_vvvv_f, do_vfmsub)
DO_HELPER_VVVV(vfnmadd_s, 32, helper_vvvv_f, do_vfnmadd)
DO_HELPER_VVVV(vfnmadd_d, 64, helper_vvvv_f, do_vfnmadd)
DO_HELPER_VVVV(vfnmsub_s, 32, helper_vvvv_f, do_vfnmsub)
DO_HELPER_VVVV(vfnmsub_d, 64, helper_vvvv_f, do_vfnmsub)

DO_HELPER_VVV(vfmax_s, 32, helper_vvv_f, do_vfmaxnum)
DO_HELPER_VVV(vfmax_d, 64, helper_vvv_f, do_vfmaxnum)
DO_HELPER_VVV(vfmin_s, 32, helper_vvv_f, do_vfminnum)
DO_HELPER_VVV(vfmin_d, 64, helper_vvv_f, do_vfminnum)

DO_HELPER_VVV(vfmaxa_s, 32, helper_vvv_f, do_vfmaxnummag)
DO_HELPER_VVV(vfmaxa_d, 64, helper_vvv_f, do_vfmaxnummag)
DO_HELPER_VVV(vfmina_s, 32, helper_vvv_f, do_vfminnummag)
DO_HELPER_VVV(vfmina_d, 64, helper_vvv_f, do_vfminnummag)

static void helper_vv_f(CPULoongArchState *env,
                uint32_t vd, uint32_t vj, int bit,
                void (*func)(CPULoongArchState*, vec_t*, vec_t*, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    vec_t dest;
    dest.D[0] = 0;
    dest.D[1] = 0;
    for (i = 0; i < LSX_LEN/bit; i++) {
        func(env, &dest, Vj, bit, i);
    }
    Vd->D[0] = dest.D[0];
    Vd->D[1] = dest.D[1];
}

#define LSX_DO_VV(name)                                     \
static void do_v## name (CPULoongArchState *env, vec_t *Vd, \
                          vec_t *Vj, int bit, int n)        \
{                                                           \
    switch (bit) {                                          \
    case 32:                                                \
        Vd->W[n] = helper_## name ## _s(env, Vj->W[n]);     \
        break;                                              \
    case 64:                                                \
        Vd->D[n] = helper_## name ## _d(env, Vj->D[n]);     \
        break;                                              \
    default:                                                \
        g_assert_not_reached();                             \
    }                                                       \
}                                                           \

LSX_DO_VV(flogb)
LSX_DO_VV(fclass)
LSX_DO_VV(fsqrt)
LSX_DO_VV(frecip)
LSX_DO_VV(frsqrt)
LSX_DO_VV(frint)

DO_HELPER_VV(vflogb_s, 32, helper_vv_f, do_vflogb)
DO_HELPER_VV(vflogb_d, 64, helper_vv_f, do_vflogb)

DO_HELPER_VV(vfclass_s, 32, helper_vv_f, do_vfclass)
DO_HELPER_VV(vfclass_d, 64, helper_vv_f, do_vfclass)

DO_HELPER_VV(vfsqrt_s, 32, helper_vv_f, do_vfsqrt)
DO_HELPER_VV(vfsqrt_d, 64, helper_vv_f, do_vfsqrt)
DO_HELPER_VV(vfrecip_s, 32, helper_vv_f, do_vfrecip)
DO_HELPER_VV(vfrecip_d, 64, helper_vv_f, do_vfrecip)
DO_HELPER_VV(vfrsqrt_s, 32, helper_vv_f, do_vfrsqrt)
DO_HELPER_VV(vfrsqrt_d, 64, helper_vv_f, do_vfrsqrt)

static void do_vfcvtl(CPULoongArchState *env, vec_t *Vd,
                      vec_t *Vj, int bit, int n)
{
    uint32_t s;
    uint64_t d;

    switch (bit) {
    case 32:
        s = float16_to_float32((uint16_t)Vj->H[n], true, &env->fp_status);
        Vd->W[n] = Vj->H[n] < 0 ? (s | (1 << 31)) : s;
        break;
    case 64:
        d = float32_to_float64((uint32_t)Vj->W[n], &env->fp_status);
        Vd->D[n] = Vj->W[n] < 0 ? (d | (1ULL << 63)) : d;
        break;
    default:
        g_assert_not_reached();
    }
    update_fcsr0(env, GETPC());
}

static void do_vfcvth(CPULoongArchState *env, vec_t *Vd,
                      vec_t *Vj, int bit, int n)
{
    uint32_t s;
    uint64_t d;

    switch (bit) {
    case 32:
        s = float16_to_float32((uint16_t)Vj->H[n + 4], true, &env->fp_status);
        Vd->W[n] = Vj->H[n + 4] < 0 ? (s | (1 << 31)) : s;
        break;
    case 64:
        d = float32_to_float64((uint32_t)Vj->W[n + 2], &env->fp_status);
        Vd->D[n] = Vj->W[n + 2] < 0 ? (d | (1ULL << 63)) : d;
        break;
    default:
        g_assert_not_reached();
    }
    update_fcsr0(env, GETPC());
}

static void do_vfcvt(float_status *status, vec_t *Vd,
                      vec_t *Vj, vec_t *Vk, int bit, int n)
{
    uint16_t H_h, H_l;
    uint32_t S_h, S_l;

    switch (bit) {
    case 32:
        H_h = float32_to_float16((uint32_t)Vj->W[n], true, status);
        H_l = float32_to_float16((uint32_t)Vk->W[n], true, status);
        Vd->H[n + 4] = Vj->W[n] < 0 ? (H_h | (1 << 15)) : H_h;
        Vd->H[n] = Vk->W[n] < 0 ? (H_l | (1 << 15)) : H_l;
        break;
    case 64:
        S_h = float64_to_float32((uint64_t)Vj->D[n], status);
        S_l = float64_to_float32((uint64_t)Vk->D[n], status);
        Vd->W[n + 2] = Vj->D[n] < 0 ? (S_h | (1 << 31)) : S_h;
        Vd->W[n] = Vk->D[n] < 0 ? (S_l | (1 << 31)) : S_l;
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV(vfcvtl_s_h, 32, helper_vv_f, do_vfcvtl)
DO_HELPER_VV(vfcvth_s_h, 32, helper_vv_f, do_vfcvth)
DO_HELPER_VV(vfcvtl_d_s, 64, helper_vv_f, do_vfcvtl)
DO_HELPER_VV(vfcvth_d_s, 64, helper_vv_f, do_vfcvth)
DO_HELPER_VVV(vfcvt_h_s, 32, helper_vvv_f, do_vfcvt)
DO_HELPER_VVV(vfcvt_s_d, 64, helper_vvv_f, do_vfcvt)

#define LSX_FRINT_RM(rm)                                                   \
static void do_vfrint## rm (CPULoongArchState *env, vec_t *Vd,             \
                          vec_t *Vj, int bit, int n)                       \
{                                                                          \
    switch (bit) {                                                         \
    case 32:                                                               \
        Vd->W[n] = float32_round_to_int_## rm (Vj->W[n], &env->fp_status); \
        break;                                                             \
    case 64:                                                               \
        Vd->D[n] = float64_round_to_int_## rm (Vj->D[n], &env->fp_status); \
        break;                                                             \
    default:                                                               \
        g_assert_not_reached();                                            \
    }                                                                      \
    update_fcsr0(env, GETPC());                                            \
}

LSX_FRINT_RM(rne)
LSX_FRINT_RM(rz)
LSX_FRINT_RM(rp)
LSX_FRINT_RM(rm)

DO_HELPER_VV(vfrintrne_s, 32, helper_vv_f, do_vfrintrne)
DO_HELPER_VV(vfrintrne_d, 64, helper_vv_f, do_vfrintrne)
DO_HELPER_VV(vfrintrz_s, 32, helper_vv_f, do_vfrintrz)
DO_HELPER_VV(vfrintrz_d, 64, helper_vv_f, do_vfrintrz)
DO_HELPER_VV(vfrintrp_s, 32, helper_vv_f, do_vfrintrp)
DO_HELPER_VV(vfrintrp_d, 64, helper_vv_f, do_vfrintrp)
DO_HELPER_VV(vfrintrm_s, 32, helper_vv_f, do_vfrintrm)
DO_HELPER_VV(vfrintrm_d, 64, helper_vv_f, do_vfrintrm)
DO_HELPER_VV(vfrint_s, 32, helper_vv_f, do_vfrint)
DO_HELPER_VV(vfrint_d, 64, helper_vv_f, do_vfrint)

#define LSX_FTINT_RM(name)                                  \
static void do_v## name (CPULoongArchState *env, vec_t *Vd, \
                          vec_t *Vj, int bit, int n)        \
{                                                           \
    switch (bit) {                                          \
    case 32:                                                \
        Vd->W[n] = helper_## name ## _w_s(env, Vj->W[n]);   \
        break;                                              \
    case 64:                                                \
        Vd->D[n] = helper_## name ## _l_d(env, Vj->D[n]);   \
        break;                                              \
    default:                                                \
        g_assert_not_reached();                             \
    }                                                       \
}                                                           \

LSX_FTINT_RM(ftintrne)
LSX_FTINT_RM(ftintrp)
LSX_FTINT_RM(ftintrz)
LSX_FTINT_RM(ftintrm)
LSX_FTINT_RM(ftint)

DO_HELPER_VV(vftintrne_w_s, 32, helper_vv_f, do_vftintrne)
DO_HELPER_VV(vftintrne_l_d, 64, helper_vv_f, do_vftintrne)
DO_HELPER_VV(vftintrp_w_s, 32, helper_vv_f, do_vftintrp)
DO_HELPER_VV(vftintrp_l_d, 64, helper_vv_f, do_vftintrp)
DO_HELPER_VV(vftintrz_w_s, 32, helper_vv_f, do_vftintrz)
DO_HELPER_VV(vftintrz_l_d, 64, helper_vv_f, do_vftintrz)
DO_HELPER_VV(vftintrm_w_s, 32, helper_vv_f, do_vftintrm)
DO_HELPER_VV(vftintrm_l_d, 64, helper_vv_f, do_vftintrm)
DO_HELPER_VV(vftint_w_s, 32, helper_vv_f, do_vftint)
DO_HELPER_VV(vftint_l_d, 64, helper_vv_f, do_vftint)

static void do_vftintrz_u(CPULoongArchState *env, vec_t *Vd,
                          vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 32:
        Vd->W[n] = float32_to_uint32_round_to_zero(Vj->W[n], &env->fp_status);
        break;
    case 64:
        Vd->D[n] = float64_to_uint64_round_to_zero(Vj->D[n], &env->fp_status);
        break;
    default:
        g_assert_not_reached();
    }
    update_fcsr0(env, GETPC());
}

static void do_vftint_u(CPULoongArchState *env, vec_t *Vd,
                        vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 32:
        Vd->W[n] = float32_to_uint32(Vj->W[n], &env->fp_status);
        break;
    case 64:
        Vd->D[n] = float64_to_uint64(Vj->D[n], &env->fp_status);
        break;
    default:
        g_assert_not_reached();
    }
    update_fcsr0(env, GETPC());
}

DO_HELPER_VV(vftintrz_wu_s, 32, helper_vv_f, do_vftintrz_u)
DO_HELPER_VV(vftintrz_lu_d, 64, helper_vv_f, do_vftintrz_u)
DO_HELPER_VV(vftint_wu_s, 32, helper_vv_f, do_vftint_u)
DO_HELPER_VV(vftint_lu_d, 64, helper_vv_f, do_vftint_u)

#define LSX_FTINT_W_D(name)                                      \
void helper_v## name ##_w_d(CPULoongArchState *env, uint32_t vd, \
                            uint32_t vj, uint32_t vk)            \
{                                                                \
    int i;                                                       \
    vec_t *Vd = &(env->fpr[vd].vec);                             \
    vec_t *Vj = &(env->fpr[vj].vec);                             \
    vec_t *Vk = &(env->fpr[vk].vec);                             \
                                                                 \
    vec_t dest;                                                  \
    dest.D[0] = 0;                                               \
    dest.D[1] = 0;                                               \
    for (i = 0; i < 2; i++) {                                    \
        dest.W[i + 2] = helper_## name ## _w_d(env, Vj->D[i]);   \
        dest.W[i]  = helper_## name ## _w_d(env, Vk->D[i]);      \
    }                                                            \
    Vd->D[0] = dest.D[0];                                        \
    Vd->D[1] = dest.D[1];                                        \
}

LSX_FTINT_W_D(ftintrne)
LSX_FTINT_W_D(ftintrz)
LSX_FTINT_W_D(ftintrp)
LSX_FTINT_W_D(ftintrm)
LSX_FTINT_W_D(ftint)

#define LSX_FTINTL_L_S(name)                                       \
static void do_v## name ##l_l_s(CPULoongArchState *env, vec_t *Vd, \
                                vec_t *Vj, int bit, int n)         \
{                                                                  \
     Vd->D[n]  = helper_## name ## _l_s(env, Vj->W[n]);            \
}                                                                  \

LSX_FTINTL_L_S(ftintrne)
LSX_FTINTL_L_S(ftintrz)
LSX_FTINTL_L_S(ftintrp)
LSX_FTINTL_L_S(ftintrm)
LSX_FTINTL_L_S(ftint)

#define LSX_FTINTH_L_S(name)                                       \
static void do_v## name ##h_l_s(CPULoongArchState *env, vec_t *Vd, \
                                vec_t *Vj, int bit, int n)         \
{                                                                  \
     Vd->D[n]  = helper_## name ## _l_s(env, Vj->W[n + 2]);        \
}                                                                  \

LSX_FTINTH_L_S(ftintrne)
LSX_FTINTH_L_S(ftintrz)
LSX_FTINTH_L_S(ftintrp)
LSX_FTINTH_L_S(ftintrm)
LSX_FTINTH_L_S(ftint)

DO_HELPER_VV(vftintrnel_l_s, 64, helper_vv_f, do_vftintrnel_l_s)
DO_HELPER_VV(vftintrneh_l_s, 64, helper_vv_f, do_vftintrneh_l_s)
DO_HELPER_VV(vftintrzl_l_s, 64, helper_vv_f, do_vftintrzl_l_s)
DO_HELPER_VV(vftintrzh_l_s, 64, helper_vv_f, do_vftintrzh_l_s)
DO_HELPER_VV(vftintrpl_l_s, 64, helper_vv_f, do_vftintrpl_l_s)
DO_HELPER_VV(vftintrph_l_s, 64, helper_vv_f, do_vftintrph_l_s)
DO_HELPER_VV(vftintrml_l_s, 64, helper_vv_f, do_vftintrml_l_s)
DO_HELPER_VV(vftintrmh_l_s, 64, helper_vv_f, do_vftintrmh_l_s)
DO_HELPER_VV(vftintl_l_s, 64, helper_vv_f, do_vftintl_l_s)
DO_HELPER_VV(vftinth_l_s, 64, helper_vv_f, do_vftinth_l_s)

static void do_vffint_s(CPULoongArchState *env,
                        vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 32:
        Vd->W[n] = int32_to_float32(Vj->W[n], &env->fp_status);
        break;
    case 64:
        Vd->D[n] = int64_to_float64(Vj->D[n], &env->fp_status);
        break;
    default:
        g_assert_not_reached();
    }
    update_fcsr0(env, GETPC());
}

static void do_vffint_u(CPULoongArchState *env,
                        vec_t *Vd, vec_t *Vj, int bit, int n)
{
    switch (bit) {
    case 32:
        Vd->W[n] = uint32_to_float32(Vj->W[n], &env->fp_status);
        break;
    case 64:
        Vd->D[n] = uint64_to_float64(Vj->D[n], &env->fp_status);
        break;
    default:
        g_assert_not_reached();
    }
    update_fcsr0(env, GETPC());
}

static void do_vffintl_d_w(CPULoongArchState *env,
                           vec_t *Vd, vec_t *Vj, int bit, int n)
{
    Vd->D[n] = int32_to_float64(Vj->W[n], &env->fp_status);
    update_fcsr0(env, GETPC());
}

static void do_vffinth_d_w(CPULoongArchState *env,
                           vec_t *Vd, vec_t *Vj, int bit, int n)
{
    Vd->D[n] = int32_to_float64(Vj->W[n + 2], &env->fp_status);
    update_fcsr0(env, GETPC());
}

void helper_vffint_s_l(CPULoongArchState *env,
                       uint32_t vd, uint32_t vj, uint32_t vk)
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t *Vk = &(env->fpr[vk].vec);

    vec_t dest;
    dest.D[0] = 0;
    dest.D[1] = 0;
    for (i = 0; i < 2; i++) {
        dest.W[i + 2] = int64_to_float32(Vj->D[i], &env->fp_status);
        dest.W[i]  = int64_to_float32(Vk->D[i], &env->fp_status);
    }
    Vd->D[0] = dest.D[0];
    Vd->D[1] = dest.D[1];
}

DO_HELPER_VV(vffint_s_w, 32, helper_vv_f, do_vffint_s)
DO_HELPER_VV(vffint_d_l, 64, helper_vv_f, do_vffint_s)
DO_HELPER_VV(vffint_s_wu, 32, helper_vv_f, do_vffint_u)
DO_HELPER_VV(vffint_d_lu, 64, helper_vv_f, do_vffint_u)
DO_HELPER_VV(vffintl_d_w, 64, helper_vv_f, do_vffintl_d_w)
DO_HELPER_VV(vffinth_d_w, 64, helper_vv_f, do_vffinth_d_w)

static int64_t vseq(int64_t s1, int64_t s2)
{
    return s1 == s2 ? -1: 0;
}

static void do_vseq(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vseq(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vseq(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vseq(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vseq(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vseqi(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vseq(Vj->B[n], imm);
        break;
    case 16:
        Vd->H[n] = vseq(Vj->H[n], imm);
        break;
    case 32:
        Vd->W[n] = vseq(Vj->W[n], imm);
        break;
    case 64:
        Vd->D[n] = vseq(Vj->D[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vseq_b, 8, helper_vvv, do_vseq)
DO_HELPER_VVV(vseq_h, 16, helper_vvv, do_vseq)
DO_HELPER_VVV(vseq_w, 32, helper_vvv, do_vseq)
DO_HELPER_VVV(vseq_d, 64, helper_vvv, do_vseq)
DO_HELPER_VVV(vseqi_b, 8, helper_vv_i, do_vseqi)
DO_HELPER_VVV(vseqi_h, 16, helper_vv_i, do_vseqi)
DO_HELPER_VVV(vseqi_w, 32, helper_vv_i, do_vseqi)
DO_HELPER_VVV(vseqi_d, 64, helper_vv_i, do_vseqi)

static int64_t vsle_s(int64_t s1, int64_t s2)
{
    return s1 <= s2 ? -1 : 0;
}

static void do_vsle_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsle_s(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vsle_s(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vsle_s(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vsle_s(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vslei_s(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsle_s(Vj->B[n], imm);
        break;
    case 16:
        Vd->H[n] = vsle_s(Vj->H[n], imm);
        break;
    case 32:
        Vd->W[n] = vsle_s(Vj->W[n], imm);
        break;
    case 64:
        Vd->D[n] = vsle_s(Vj->D[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsle_b, 8, helper_vvv, do_vsle_s)
DO_HELPER_VVV(vsle_h, 16, helper_vvv, do_vsle_s)
DO_HELPER_VVV(vsle_w, 32, helper_vvv, do_vsle_s)
DO_HELPER_VVV(vsle_d, 64, helper_vvv, do_vsle_s)
DO_HELPER_VVV(vslei_b, 8, helper_vv_i, do_vslei_s)
DO_HELPER_VVV(vslei_h, 16, helper_vv_i, do_vslei_s)
DO_HELPER_VVV(vslei_w, 32, helper_vv_i, do_vslei_s)
DO_HELPER_VVV(vslei_d, 64, helper_vv_i, do_vslei_s)

static int64_t vsle_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;

    return u1 <= u2 ? -1 : 0;
}

static void do_vsle_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int  bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsle_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vsle_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vsle_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vsle_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vslei_u(vec_t *Vd, vec_t *Vj, uint32_t imm, int  bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vsle_u(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vsle_u(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vsle_u(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vsle_u(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vsle_bu, 8, helper_vvv, do_vsle_u)
DO_HELPER_VVV(vsle_hu, 16, helper_vvv, do_vsle_u)
DO_HELPER_VVV(vsle_wu, 32, helper_vvv, do_vsle_u)
DO_HELPER_VVV(vsle_du, 64, helper_vvv, do_vsle_u)
DO_HELPER_VVV(vslei_bu, 8, helper_vv_i, do_vslei_u)
DO_HELPER_VVV(vslei_hu, 16, helper_vv_i, do_vslei_u)
DO_HELPER_VVV(vslei_wu, 32, helper_vv_i, do_vslei_u)
DO_HELPER_VVV(vslei_du, 64, helper_vv_i, do_vslei_u)

static int64_t vslt_s(int64_t s1, int64_t s2)
{
    return s1 < s2 ? -1 : 0;
}

static void do_vslt_s(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vslt_s(Vj->B[n], Vk->B[n]);
        break;
    case 16:
        Vd->H[n] = vslt_s(Vj->H[n], Vk->H[n]);
        break;
    case 32:
        Vd->W[n] = vslt_s(Vj->W[n], Vk->W[n]);
        break;
    case 64:
        Vd->D[n] = vslt_s(Vj->D[n], Vk->D[n]);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vslti_s(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vslt_s(Vj->B[n], imm);
        break;
    case 16:
        Vd->H[n] = vslt_s(Vj->H[n], imm);
        break;
    case 32:
        Vd->W[n] = vslt_s(Vj->W[n], imm);
        break;
    case 64:
        Vd->D[n] = vslt_s(Vj->D[n], imm);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vslt_b, 8, helper_vvv, do_vslt_s)
DO_HELPER_VVV(vslt_h, 16, helper_vvv, do_vslt_s)
DO_HELPER_VVV(vslt_w, 32, helper_vvv, do_vslt_s)
DO_HELPER_VVV(vslt_d, 64, helper_vvv, do_vslt_s)
DO_HELPER_VVV(vslti_b, 8, helper_vv_i, do_vslti_s)
DO_HELPER_VVV(vslti_h, 16, helper_vv_i, do_vslti_s)
DO_HELPER_VVV(vslti_w, 32, helper_vv_i, do_vslti_s)
DO_HELPER_VVV(vslti_d, 64, helper_vv_i, do_vslti_s)

static int64_t vslt_u(int64_t s1, int64_t s2, int bit)
{
    uint64_t umax = MAKE_64BIT_MASK(0, bit);
    uint64_t u1 = s1 & umax;
    uint64_t u2 = s2 & umax;

    return u1 < u2 ? -1 : 0;
}

static void do_vslt_u(vec_t *Vd, vec_t *Vj, vec_t *Vk, int  bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vslt_u(Vj->B[n], Vk->B[n], bit);
        break;
    case 16:
        Vd->H[n] = vslt_u(Vj->H[n], Vk->H[n], bit);
        break;
    case 32:
        Vd->W[n] = vslt_u(Vj->W[n], Vk->W[n], bit);
        break;
    case 64:
        Vd->D[n] = vslt_u(Vj->D[n], Vk->D[n], bit);
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vslti_u(vec_t *Vd, vec_t *Vj, uint32_t imm, int  bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = vslt_u(Vj->B[n], imm, bit);
        break;
    case 16:
        Vd->H[n] = vslt_u(Vj->H[n], imm, bit);
        break;
    case 32:
        Vd->W[n] = vslt_u(Vj->W[n], imm, bit);
        break;
    case 64:
        Vd->D[n] = vslt_u(Vj->D[n], imm, bit);
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vslt_bu, 8, helper_vvv, do_vslt_u)
DO_HELPER_VVV(vslt_hu, 16, helper_vvv, do_vslt_u)
DO_HELPER_VVV(vslt_wu, 32, helper_vvv, do_vslt_u)
DO_HELPER_VVV(vslt_du, 64, helper_vvv, do_vslt_u)
DO_HELPER_VVV(vslti_bu, 8, helper_vv_i, do_vslti_u)
DO_HELPER_VVV(vslti_hu, 16, helper_vv_i, do_vslti_u)
DO_HELPER_VVV(vslti_wu, 32, helper_vv_i, do_vslti_u)
DO_HELPER_VVV(vslti_du, 64, helper_vv_i, do_vslti_u)

#define LSX_FCMP_S(name)                                            \
void helper_v## name ##_s(CPULoongArchState *env, uint32_t vd,      \
                          uint32_t vj, uint32_t vk, uint32_t flags) \
{                                                                   \
    int ret;                                                        \
    int i;                                                          \
    vec_t *Vd = &(env->fpr[vd].vec);                                \
    vec_t *Vj = &(env->fpr[vj].vec);                                \
    vec_t *Vk = &(env->fpr[vk].vec);                                \
                                                                    \
    for (i = 0; i < 4; i++) {                                       \
        ret = helper_## name ## _s(env, Vj->W[i], Vk->W[i], flags); \
        Vd->W[i] = (ret == 1) ? -1 : 0;                             \
    }                                                               \
}

LSX_FCMP_S(fcmp_c)
LSX_FCMP_S(fcmp_s)

#define LSX_FCMP_D(name)                                            \
void helper_v## name ##_d(CPULoongArchState *env, uint32_t vd,      \
                          uint32_t vj, uint32_t vk, uint32_t flags) \
{                                                                   \
    int ret;                                                        \
    int i;                                                          \
    vec_t *Vd = &(env->fpr[vd].vec);                                \
    vec_t *Vj = &(env->fpr[vj].vec);                                \
    vec_t *Vk = &(env->fpr[vk].vec);                                \
                                                                    \
    for (i = 0; i < 2; i++) {                                       \
        ret = helper_## name ## _d(env, Vj->D[i], Vk->D[i], flags); \
        Vd->D[i] = (ret == 1) ? -1 : 0;                             \
    }                                                               \
}

LSX_FCMP_D(fcmp_c)
LSX_FCMP_D(fcmp_s)

void helper_vbitsel_v(CPULoongArchState *env,
                      uint32_t vd, uint32_t vj, uint32_t vk, uint32_t va)
{
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t *Vk = &(env->fpr[vk].vec);
    vec_t *Va = &(env->fpr[va].vec);

    Vd->D[0] = (Vk->D[0] & Va->D[0]) | (Vj->D[0] & ~(Va->D[0]));
    Vd->D[1] = (Vk->D[1] & Va->D[1]) | (Vj->D[1] & ~(Va->D[1]));
}

static void do_vbitseli_b(vec_t *Vd, vec_t *Vj, uint32_t imm, int bit, int n)
{
    Vd->B[n] = (~Vd->B[n] & Vj->B[n] ) | (Vd->B[n] & imm);
}

DO_HELPER_VV_I(vbitseli_b, 8, helper_vv_i, do_vbitseli_b)

void helper_vseteqz_v(CPULoongArchState *env, uint32_t cd, uint32_t vj)
{
    vec_t *Vj = &(env->fpr[vj].vec);
    env->cf[cd & 0x7] = (Vj->Q[0] == 0);
}

void helper_vsetnez_v(CPULoongArchState *env, uint32_t cd, uint32_t vj)
{
    vec_t *Vj = &(env->fpr[vj].vec);
    env->cf[cd & 0x7] = (Vj->Q[0] != 0);
}

static void helper_setanyeqz(CPULoongArchState *env,
                             uint32_t cd, uint32_t vj, int bit,
                             bool (*func)(vec_t*, int, int))
{
    int i;
    bool ret = false;
    vec_t *Vj = &(env->fpr[vj].vec);

    for (i = 0; i < LSX_LEN/bit; i++) {
        ret |= func(Vj, bit, i);
    }
    env->cf[cd & 0x7] = ret;
}

static void helper_setallnez(CPULoongArchState *env,
                             uint32_t cd, uint32_t vj, int bit,
                             bool (*func)(vec_t*, int, int))
{
    int i;
    bool ret = true;
    vec_t *Vj = &(env->fpr[vj].vec);

    for (i = 0; i < LSX_LEN/bit; i++) {
        ret &= func(Vj, bit, i);
    }
    env->cf[cd & 0x7] = ret;
}

static bool do_setanyeqz(vec_t *Vj, int bit, int n)
{
    bool ret = false;
    switch (bit) {
    case 8:
        ret = (Vj->B[n] == 0);
        break;
    case 16:
        ret = (Vj->H[n] == 0);
        break;
    case 32:
        ret = (Vj->W[n] == 0);
        break;
    case 64:
        ret = (Vj->D[n] == 0);
        break;
    default:
        g_assert_not_reached();
    }
    return ret;
}

static bool do_setallnez(vec_t *Vj, int bit, int n)
{
    bool ret = false;
    switch (bit) {
    case 8:
        ret = (Vj->B[n] != 0);
        break;
    case 16:
        ret = (Vj->H[n] != 0);
        break;
    case 32:
        ret = (Vj->W[n] != 0);
        break;
    case 64:
        ret = (Vj->D[n] != 0);
        break;
    default:
        g_assert_not_reached();
    }
    return ret;
}

DO_HELPER_CV(vsetanyeqz_b, 8, helper_setanyeqz, do_setanyeqz)
DO_HELPER_CV(vsetanyeqz_h, 16, helper_setanyeqz, do_setanyeqz)
DO_HELPER_CV(vsetanyeqz_w, 32, helper_setanyeqz, do_setanyeqz)
DO_HELPER_CV(vsetanyeqz_d, 64, helper_setanyeqz, do_setanyeqz)
DO_HELPER_CV(vsetallnez_b, 8, helper_setallnez, do_setallnez)
DO_HELPER_CV(vsetallnez_h, 16, helper_setallnez, do_setallnez)
DO_HELPER_CV(vsetallnez_w, 32, helper_setallnez, do_setallnez)
DO_HELPER_CV(vsetallnez_d, 64, helper_setallnez, do_setallnez)

static void helper_vr_i(CPULoongArchState *env,
                        uint32_t vd, uint32_t rj, uint32_t imm, int bit,
                        void (*func)(vec_t*, uint64_t, uint32_t, int))
{
    vec_t *Vd = &(env->fpr[vd].vec);
    uint64_t Rj = env->gpr[rj];

    imm %= (LSX_LEN/bit);

    func(Vd, Rj, imm, bit);
}

static void do_insgr2vr(vec_t *Vd, uint64_t value, uint32_t imm, int bit)
{
    switch (bit) {
    case 8:
        Vd->B[imm] = (int8_t)value;
        break;
    case 16:
        Vd->H[imm] = (int16_t)value;
        break;
    case 32:
        Vd->W[imm] = (int32_t)value;
        break;
    case 64:
        Vd->D[imm] = (int64_t)value;
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VR_I(vinsgr2vr_b, 8, helper_vr_i, do_insgr2vr)
DO_HELPER_VR_I(vinsgr2vr_h, 16, helper_vr_i, do_insgr2vr)
DO_HELPER_VR_I(vinsgr2vr_w, 32, helper_vr_i, do_insgr2vr)
DO_HELPER_VR_I(vinsgr2vr_d, 64, helper_vr_i, do_insgr2vr)

static void helper_rv_i(CPULoongArchState *env,
                        uint32_t rd, uint32_t vj, uint32_t imm, int bit,
                        void (*func)(CPULoongArchState*, uint32_t, vec_t*,
                                     uint32_t, int))
{
    vec_t *Vj = &(env->fpr[vj].vec);

    imm %=(LSX_LEN/bit);

    func(env, rd, Vj, imm, bit);
}

static void do_pickve2gr_s(CPULoongArchState *env,
                           uint32_t rd, vec_t *Vj, uint32_t imm, int bit)
{
    switch (bit) {
    case 8:
        env->gpr[rd] = Vj->B[imm];
        break;
    case 16:
        env->gpr[rd] = Vj->H[imm];
        break;
    case 32:
        env->gpr[rd] = Vj->W[imm];
        break;
    case 64:
        env->gpr[rd] = Vj->D[imm];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_pickve2gr_u(CPULoongArchState *env,
                           uint32_t rd, vec_t *Vj, uint32_t imm, int bit)
{
    switch (bit) {
    case 8:
        env->gpr[rd] = (uint8_t)Vj->B[imm];
        break;
    case 16:
        env->gpr[rd] = (uint16_t)Vj->H[imm];
        break;
    case 32:
        env->gpr[rd] = (uint32_t)Vj->W[imm];
        break;
    case 64:
        env->gpr[rd] = (uint64_t)Vj->D[imm];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_RV_I(vpickve2gr_b, 8, helper_rv_i, do_pickve2gr_s)
DO_HELPER_RV_I(vpickve2gr_h, 16, helper_rv_i, do_pickve2gr_s)
DO_HELPER_RV_I(vpickve2gr_w, 32, helper_rv_i, do_pickve2gr_s)
DO_HELPER_RV_I(vpickve2gr_d, 64, helper_rv_i, do_pickve2gr_s)
DO_HELPER_RV_I(vpickve2gr_bu, 8, helper_rv_i, do_pickve2gr_u)
DO_HELPER_RV_I(vpickve2gr_hu, 16, helper_rv_i, do_pickve2gr_u)
DO_HELPER_RV_I(vpickve2gr_wu, 32, helper_rv_i, do_pickve2gr_u)
DO_HELPER_RV_I(vpickve2gr_du, 64, helper_rv_i, do_pickve2gr_u)

static void helper_vr(CPULoongArchState *env,
                      uint32_t vd, uint32_t rj, int bit,
                      void (*func)(CPULoongArchState*,
                                   vec_t*, uint32_t,  int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);

    for (i = 0; i < LSX_LEN/bit; i++) {
        func(env, Vd, rj, bit, i);
    }
}

static void do_replgr2vr(CPULoongArchState *env,
                         vec_t *Vd, uint32_t rj, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n] = (int8_t)env->gpr[rj];
        break;
    case 16:
        Vd->H[n] = (int16_t)env->gpr[rj];
        break;
    case 32:
        Vd->W[n] = (int32_t)env->gpr[rj];
        break;
    case 64:
        Vd->D[n] = (int64_t)env->gpr[rj];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VR(vreplgr2vr_b, 8, helper_vr, do_replgr2vr)
DO_HELPER_VR(vreplgr2vr_h, 16, helper_vr, do_replgr2vr)
DO_HELPER_VR(vreplgr2vr_w, 32, helper_vr, do_replgr2vr)
DO_HELPER_VR(vreplgr2vr_d, 64, helper_vr, do_replgr2vr)

static void helper_vreplve(CPULoongArchState *env,
                           uint32_t vd, uint32_t vj, uint32_t rk, int bit,
                           void (*func)(vec_t*, vec_t*, uint64_t, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    for (i = 0; i < LSX_LEN/bit; i++) {
        func(Vd, Vj, env->gpr[rk], bit, i);
    }
}

static void helper_vreplvei(CPULoongArchState *env,
                            uint32_t vd, uint32_t vj, uint32_t imm, int bit,
                            void (*func)(vec_t*, vec_t*, uint64_t, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    for (i = 0; i < LSX_LEN/bit; i++) {
        func(Vd, Vj, imm, bit, i);
    }
}

static void do_vreplve(vec_t *Vd, vec_t *Vj, uint64_t value, int bit, int n)
{
    uint32_t index = value % (LSX_LEN/bit);
    switch (bit) {
    case 8:
        Vd->B[n] = Vj->B[index];
        break;
    case 16:
        Vd->H[n] = Vj->H[index];
        break;
    case 32:
        Vd->W[n] = Vj->W[index];
        break;
    case 64:
        Vd->D[n] = Vj->D[index];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VV_R(vreplve_b, 8, helper_vreplve, do_vreplve)
DO_HELPER_VV_R(vreplve_h, 16, helper_vreplve, do_vreplve)
DO_HELPER_VV_R(vreplve_w, 32, helper_vreplve, do_vreplve)
DO_HELPER_VV_R(vreplve_d, 64, helper_vreplve, do_vreplve)
DO_HELPER_VV_I(vreplvei_b, 8, helper_vreplvei, do_vreplve)
DO_HELPER_VV_I(vreplvei_h, 16, helper_vreplvei, do_vreplve)
DO_HELPER_VV_I(vreplvei_w, 32, helper_vreplvei, do_vreplve)
DO_HELPER_VV_I(vreplvei_d, 64, helper_vreplvei, do_vreplve)

void helper_vbsll_v(CPULoongArchState *env,
                    uint32_t vd, uint32_t vj, uint32_t imm)
{
    uint32_t idx, i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t tmp;

    tmp.D[0] = Vd->D[0];
    tmp.D[1] = Vd->D[1];
    idx = (imm & 0xf);
    for(i = 0; i < 16; i++) {
        tmp.B[i]  = (i < idx) ? 0 : Vj->B[i - idx];
    }
    Vd->D[0] = tmp.D[0];
    Vd->D[1] = tmp.D[1];
}

void helper_vbsrl_v(CPULoongArchState *env,
                    uint32_t vd, uint32_t vj, uint32_t imm)
{
    uint32_t idx, i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);

    idx = (imm & 0xf);
    for(i = 0; i < 16; i++) {
        Vd->B[i]  = (i + idx > 15) ? 0 : Vj->B[i + idx];
    }
}

static void helper_vvv_c(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t vk, int bit,
                        void (*func)(vec_t*, vec_t*, vec_t*, int, int))
{
    int i;
    vec_t *Vd = &(env->fpr[vd].vec);
    vec_t *Vj = &(env->fpr[vj].vec);
    vec_t *Vk = &(env->fpr[vk].vec);

    vec_t temp;
    temp.D[0] = 0;
    temp.D[1] = 0;

    for (i = 0; i < LSX_LEN/bit/2; i++) {
        func(&temp, Vj, Vk, bit, i);
    }
    Vd->D[0] = temp.D[0];
    Vd->D[1] = temp.D[1];
}

static void do_vpackev(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[2 * n + 1] = Vj->B[2 * n];
        Vd->B[2 * n] = Vk->B[2 * n];
        break;
    case 16:
        Vd->H[2 * n + 1] = Vj->H[2 * n];
        Vd->H[2 * n] = Vk->H[2 * n];
        break;
    case 32:
        Vd->W[2 * n + 1] = Vj->W[2 * n];
        Vd->W[2 * n] = Vk->W[2 * n];
        break;
    case 64:
        Vd->D[2 * n + 1] = Vj->D[2 * n];
        Vd->D[2 * n] = Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vpackod(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[2 * n + 1] = Vj->B[2 * n + 1];
        Vd->B[2 * n] = Vk->B[2 * n + 1];
        break;
    case 16:
        Vd->H[2 * n + 1] = Vj->H[2 * n + 1];
        Vd->H[2 * n] = Vk->H[2 * n + 1];
        break;
    case 32:
        Vd->W[2 * n + 1] = Vj->W[2 * n + 1];
        Vd->W[2 * n] = Vk->W[2 * n + 1];
        break;
    case 64:
        Vd->D[2 * n + 1] = Vj->D[2 * n + 1];
        Vd->D[2 * n] = Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vpackev_b, 8, helper_vvv_c, do_vpackev)
DO_HELPER_VVV(vpackev_h, 16, helper_vvv_c, do_vpackev)
DO_HELPER_VVV(vpackev_w, 32, helper_vvv_c, do_vpackev)
DO_HELPER_VVV(vpackev_d, 64, helper_vvv_c, do_vpackev)
DO_HELPER_VVV(vpackod_b, 8, helper_vvv_c, do_vpackod)
DO_HELPER_VVV(vpackod_h, 16, helper_vvv_c, do_vpackod)
DO_HELPER_VVV(vpackod_w, 32, helper_vvv_c, do_vpackod)
DO_HELPER_VVV(vpackod_d, 64, helper_vvv_c, do_vpackod)

static void do_vpickev(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n + 8] = Vj->B[2 * n];
        Vd->B[n] = Vk->B[2 * n];
        break;
    case 16:
        Vd->H[n + 4] = Vj->H[2 * n];
        Vd->H[n] = Vk->H[2 * n];
        break;
    case 32:
        Vd->W[n + 2] = Vj->W[2 * n];
        Vd->W[n] = Vk->W[2 * n];
        break;
    case 64:
        Vd->D[n + 1] = Vj->D[2 *n];
        Vd->D[n] = Vk->D[2 * n];
        break;
    default:
        g_assert_not_reached();
    }
}

static void do_vpickod(vec_t *Vd, vec_t *Vj, vec_t *Vk, int bit, int n)
{
    switch (bit) {
    case 8:
        Vd->B[n + 8] = Vj->B[2 * n + 1];
        Vd->B[n] = Vk->B[2 * n + 1];
        break;
    case 16:
        Vd->H[n + 4] = Vj->H[2 * n + 1];
        Vd->H[n] = Vk->H[2 * n + 1];
        break;
    case 32:
        Vd->W[n + 2] = Vj->W[2 * n + 1];
        Vd->W[n] = Vk->W[2 * n + 1];
        break;
    case 64:
        Vd->D[n + 1] = Vj->D[2 *n + 1];
        Vd->D[n] = Vk->D[2 * n + 1];
        break;
    default:
        g_assert_not_reached();
    }
}

DO_HELPER_VVV(vpickev_b, 8, helper_vvv_c, do_vpickev)
DO_HELPER_VVV(vpickev_h, 16, helper_vvv_c, do_vpickev)
DO_HELPER_VVV(vpickev_w, 32, helper_vvv_c, do_vpickev)
DO_HELPER_VVV(vpickev_d, 64, helper_vvv_c, do_vpickev)
DO_HELPER_VVV(vpickod_b, 8, helper_vvv_c, do_vpickod)
DO_HELPER_VVV(vpickod_h, 16, helper_vvv_c, do_vpickod)
DO_HELPER_VVV(vpickod_w, 32, helper_vvv_c, do_vpickod)
DO_HELPER_VVV(vpickod_d, 64, helper_vvv_c, do_vpickod)
