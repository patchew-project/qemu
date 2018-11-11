/*
 *  MIPS emulation for QEMU - MIPS32 translation routines
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

/* Include the auto-generated decoder.  */
#include "decode.inc.c"

static bool trans_synci(DisasContext *dc, arg_synci *a)
{
    /* Break the TB to be able to sync copied instructions immediately */
    dc->base.is_jmp = DISAS_STOP;
    return true;
}

static bool trans_dclz(DisasContext *ctx, arg_rs_rt_rd *a)
{
    gen_cl(ctx, OPC_DCLZ, a->rd, a->rs);
    return true;
}

static bool trans_dclo(DisasContext *ctx, arg_rs_rt_rd *a)
{
    gen_cl(ctx, OPC_DCLO, a->rd, a->rs);
    return true;
}
