/*
 * MIPS emulation for QEMU - microMIPS translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

static inline int plus_1(DisasContext *ctx, int x)
{
    return x + 1;
}

/* Include the auto-generated decoders.  */
#include "decode-micromips16.c.inc"
#include "decode-micromips32.c.inc"

static bool trans_LSA(DisasContext *ctx, arg_r *a)
{
    return gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);
}
