/*
 * VR5432 extensions translation routines
 *
 * Reference: VR5432 Microprocessor User’s Manual
 *            (Document Number U13751EU5V0UM00)
 *
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"
#include "internal.h"

/* Include the auto-generated decoder. */
#include "decode-vr54xx.c.inc"

/*
 * Integer Multiply-Accumulate Instructions
 *
 * MACC         Multiply, accumulate, and move LO
 * MACCHI       Multiply, accumulate, and move HI
 * MACCHIU      Unsigned multiply, accumulate, and move HI
 * MACCU        Unsigned multiply, accumulate, and move LO
 * MULHI        Multiply and move HI
 * MULHIU       Unsigned multiply and move HI
 * MULS         Multiply, negate, and move LO
 * MULSHI       Multiply, negate, and move HI
 * MULSHIU      Unsigned multiply, negate, and move HI
 * MULSU        Unsigned multiply, negate, and move LO
 */

typedef void gen_helper_mult_acc_t(TCGv, TCGv_ptr, TCGv, TCGv);

static bool trans_mult_acc(DisasContext *ctx, arg_r *a,
                           gen_helper_mult_acc_t *gen_helper_mult_acc)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    gen_helper_mult_acc(t0, cpu_env, t0, t1);

    gen_store_gpr(t0, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);

    return false;
}

#define MULT_ACC(opcode, gen_helper) \
static bool trans_##opcode(DisasContext *ctx, arg_r *a) \
{ \
    return trans_mult_acc(ctx, a, gen_helper); \
}
MULT_ACC(MACC,      gen_helper_macc);
MULT_ACC(MACCHI,    gen_helper_macchi);
MULT_ACC(MACCHIU,   gen_helper_macchiu);
MULT_ACC(MACCU,     gen_helper_maccu);
MULT_ACC(MULHI,     gen_helper_mulhi);
MULT_ACC(MULHIU,    gen_helper_mulhiu);
MULT_ACC(MULS,      gen_helper_muls);
MULT_ACC(MULSHI,    gen_helper_mulshi);
MULT_ACC(MULSHIU,   gen_helper_mulshiu);
MULT_ACC(MULSU,     gen_helper_mulsu);
