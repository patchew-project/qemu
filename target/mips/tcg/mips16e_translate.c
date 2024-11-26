/*
 * MIPS emulation for QEMU - MIPS16e translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

static inline int xlat(DisasContext *ctx, int x)
{
  static const int map[8] = { 16, 17, 2, 3, 4, 5, 6, 7 };

  return map[x];
}

/* Include the auto-generated decoders.  */
#include "decode-mips16e_16.c.inc"
#include "decode-mips16e_32.c.inc"

static bool trans_LI(DisasContext *ctx, arg_rd_imm *a)
{
    gen_li(ctx, a->rd, a->imm);

    return true;
}
