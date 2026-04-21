/*
 * Octeon-specific instructions translation routines
 *
 *  Copyright (c) 2022 Pavel Dovgalyuk
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"
#include "tcg/tcg-op-gvec.h"

/* Include the auto-generated decoder.  */
#include "decode-octeon.c.inc"

static bool trans_BBIT(DisasContext *ctx, arg_BBIT *a)
{
    TCGv p;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x%" VADDR_PRIx "\n",
                  ctx->base.pc_next);
        generate_exception_end(ctx, EXCP_RI);
        return true;
    }

    /* Load needed operands */
    TCGv t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);

    p = tcg_constant_tl(1ULL << a->p);
    if (a->set) {
        tcg_gen_and_tl(bcond, p, t0);
    } else {
        tcg_gen_andc_tl(bcond, p, t0);
    }

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->btarget = ctx->base.pc_next + 4 + a->offset * 4;
    ctx->hflags |= MIPS_HFLAG_BDS32;
    return true;
}

static bool trans_BADDU(DisasContext *ctx, arg_BADDU *a)
{
    TCGv t0, t1;

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_andi_i64(t0, t0, 0xff);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_DMUL(DisasContext *ctx, arg_DMUL *a)
{
    TCGv t0, t1;

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_mul_i64(t0, t0, t1);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_EXTS(DisasContext *ctx, arg_EXTS *a)
{
    TCGv t0;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);
    tcg_gen_sextract_tl(t0, t0, a->p, a->lenm1 + 1);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_CINS(DisasContext *ctx, arg_CINS *a)
{
    TCGv t0;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);
    tcg_gen_deposit_z_tl(t0, t0, a->p, a->lenm1 + 1);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    TCGv t0;

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);
    if (!a->dw) {
        tcg_gen_andi_i64(t0, t0, 0xffffffff);
    }
    tcg_gen_ctpop_tl(t0, t0);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_seqne(DisasContext *ctx, const arg_cmp3 *a)
{
    TCGv t0, t1;

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    if (a->ne) {
        tcg_gen_setcond_tl(TCG_COND_NE, cpu_gpr[a->rd], t1, t0);
    } else {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_gpr[a->rd], t1, t0);
    }
    return true;
}

static bool trans_SEQ(DisasContext *ctx, arg_cmp3 *a)
{
    return trans_seqne(ctx, a);
}

static bool trans_SNE(DisasContext *ctx, arg_cmp3 *a)
{
    return trans_seqne(ctx, a);
}

static bool trans_seqnei(DisasContext *ctx, const arg_cmpi *a)
{
    TCGv t0;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rs);

    /* Sign-extend to 64 bit value */
    int64_t imm = a->imm;
    if (a->ne) {
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_gpr[a->rt], t0, imm);
    } else {
        tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_gpr[a->rt], t0, imm);
    }
    return true;
}

static bool trans_SEQI(DisasContext *ctx, arg_cmpi *a)
{
    return trans_seqnei(ctx, a);
}

static bool trans_SNEI(DisasContext *ctx, arg_cmpi *a)
{
    return trans_seqnei(ctx, a);
}

static bool trans_lx(DisasContext *ctx, arg_lx *a, MemOp mop)
{
    gen_lx(ctx, a->rd, a->base, a->index, mop);

    return true;
}

TRANS(LBUX, trans_lx, MO_UB);
TRANS(LHX,  trans_lx, MO_SW);
TRANS(LWX,  trans_lx, MO_SL);
TRANS(LDX,  trans_lx, MO_UQ);
