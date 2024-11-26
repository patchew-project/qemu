/*
 * MIPS emulation for QEMU - nanoMIPS translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

static inline int s_eu(DisasContext *ctx, int x)
{
    return x == 0x7f ? -1 : x;
}

/* Include the auto-generated decoders.  */
#include "decode-nanomips16.c.inc"
#include "decode-nanomips32.c.inc"
#include "decode-nanomips48.c.inc"

static inline void check_nms(DisasContext *ctx, bool not_in_nms)
{
    if (not_in_nms && unlikely(ctx->CP0_Config5 & (1 << CP0C5_NMS))) {
        gen_reserved_instruction(ctx);
    }
}

static bool trans_LSA(DisasContext *ctx, arg_r *a)
{
    gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);

    return true;
}

static bool trans_LI(DisasContext *ctx, arg_rd_imm *a)
{
    check_nms(ctx, a->not_in_nms);

    gen_li(ctx, a->rd, a->imm);

    return true;
}
