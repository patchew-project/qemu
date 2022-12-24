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
