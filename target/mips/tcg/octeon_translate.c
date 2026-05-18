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
    TCGv_i64 p;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x%" VADDR_PRIx "\n",
                  ctx->base.pc_next);
        generate_exception_end(ctx, EXCP_RI);
        return true;
    }

    /* Load needed operands */
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);

    p = tcg_constant_i64(1ULL << a->p);
    if (a->set) {
        tcg_gen_and_i64(bcond, p, t0);
    } else {
        tcg_gen_andc_i64(bcond, p, t0);
    }

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->btarget = ctx->base.pc_next + 4 + a->offset * 4;
    ctx->hflags |= MIPS_HFLAG_BDS32;
    return true;
}

static bool trans_BADDU(DisasContext *ctx, arg_BADDU *a)
{
    TCGv_i64 t0, t1;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_andi_i64(cpu_gpr[a->rd], t0, 0xff);
    return true;
}

static bool trans_DMUL(DisasContext *ctx, arg_DMUL *a)
{
    TCGv_i64 t0, t1;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_mul_i64(cpu_gpr[a->rd], t0, t1);
    return true;
}

static bool trans_EXTS(DisasContext *ctx, arg_EXTS *a)
{
    TCGv_i64 t0;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    tcg_gen_sextract_i64(t0, t0, a->p, a->lenm1 + 1);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_CINS(DisasContext *ctx, arg_CINS *a)
{
    TCGv_i64 t0;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    tcg_gen_deposit_z_i64(t0, t0, a->p, a->lenm1 + 1);
    gen_store_gpr(t0, a->rt);
    return true;
}

static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    TCGv_i64 t0;

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    if (!a->dw) {
        tcg_gen_andi_i64(t0, t0, 0xffffffff);
    }
    tcg_gen_ctpop_i64(t0, t0);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_SEQNE(DisasContext *ctx, arg_SEQNE *a)
{
    TCGv_i64 t0, t1;

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    if (a->ne) {
        tcg_gen_setcond_i64(TCG_COND_NE, cpu_gpr[a->rd], t1, t0);
    } else {
        tcg_gen_setcond_i64(TCG_COND_EQ, cpu_gpr[a->rd], t1, t0);
    }
    return true;
}

static bool trans_SEQNEI(DisasContext *ctx, arg_SEQNEI *a)
{
    TCGv_i64 t0;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();

    gen_load_gpr(t0, a->rs);

    /* Sign-extend to 64 bit value */
    target_ulong imm = a->imm;
    if (a->ne) {
        tcg_gen_setcondi_i64(TCG_COND_NE, cpu_gpr[a->rt], t0, imm);
    } else {
        tcg_gen_setcondi_i64(TCG_COND_EQ, cpu_gpr[a->rt], t0, imm);
    }
    return true;
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

static void octeon_zero_partial_product_state(void)
{
    for (int i = 0; i < OCTEON_MULTIPLIER_REGS; i++) {
        tcg_gen_movi_i64(oct_p[i], 0);
    }
}

static bool trans_mtm(DisasContext *ctx, arg_r2 *a, unsigned int index)
{
    /*
     * Octeon3 two-source MTM forms load lane index from rs and lane
     * index + 3 from rt.  Legacy one-source forms encode rt as $zero.
     */
    gen_load_gpr(oct_mpl[index], a->rs);
    gen_load_gpr(oct_mpl[index + 3], a->rt);

    /*
     * Octeon3 clears P1 with P0 so that VMULU sequences remain
     * backward compatible with Octeon2.
     */
    if (index == 0) {
        tcg_gen_movi_i64(oct_mpl[1], 0);
    }

    octeon_zero_partial_product_state();
    return true;
}

TRANS(MTM0, trans_mtm, 0);
TRANS(MTM1, trans_mtm, 1);
TRANS(MTM2, trans_mtm, 2);

static bool trans_mtp(DisasContext *ctx, arg_r2 *a, unsigned int index)
{
    /*
     * Octeon3 two-source MTP forms load lane index from rs and lane
     * index + 3 from rt.  Legacy one-source forms encode rt as $zero.
     */
    gen_load_gpr(oct_p[index], a->rs);
    gen_load_gpr(oct_p[index + 3], a->rt);

    /*
     * Octeon3 clears P1 with P0 so that VMULU sequences remain
     * backward compatible with Octeon2.
     */
    if (index == 0) {
        tcg_gen_movi_i64(oct_p[1], 0);
    }
    return true;
}

TRANS(MTP0, trans_mtp, 0);
TRANS(MTP1, trans_mtp, 1);
TRANS(MTP2, trans_mtp, 2);

static bool trans_VMULU(DisasContext *ctx, arg_VMULU *a)
{
    TCGv_i64 x[3], y[3], z[3];
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);

    z[0] = y[0] = tcg_temp_new_i64();
    z[1] = y[1] = tcg_temp_new_i64();
    z[2] = y[2] = tcg_temp_new_i64();
    x[0] = tcg_temp_new_i64();
    x[1] = tcg_temp_new_i64();
    x[2] = zero;

    /* Z = rs * (mpl1 : mpl0) + rt */
    gen_load_gpr(tmp, a->rs);
    gen_load_gpr(y[0], a->rt);
    tcg_gen_mulu2_i64(x[0], x[1], tmp, oct_mpl[0]);
    tcg_gen_mulu2_i64(y[1], y[2], tmp, oct_mpl[1]);
    tcg_gen_addN_i64(3, z, y, x);

    /* X == (0 : p1 : p0) */
    x[0] = oct_p[0];
    x[1] = oct_p[1];

    /* Y == (p1 : p0 : tmp) */
    y[0] = tmp;
    y[1] = oct_p[0];
    y[2] = oct_p[1];

    /* (p1 : p0 : rd) = Z + (0 : p1 : p0) */
    tcg_gen_addN_i64(3, y, z, x);
    gen_store_gpr(tmp, a->rd);
    return true;
}

static bool trans_VMM0(DisasContext *ctx, arg_VMM0 *a)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    gen_load_gpr(tmp, a->rs);
    tcg_gen_mul_i64(oct_mpl[0], oct_mpl[0], tmp);
    gen_load_gpr(tmp, a->rt);
    tcg_gen_add_i64(oct_mpl[0], oct_mpl[0], tmp);
    tcg_gen_add_i64(oct_mpl[0], oct_mpl[0], oct_p[0]);
    gen_store_gpr(oct_mpl[0], a->rd);

    tcg_gen_movi_i64(oct_mpl[1], 0);
    for (int i = 0; i < OCTEON_MULTIPLIER_REGS; i++) {
        tcg_gen_movi_i64(oct_p[i], 0);
    }
    return true;
}

static bool trans_V3MULU(DisasContext *ctx, arg_V3MULU *a)
{
    TCGv_i64 x[7], y[7], z[7];
    TCGv_i64 tmp = tcg_temp_new_i64();

    for (int i = 0; i < 7; ++i) {
        z[i] = tcg_temp_new_i64();
        y[i] = tcg_temp_new_i64();
    }
    memcpy(&x[0], z, 6 * sizeof(TCGv_i64));
    x[6] = tcg_constant_i64(0);

    /*
     * Z = rs * mpl -- 64x384->448 bit multiply
     * Compute even partial products into X and odd partial products into Y.
     * Include RT into the odd partial products, which are 0 in bits [63:0].
     */
    gen_load_gpr(tmp, a->rs);
    gen_load_gpr(y[0], a->rt);
    for (int i = 0; i < 6; i += 2) {
        tcg_gen_mulu2_i64(x[i + 0], x[i + 1], tmp, oct_mpl[i]);
        tcg_gen_mulu2_i64(y[i + 1], y[i + 2], tmp, oct_mpl[i + 1]);
    }

    /* Sum even and odd to produce final product, plus rt. */
    tcg_gen_addN_i64(7, z, x, y);

    /* X == (0 : p5 : p4 : p3 : p2 : p1 : p0) -- x[6] is still 0 */
    memcpy(&x[0], oct_p, 6 * sizeof(TCGv_i64));

    /* Y == (p5 : p4 : p3 : p2 : p1 : p0 : tmp) */
    memcpy(&y[1], oct_p, 6 * sizeof(TCGv_i64));
    y[0] = tmp;

    /* (p* : rd) = (0 : p*) + (rs * mpl + rt) */
    tcg_gen_addN_i64(7, y, x, z);
    gen_store_gpr(tmp, a->rd);
    return true;
}
