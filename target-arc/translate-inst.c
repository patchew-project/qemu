/*
 *  QEMU ARC CPU
 *
 *  Copyright (c) 2016 Michael Rolnik
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR dest PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see
 *  <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

/*
    see http://me.bios.io/images/d/dd/ARCompactISA_ProgrammersReference.pdf
*/

#include "translate.h"
#include "qemu/bitops.h"
#include "tcg/tcg.h"
#include "translate-inst.h"

static void gen_add_Cf(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_and_tl(t1, src1, src2); /*  t1 = src1 & src2                    */
    tcg_gen_andc_tl(t2, src1, dest);/*  t2 = src1 & ~dest                   */
    tcg_gen_andc_tl(t3, src2, dest);/*  t3 = src2 & ~dest                   */
    tcg_gen_or_tl(t1, t1, t2);      /*  t1 = t1 | t2 | t3                   */
    tcg_gen_or_tl(t1, t1, t3);

    tcg_gen_shri_tl(cpu_Cf, t1, 31);/*  Cf = t1(31)                         */

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void gen_add_Vf(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    /*

    src1 & src2 & ~dest | ~src1 & ~src2 & dest = (src1 ^ dest) & ~(src1 ^ src2)

    */
    tcg_gen_xor_tl(t1, src1, dest); /*  t1 = src1 ^ dest                    */
    tcg_gen_xor_tl(t2, src1, src2); /*  t2 = src1 ^ src2                    */
    tcg_gen_andc_tl(t1, t1, t2);    /*  t1 = (src1 ^ src2) & ~(src1 ^ src2) */

    tcg_gen_shri_tl(cpu_Vf, t1, 31);/*  Vf = t1(31)                         */

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void gen_sub_Cf(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_not_tl(t1, src1);       /*  t1 = ~src1                          */
    tcg_gen_and_tl(t2, t1, src2);   /*  t2 = ~src1 & src2                   */
    tcg_gen_or_tl(t3, t1, src2);    /*  t3 = (~src1 | src2) & dest          */
    tcg_gen_and_tl(t3, t3, dest);
    tcg_gen_or_tl(t2, t2, t3);      /*  t2 = ~src1 & src2
                                           | ~src1 & dest
                                           | dest & src2                    */
    tcg_gen_shri_tl(cpu_Cf, t2, 31);/*  Cf = t2(31)                         */

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void gen_sub_Vf(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    /*
        t1 = src1 & ~src2 & ~dest
           | ~src1 & src2 & dest
           = (src1 ^ dest) & (src1 ^ dest)*/
    tcg_gen_xor_tl(t1, src1, dest);
    tcg_gen_xor_tl(t2, src1, src2);
    tcg_gen_and_tl(t1, t1, t2);
    tcg_gen_shri_tl(cpu_Vf, t1, 31);/*  Vf = t1(31) */

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void arc_gen_exec_delayslot(DisasCtxt *ctx)
{
    if (ctx->opt.limm == 0) {
        uint32_t cpc = ctx->cpc;
        uint32_t npc = ctx->npc;
        uint32_t dpc = ctx->dpc;
        uint32_t pcl = ctx->pcl;
        options_t opt = ctx->opt;
        int bstate = ctx->bstate;

        ctx->cpc = ctx->npc;
        ctx->pcl = ctx->cpc & 0xfffffffc;

        ++ctx->ds;

        /* TODO: check for illegal instruction sequence */

        memset(&ctx->opt, 0, sizeof(ctx->opt));
        arc_decode(ctx);

        --ctx->ds;

        ctx->cpc = cpc;
        ctx->npc = npc;
        ctx->dpc = dpc;
        ctx->pcl = pcl;
        ctx->opt = opt;
        ctx->bstate = bstate;
    }
}

static void arc_gen_kill_delayslot(DisasCtxt *ctx)
{
    /*  nothing to do   */
}

#define ARC_COND_IF_1(flag, label) \
                    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_ ## flag ## f, 0, label)
#define ARC_COND_IF_0(flag, label) \
                    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_ ## flag ## f, 0, label)

void arc_gen_jump_ifnot(DisasCtxt *ctx, ARC_COND cond, TCGLabel *label_skip)
{
    TCGLabel *label_cont = gen_new_label();

    switch (cond) {
        /*
            Always
        */
        case    ARC_COND_AL: {
        } break;

        /*
            Zero
        */
        case    ARC_COND_Z: {
            ARC_COND_IF_0(Z, label_skip);
        } break;

        /*
            Non-Zero
        */
        case    ARC_COND_NZ: {
            ARC_COND_IF_1(Z, label_skip);
        } break;

        /*
            Positive
        */
        case    ARC_COND_P: {
            tcg_gen_brcondi_tl(TCG_COND_LT, cpu_Nf, 0, label_skip);
        } break;

        /*
            Negative
        */
        case    ARC_COND_N: {
            tcg_gen_brcondi_tl(TCG_COND_GE, cpu_Nf, 0, label_skip);
        } break;

        /*
            Carry set, lower than (unsigned)
        */
        case    ARC_COND_C: {
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_Cf, 0, label_skip);
        } break;

        /*
            Carry clear, higher or same (unsigned)
        */
        case    ARC_COND_CC: {
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_Cf, 0, label_skip);
        } break;

        /*
            Over-flow set
        */
        case    ARC_COND_VS: {
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_Cf, 0, label_skip);
        } break;

        /*
            Over-flow clear
        */
        case    ARC_COND_VC: {
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_Cf, 0, label_skip);
        } break;

        /*
            Greater than (signed)
        */
        case    ARC_COND_GT: {
            tcg_gen_brcondi_tl(TCG_COND_LE, cpu_Nf, 0, label_skip);
        } break;

        /*
            Greater than or equal to (signed)
        */
        case    ARC_COND_GE: {
            tcg_gen_brcondi_tl(TCG_COND_LT, cpu_Nf, 0, label_skip);
        } break;

        /*
            Less than (signed)
        */
        case    ARC_COND_LT: {
            tcg_gen_brcondi_tl(TCG_COND_GE, cpu_Nf, 0, label_skip);
        } break;

        /*
            Less than or equal to (signed)
        */
        case    ARC_COND_LE: {
            tcg_gen_brcondi_tl(TCG_COND_GT, cpu_Nf, 0, label_skip);
        } break;

        /*
            Higher than (unsigned)
            !C and !Z
        */
        case    ARC_COND_HI: {
            ARC_COND_IF_1(C, label_skip);
            ARC_COND_IF_1(Z, label_skip);
        } break;

        /*
            Lower than
            C or Z
        */
        case    ARC_COND_LS: {
            ARC_COND_IF_1(C, label_cont);
            ARC_COND_IF_0(Z, label_skip);
        } break;

        /*
            Positive non-zero
            !N and !Z
        */
        case    ARC_COND_PNZ: {
            ARC_COND_IF_1(N, label_skip);
            ARC_COND_IF_1(Z, label_skip);
        } break;
    }

    gen_set_label(label_cont);
}

/*
    ADC
*/
int arc_gen_ADC(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_add_tl(rslt, src1, src2);
    tcg_gen_add_tl(rslt, rslt, cpu_Cf);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        gen_add_Cf(rslt, src1, src2);
        gen_add_Vf(rslt, src1, src2);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    ADD
*/
int arc_gen_ADD(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_add_tl(rslt, src1, src2);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        gen_add_Cf(rslt, src1, src2);
        gen_add_Vf(rslt, src1, src2);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    ADD1
*/
int arc_gen_ADD1(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, src2, 1);
    arc_gen_ADD(ctx, dest, src1, t0);

    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    ADD2
*/
int arc_gen_ADD2(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, src2, 2);
    arc_gen_ADD(ctx, dest, src1, t0);

    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    ADD3
*/
int arc_gen_ADD3(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, src2, 3);
    arc_gen_ADD(ctx, dest, src1, t0);

    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    SUB
*/
int arc_gen_SUB(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_sub_tl(rslt, src1, src2);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        gen_sub_Cf(rslt, src1, src2);
        gen_sub_Vf(rslt, src1, src2);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    SBC
*/
int arc_gen_SBC(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_sub_tl(rslt, src1, src2);
    tcg_gen_sub_tl(rslt, rslt, cpu_Cf);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        gen_sub_Cf(rslt, src1, src2);
        gen_sub_Vf(rslt, src1, src2);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}
/*
    SUB1
*/
int arc_gen_SUB1(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, src2, 1);
    arc_gen_SUB(ctx, dest, src1, t0);

    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    SUB2
*/
int arc_gen_SUB2(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, src2, 2);
    arc_gen_SUB(ctx, dest, src1, t0);

    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    SUB3
*/
int arc_gen_SUB3(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, src2, 3);
    arc_gen_SUB(ctx, dest, src1, t0);

    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    RSUB
*/
int arc_gen_RSUB(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    return arc_gen_SUB(ctx, dest, src2, src1);
}

/*
    CMP
*/
int arc_gen_CMP(DisasCtxt *ctx, TCGv src1, TCGv src2)
{
    TCGv rslt = tcg_temp_new_i32();

    tcg_gen_sub_tl(rslt, src1, src2);

    tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
    tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    gen_sub_Cf(rslt, src1, src2);
    gen_sub_Vf(rslt, src1, src2);

    tcg_temp_free_i32(rslt);

    return  BS_NONE;
}

/*
    TST
*/
int arc_gen_TST(DisasCtxt *ctx, TCGv src1, TCGv src2)
{
    TCGv temp = tcg_temp_new_i32();

    ctx->opt.f = 1;
    arc_gen_AND(ctx, temp, src1, src2);
    tcg_temp_free_i32(temp);

    return BS_NONE;
}

/*
    AND
*/
int arc_gen_AND(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_and_tl(rslt, src1, src2);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    OR
*/
int arc_gen_OR(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_or_tl(rslt, src1, src2);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    BIC
*/
int arc_gen_BIC(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andc_tl(rslt, src1, src2);  /*  rslt = src1 & ~src2             */

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    XOR
*/
int arc_gen_XOR(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_xor_tl(rslt, src1, src2);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    ASL
*/
int arc_gen_ASL(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    arc_gen_ADD(ctx, dest, src1, src1);

    return  BS_NONE;
}

/*
    ASLm
*/
int arc_gen_ASLm(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;
    TCGv t0 = tcg_temp_new_i32();

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(t0, src2, 31);
    tcg_gen_shl_tl(rslt, src1, t0);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        tcg_gen_rotl_tl(cpu_Cf, src1, t0);
        tcg_gen_andi_tl(cpu_Cf, cpu_Cf, 1);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }
    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    ASR
*/
int arc_gen_ASR(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_sari_tl(rslt, src1, 1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        tcg_gen_andi_tl(cpu_Cf, src1, 1);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    ASRm
*/
int arc_gen_ASRm(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;
    TCGv t0 = tcg_temp_new_i32();

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(t0, src2, 31);
    tcg_gen_sar_tl(rslt, src1, t0);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        tcg_gen_rotr_tl(cpu_Cf, src1, src2);
        tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }
    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    LSR
*/
int arc_gen_LSR(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_shri_tl(rslt, src1, 1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_movi_tl(cpu_Nf, 0);
        tcg_gen_andi_tl(cpu_Cf, src1, 1);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    LSRm
*/
int arc_gen_LSRm(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;
    TCGv t0 = tcg_temp_new_i32();

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(t0, src2, 31);
    tcg_gen_shr_tl(rslt, src1, t0);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        tcg_gen_rotr_tl(cpu_Cf, src1, t0);
        tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }
    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    ROR
*/
int arc_gen_ROR(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_rotri_tl(rslt, src1, 1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        tcg_gen_mov_tl(cpu_Cf, cpu_Nf);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    RORm
*/
int arc_gen_RORm(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(rslt, src2, 0x1f);
    tcg_gen_rotr_tl(rslt, src1, rslt);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        tcg_gen_mov_tl(cpu_Cf, cpu_Nf);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    EX
*/
int arc_gen_EX(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv temp = tcg_temp_new_i32();

    tcg_gen_mov_tl(temp, dest);

    tcg_gen_qemu_ld_tl(dest, src1, ctx->memidx, MO_UL);
    tcg_gen_qemu_st_tl(temp, src1, ctx->memidx, MO_UL);

    tcg_temp_free_i32(temp);

    return BS_NONE;
}

/*
    LD
*/
int arc_gen_LD(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv addr = tcg_temp_new_i32();

    /*  address             */
    switch (ctx->opt.aa) {
        case 0x00: {
            tcg_gen_add_tl(addr, src1, src2);
        } break;

        case 0x01: {
            tcg_gen_add_tl(addr, src1, src2);
        } break;

        case 0x02: {
            tcg_gen_mov_tl(addr, src1);
        } break;

        case 0x03: {
            if (ctx->opt.zz == 0x02) {
                tcg_gen_shli_tl(addr, src2, 1);
            } else if (ctx->opt.zz == 0x00) {
                tcg_gen_shli_tl(addr, src2, 2);
            } else {
                assert(!"bad format");
            }

            tcg_gen_add_tl(addr, src1, addr);
        } break;
    }

    /*  memory read         */
    switch (ctx->opt.zz) {
        case 0x00: {
            tcg_gen_qemu_ld_tl(dest, addr, ctx->memidx, MO_UL);
        } break;

        case 0x01: {
            if (ctx->opt.x) {
                tcg_gen_qemu_ld_tl(dest, addr, ctx->memidx, MO_SB);
            } else {
                tcg_gen_qemu_ld_tl(dest, addr, ctx->memidx, MO_UB);
            }
        } break;

        case 0x02: {
            if (ctx->opt.x) {
                tcg_gen_qemu_ld_tl(dest, addr, ctx->memidx, MO_SW);
            } else {
                tcg_gen_qemu_ld_tl(dest, addr, ctx->memidx, MO_UW);
            }
        } break;

        case 0x03: {
            assert(!"reserved");
        } break;
    }

    /*  address write back      */
    if (ctx->opt.aa == 0x01 || ctx->opt.aa == 0x02) {
        tcg_gen_add_tl(src1, src1, src2);
    }

    tcg_temp_free_i32(addr);

    return BS_NONE;
}

/*
    LDB
*/
int arc_gen_LDB(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{

    ctx->opt.zz = 1;    /*  byte                        */
    ctx->opt.x = 0;     /*  no sign extension           */
    ctx->opt.aa = 0;    /*  no address write back       */
    ctx->opt.di = 0;    /*  cached data memory access   */

    return arc_gen_LD(ctx, dest, src1, src2);
}

/*
    LDW
*/
int arc_gen_LDW(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{

    ctx->opt.zz = 2;    /*  word                        */
    ctx->opt.x = 0;     /*  no sign extension           */
    ctx->opt.aa = 0;    /*  no address write back       */
    ctx->opt.di = 0;    /*  cached data memory access   */

    return arc_gen_LD(ctx, dest, src1, src2);
}

/*
    ST
*/
int arc_gen_ST(DisasCtxt *ctx, TCGv src1, TCGv src2, TCGv src3)
{
    TCGv addr = tcg_temp_new_i32();

    /*  address         */
    switch (ctx->opt.aa) {
        case 0x00: {
            tcg_gen_add_tl(addr, src2, src3);
        } break;

        case 0x01: {
            tcg_gen_add_tl(addr, src2, src3);
        } break;

        case 0x02: {
            tcg_gen_mov_tl(addr, src2);
        } break;

        case 0x03: {
            if (ctx->opt.zz == 0x02) {
                tcg_gen_shli_tl(addr, src3, 1);
            } else if (ctx->opt.zz == 0x00) {
                tcg_gen_shli_tl(addr, src3, 2);
            } else {
                assert(!"bad format");
            }

            tcg_gen_add_tl(addr, src2, addr);
        } break;
    }

    /*  write               */
    switch (ctx->opt.zz) {
        case 0x00: {
            tcg_gen_qemu_st_tl(src1, addr, ctx->memidx, MO_UL);
        } break;

        case 0x01: {
            tcg_gen_qemu_st_tl(src1, addr, ctx->memidx, MO_UB);
        } break;

        case 0x02: {
            tcg_gen_qemu_st_tl(src1, addr, ctx->memidx, MO_UW);
        } break;

        case 0x03: {
            assert(!"reserved");
        } break;
    }

    /*  address write back  */
    if (ctx->opt.aa == 0x01 || ctx->opt.aa == 0x02) {
        tcg_gen_add_tl(src2, src2, src3);
    }

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    STB
*/
int arc_gen_STB(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{

    ctx->opt.zz = 1;    /*  byte                        */
    ctx->opt.x = 0;     /*  no sign extension           */
    ctx->opt.aa = 0;    /*  no address write back       */
    ctx->opt.di = 0;    /*  cached data memory access   */

    return arc_gen_ST(ctx, dest, src1, src2);
}

/*
    STW
*/
int arc_gen_STW(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{

    ctx->opt.zz = 2;    /*  word                        */
    ctx->opt.x = 0;     /*  no sign extension           */
    ctx->opt.aa = 0;    /*  no address write back       */
    ctx->opt.di = 0;    /*  cached data memory access   */

    return arc_gen_ST(ctx, dest, src1, src2);
}

/*
    PREFETCH
*/
int arc_gen_PREFETCH(DisasCtxt *ctx, TCGv src1, TCGv src2)
{
    TCGv temp = tcg_temp_new_i32();

    arc_gen_LD(ctx, temp, src1, src2);

    tcg_temp_free_i32(temp);

    return BS_NONE;
}

/*
    SYNC
*/
int arc_gen_SYNC(DisasCtxt *ctx)
{
    /*  nothing to do*/

    return BS_NONE;
}

/*
    MAX
*/
int arc_gen_MAX(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    if (ctx->opt.f) {
        arc_gen_CMP(ctx, src1, src2);
    }

    tcg_gen_movcond_tl(TCG_COND_GEU, dest, src1, src2, src1, src2);

    return  BS_NONE;
}

/*
    MIN
*/
int arc_gen_MIN(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    if (ctx->opt.f) {
        arc_gen_CMP(ctx, src1, src2);
    }

    tcg_gen_movcond_tl(TCG_COND_GEU, dest, src1, src2, src2, src1);

    return  BS_NONE;
}

/*
    MOV
*/
int arc_gen_MOV(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_mov_tl(rslt, src1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    EXTB
*/
int arc_gen_EXTB(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_ext8u_tl(rslt, src1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_movi_tl(cpu_Nf, 0);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return BS_NONE;
}

/*
    EXTW
*/
int arc_gen_EXTW(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_ext16u_tl(rslt, src1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_movi_tl(cpu_Nf, 0);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return BS_NONE;
}

/*
    SEXB
*/
int arc_gen_SEXB(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_ext8s_tl(rslt, src1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    SEXW
*/
int arc_gen_SEXW(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_ext16s_tl(rslt, src1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    SWAP
*/
int arc_gen_SWAP(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_rotli_tl(rslt, src1, 16);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    ABS
*/
int arc_gen_ABS(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;
    TCGv_i32 t0 = tcg_temp_new_i32();

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_neg_i32(t0, src1);
    tcg_gen_movcond_tl(TCG_COND_GEU, rslt, src1, ctx->msb32, src1, t0);

    tcg_temp_free_i32(t0);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Cf, src1, 31);
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Nf, src1, ctx->msb32);
        tcg_gen_mov_tl(cpu_Vf, cpu_Nf);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    NEG
*/
int arc_gen_NEG(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    arc_gen_SUB(ctx, dest, ctx->zero, src1);

    return  BS_NONE;
}

/*
    NOT
*/
int arc_gen_NOT(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_not_tl(rslt, src1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    POP
*/
int arc_gen_POP(DisasCtxt *ctx, TCGv src1)
{
    tcg_gen_qemu_ld_tl(src1, cpu_sp, ctx->memidx, MO_UL);
    tcg_gen_addi_tl(cpu_sp, cpu_sp, 4);

    return BS_NONE;
}

/*
    PUSH
*/
int arc_gen_PUSH(DisasCtxt *ctx, TCGv src1)
{
    tcg_gen_subi_tl(cpu_sp, cpu_sp, 4);
    tcg_gen_qemu_st_tl(src1, cpu_sp, ctx->memidx, MO_UL);

    return BS_NONE;
}

/*
    BCLR
*/
int arc_gen_BCLR(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(rslt, src2, 0x3f);
    tcg_gen_shr_tl(rslt, ctx->one, rslt);
    tcg_gen_andc_tl(rslt, src1, rslt);  /*  rslt = src1 & ~(1 << src2)      */

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    BMSK
*/
int arc_gen_BMSK(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;
    TCGv mask = tcg_temp_new_i32();

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(mask, src2, 0x3f);
    tcg_gen_add_tl(mask, mask, ctx->one);
    tcg_gen_shr_tl(mask, ctx->one, mask);
    tcg_gen_sub_tl(mask, mask, ctx->one);

    tcg_gen_and_tl(rslt, src1, mask);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    tcg_temp_free_i32(mask);

    return  BS_NONE;
}

/*
    BSET
*/
int arc_gen_BSET(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(rslt, src2, 0x3f);
    tcg_gen_shr_tl(rslt, ctx->one, rslt);
    tcg_gen_or_tl(rslt, src1, rslt);    /*  rslt = src1 | (1 << src2)   */

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    BTST
*/
int arc_gen_BTST(DisasCtxt *ctx, TCGv src1, TCGv src2)
{
    TCGv rslt = tcg_temp_new_i32();

    tcg_gen_andi_tl(rslt, src2, 0x3f);
    tcg_gen_shr_tl(rslt, ctx->one, rslt);
    tcg_gen_and_tl(rslt, src1, rslt);       /*  rslt = src1 & (1 << src2)   */

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    tcg_temp_free_i32(rslt);

    return  BS_NONE;
}

/*
    BXOR
*/
int arc_gen_BXOR(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(rslt, src2, 0x3f);
    tcg_gen_shr_tl(rslt, ctx->one, rslt);
    tcg_gen_xor_tl(rslt, src1, rslt);       /*  rslt = src1 ^ (1 << src2)   */

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    RLC
*/
int arc_gen_RLC(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_shli_tl(rslt, src1, 1);
    tcg_gen_or_tl(rslt, rslt, cpu_Cf);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        tcg_gen_shri_tl(cpu_Cf, src1, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    RRC
*/
int arc_gen_RRC(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    tcg_gen_andi_tl(rslt, src1, 0xfffffffe);
    tcg_gen_or_tl(rslt, rslt, cpu_Cf);
    tcg_gen_rotri_tl(rslt, rslt, 1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, rslt, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, rslt, 31);
        tcg_gen_andi_tl(cpu_Cf, src1, 1);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return  BS_NONE;
}

/*
    NORMW
*/
int arc_gen_NORMW(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    gen_helper_normw(rslt, cpu_env, src1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, src1, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, src1, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return BS_NONE;
}

/*
    NORM
*/
int arc_gen_NORM(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv rslt = dest;

    if (TCGV_EQUAL(dest, src1)) {
        rslt = tcg_temp_new_i32();
    }

    gen_helper_norm(rslt, cpu_env, src1);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, src1, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, src1, 31);
    }

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    return BS_NONE;
}

/*
    MPY
*/
int arc_gen_MPY(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv_i64 rslt = tcg_temp_new_i64();
    TCGv_i64 srcA = tcg_temp_new_i64();
    TCGv_i64 srcB = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(srcA, src1);
    tcg_gen_ext_i32_i64(srcB, src2);

    tcg_gen_mul_i64(rslt, srcA, srcB);

    tcg_gen_trunc_i64_tl(dest, rslt);

    if (ctx->opt.f) {
        TCGv_i64 temp = tcg_temp_new_i64();

        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, dest, ctx->zero);

        tcg_gen_trunc_i64_tl(cpu_Nf, rslt);
        tcg_gen_shri_tl(cpu_Nf, cpu_Nf, 31);

        tcg_gen_ext_i32_i64(temp, dest);
        tcg_gen_setcond_i64(TCG_COND_NE, temp, temp, rslt);
        tcg_gen_trunc_i64_tl(cpu_Cf, temp);

        tcg_temp_free_i64(temp);
    }

    tcg_temp_free_i64(rslt);
    tcg_temp_free_i64(srcA);
    tcg_temp_free_i64(srcB);

    return BS_NONE;
}

/*
    MPYH
*/
int arc_gen_MPYH(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv_i64    rslt = tcg_temp_new_i64();
    TCGv_i64    srcA = tcg_temp_new_i64();
    TCGv_i64    srcB = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(srcA, src1);
    tcg_gen_ext_i32_i64(srcB, src2);

    tcg_gen_mul_i64(rslt, srcA, srcB);

    tcg_gen_sari_i64(rslt, rslt, 32);
    tcg_gen_trunc_i64_tl(dest, rslt);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, dest, ctx->zero);
        tcg_gen_shri_tl(cpu_Nf, dest, 31);
        tcg_gen_movi_tl(cpu_Vf, 0);
    }

    tcg_temp_free_i64(rslt);
    tcg_temp_free_i64(srcA);
    tcg_temp_free_i64(srcB);

    return BS_NONE;
}

/*
    MPYHU
*/
int arc_gen_MPYHU(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv_i64    rslt = tcg_temp_new_i64();
    TCGv_i64    srcA = tcg_temp_new_i64();
    TCGv_i64    srcB = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(srcA, src1);
    tcg_gen_extu_i32_i64(srcB, src2);

    tcg_gen_mul_i64(rslt, srcA, srcB);

    tcg_gen_shri_i64(rslt, rslt, 32);
    tcg_gen_trunc_i64_tl(dest, rslt);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, dest, ctx->zero);
        tcg_gen_movi_tl(cpu_Nf, 0);
        tcg_gen_movi_tl(cpu_Vf, 0);
    }

    tcg_temp_free_i64(rslt);
    tcg_temp_free_i64(srcA);
    tcg_temp_free_i64(srcB);

    return BS_NONE;
}

/*
    MPYU
*/
int arc_gen_MPYU(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv_i64    rslt = tcg_temp_new_i64();
    TCGv_i64    srcA = tcg_temp_new_i64();
    TCGv_i64    srcB = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(srcA, src1);
    tcg_gen_extu_i32_i64(srcB, src2);

    tcg_gen_mul_i64(rslt, srcA, srcB);

    tcg_gen_trunc_i64_tl(dest, rslt);

    if (ctx->opt.f) {
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_Zf, dest, ctx->zero);
        tcg_gen_movi_tl(cpu_Nf, 0);
        tcg_gen_shri_i64(rslt, rslt, 32);
        tcg_gen_trunc_i64_tl(cpu_Vf, rslt);
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_Vf, cpu_Vf, 0);
    }

    tcg_temp_free_i64(rslt);
    tcg_temp_free_i64(srcA);
    tcg_temp_free_i64(srcB);

    return BS_NONE;
}

/*
    DIVAW
*/
int arc_gen_DIVAW(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGLabel *label_else = gen_new_label();
    TCGLabel *label_done = gen_new_label();
    TCGv rslt = dest;
    TCGv temp = dest;

    if (TCGV_EQUAL(dest, src1) || TCGV_EQUAL(dest, src2)) {
        rslt = tcg_temp_new_i32();
    }

    /*
        if (src1 == 0)
            dest = 0
        else
        {
            src1_temp = src1 << 1
            if (src1_temp >= src2)
                dest = ((src1_temp - src2) | 0x0000_0001)
            else
                dest = src1_temp
        }
    */

    tcg_gen_brcondi_tl(TCG_COND_NE, src1, 0, label_else);

    tcg_gen_xor_tl(rslt, rslt, rslt);
    tcg_gen_br(label_done);

gen_set_label(label_else);
    tcg_gen_shli_tl(temp, src1, 1);
    tcg_gen_mov_tl(rslt, temp);
    tcg_gen_brcond_tl(TCG_COND_LT, temp, src2, label_done);
        tcg_gen_sub_tl(rslt, temp, src2);
        tcg_gen_ori_tl(rslt, rslt, 1);

gen_set_label(label_done);

    if (!TCGV_EQUAL(dest, rslt)) {
        tcg_gen_mov_tl(dest, rslt);
        tcg_temp_free_i32(rslt);
    }

    tcg_temp_free_i32(temp);

    return  BS_NONE;
}

/*
    MUL64
*/
int arc_gen_MUL64(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv_i64    rslt = tcg_temp_new_i64();
    TCGv_i64    srcA = tcg_temp_new_i64();
    TCGv_i64    srcB = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(srcA, src1);
    tcg_gen_ext_i32_i64(srcB, src2);

    tcg_gen_mul_i64(rslt, srcA, srcB);

    tcg_gen_trunc_i64_tl(cpu_mlo, rslt);
    tcg_gen_sari_i64(rslt, rslt, 16);
    tcg_gen_trunc_i64_tl(cpu_mmi, rslt);
    tcg_gen_sari_i64(rslt, rslt, 16);
    tcg_gen_trunc_i64_tl(cpu_mhi, rslt);

    tcg_temp_free_i64(rslt);
    tcg_temp_free_i64(srcA);
    tcg_temp_free_i64(srcB);

    return BS_NONE;
}

/*
    MULU64
*/
int arc_gen_MULU64(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv_i64    rslt = tcg_temp_new_i64();
    TCGv_i64    srcA = tcg_temp_new_i64();
    TCGv_i64    srcB = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(srcA, src1);
    tcg_gen_extu_i32_i64(srcB, src2);

    tcg_gen_mul_i64(rslt, srcA, srcB);

    tcg_gen_trunc_i64_tl(cpu_mlo, rslt);
    tcg_gen_shri_i64(rslt, rslt, 16);
    tcg_gen_trunc_i64_tl(cpu_mmi, rslt);
    tcg_gen_shri_i64(rslt, rslt, 16);
    tcg_gen_trunc_i64_tl(cpu_mhi, rslt);

    tcg_temp_free_i64(rslt);
    tcg_temp_free_i64(srcA);
    tcg_temp_free_i64(srcB);

    return BS_NONE;
}

/*
    BBIT0
*/
int arc_gen_BBIT0(DisasCtxt *ctx, TCGv src1, TCGv src2, TCGv rd)
{
    TCGv mask = tcg_temp_new_i32();
    TCGv cond = tcg_temp_new_i32();
    TCGLabel *label_done = gen_new_label();
    TCGLabel *label_jump = gen_new_label();

    tcg_gen_andi_tl(mask, src2, 0x31);
    tcg_gen_shr_tl(mask, ctx->one, mask);
    tcg_gen_andc_tl(cond, src1, mask);      /*  cond = src1 & ~(1 << src2)   */

    tcg_gen_brcond_tl(TCG_COND_EQ, cond, ctx->zero, label_jump);

    tcg_gen_movi_tl(cpu_pc, ctx->npc);
    arc_gen_exec_delayslot(ctx);
    tcg_gen_br(label_done);

gen_set_label(label_jump);
    tcg_gen_shli_tl(cpu_pc, rd, 1);
    tcg_gen_addi_tl(cpu_pc, cpu_pc, ctx->pcl);
    if (ctx->opt.d == 0) {
        arc_gen_kill_delayslot(ctx);
    } else {
        arc_gen_exec_delayslot(ctx);
    }

gen_set_label(label_done);
    tcg_temp_free_i32(cond);
    tcg_temp_free_i32(mask);

    return  BS_BRANCH_DS;
}

/*
    BBIT1
*/
int arc_gen_BBIT1(DisasCtxt *ctx, TCGv src1, TCGv src2, TCGv rd)
{
    TCGv mask = tcg_temp_new_i32();
    TCGv cond = tcg_temp_new_i32();
    TCGLabel *label_done = gen_new_label();
    TCGLabel *label_jump = gen_new_label();

    tcg_gen_andi_tl(mask, src2, 0x31);
    tcg_gen_shr_tl(mask, ctx->one, mask);
    tcg_gen_and_tl(cond, src1, mask);       /*  cond = src1 & (1 << src2)   */

    tcg_gen_brcond_tl(TCG_COND_EQ, cond, ctx->zero, label_jump);

    tcg_gen_movi_tl(cpu_pc, ctx->dpc);
    arc_gen_exec_delayslot(ctx);
    tcg_gen_br(label_done);

gen_set_label(label_jump);
    tcg_gen_shli_tl(cpu_pc, rd, 1);
    tcg_gen_addi_tl(cpu_pc, cpu_pc, ctx->pcl);
    if (ctx->opt.d == 0) {
        arc_gen_kill_delayslot(ctx);
    } else {
        arc_gen_exec_delayslot(ctx);
    }

gen_set_label(label_done);
    tcg_temp_free_i32(cond);
    tcg_temp_free_i32(mask);

    return  BS_BRANCH_DS;
}

/*
    BR
*/
int arc_gen_BR(DisasCtxt *ctx, TCGv src1, TCGv src2, TCGv Rd, TCGCond cond)
{
    TCGLabel *label_done = gen_new_label();
    TCGLabel *label_jump = gen_new_label();

    tcg_gen_brcond_tl(cond, src1, src2, label_jump);

    tcg_gen_movi_tl(cpu_pc, ctx->dpc);
    arc_gen_exec_delayslot(ctx);
    tcg_gen_br(label_done);

gen_set_label(label_jump);
    tcg_gen_shli_tl(cpu_pc, Rd, 1);
    tcg_gen_addi_tl(cpu_pc, cpu_pc, ctx->pcl);
    if (ctx->opt.d == 0) {
        arc_gen_kill_delayslot(ctx);
    } else {
        arc_gen_exec_delayslot(ctx);
    }

gen_set_label(label_done);

    return  BS_BRANCH_DS;
}

/*
    B
*/
int arc_gen_B(DisasCtxt *ctx, TCGv rd, ARC_COND cond)
{
    TCGLabel *label_done = gen_new_label();
    TCGLabel *label_fall = gen_new_label();

    arc_gen_jump_ifnot(ctx, cond, label_fall);

    tcg_gen_shli_tl(cpu_pc, rd, 1);
    tcg_gen_addi_tl(cpu_pc, cpu_pc, ctx->pcl);
    if (ctx->opt.d == 0) {
        arc_gen_kill_delayslot(ctx);
    } else {
        arc_gen_exec_delayslot(ctx);
    }
    tcg_gen_br(label_done);

gen_set_label(label_fall);
    tcg_gen_movi_tl(cpu_pc, ctx->dpc);
    arc_gen_exec_delayslot(ctx);

gen_set_label(label_done);

    return  BS_BRANCH_DS;
}

/*
    BL
*/
int arc_gen_BL(DisasCtxt *ctx, TCGv Rd, ARC_COND cond)
{
    TCGLabel *label_done = gen_new_label();
    TCGLabel *label_fall = gen_new_label();

    arc_gen_jump_ifnot(ctx, cond, label_fall);

    tcg_gen_shli_tl(cpu_pc, Rd, 2);
    tcg_gen_addi_tl(cpu_pc, cpu_pc, ctx->pcl);
    if (ctx->opt.d == 0) {
        tcg_gen_movi_tl(cpu_blink, ctx->npc);
        arc_gen_kill_delayslot(ctx);
    } else {
        tcg_gen_movi_tl(cpu_blink, ctx->dpc);
        arc_gen_exec_delayslot(ctx);
    }
    tcg_gen_br(label_done);

gen_set_label(label_fall);
    tcg_gen_movi_tl(cpu_pc, ctx->dpc);
    arc_gen_exec_delayslot(ctx);

gen_set_label(label_done);

    return  BS_BRANCH_DS;
}

/*
    J
*/
int arc_gen_J(DisasCtxt *ctx, TCGv src1, ARC_COND cond)
{
    TCGLabel *label_done = gen_new_label();
    TCGLabel *label_fall = gen_new_label();

    arc_gen_jump_ifnot(ctx, cond, label_fall);

    if (ctx->opt.f) {
        if (TCGV_EQUAL(src1, cpu_ilink1)) {
            tcg_gen_mov_tl(cpu_Lf, cpu_l1_Lf);
            tcg_gen_mov_tl(cpu_Zf, cpu_l1_Zf);
            tcg_gen_mov_tl(cpu_Nf, cpu_l1_Nf);
            tcg_gen_mov_tl(cpu_Cf, cpu_l1_Cf);
            tcg_gen_mov_tl(cpu_Vf, cpu_l1_Vf);
            tcg_gen_mov_tl(cpu_Uf, cpu_l1_Uf);

            tcg_gen_mov_tl(cpu_DEf, cpu_l1_DEf);
            tcg_gen_mov_tl(cpu_AEf, cpu_l1_AEf);
            tcg_gen_mov_tl(cpu_A2f, cpu_l1_A2f);
            tcg_gen_mov_tl(cpu_A1f, cpu_l1_A1f);
            tcg_gen_mov_tl(cpu_E2f, cpu_l1_E2f);
            tcg_gen_mov_tl(cpu_E1f, cpu_l1_E1f);

            tcg_gen_mov_tl(cpu_bta, cpu_bta_l1);
        }
        if (TCGV_EQUAL(src1, cpu_ilink2)) {
            tcg_gen_mov_tl(cpu_Lf, cpu_l2_Lf);
            tcg_gen_mov_tl(cpu_Zf, cpu_l2_Zf);
            tcg_gen_mov_tl(cpu_Nf, cpu_l2_Nf);
            tcg_gen_mov_tl(cpu_Cf, cpu_l2_Cf);
            tcg_gen_mov_tl(cpu_Vf, cpu_l2_Vf);
            tcg_gen_mov_tl(cpu_Uf, cpu_l2_Uf);

            tcg_gen_mov_tl(cpu_DEf, cpu_l2_DEf);
            tcg_gen_mov_tl(cpu_AEf, cpu_l2_AEf);
            tcg_gen_mov_tl(cpu_A2f, cpu_l2_A2f);
            tcg_gen_mov_tl(cpu_A1f, cpu_l2_A1f);
            tcg_gen_mov_tl(cpu_E2f, cpu_l2_E2f);
            tcg_gen_mov_tl(cpu_E1f, cpu_l2_E1f);

            tcg_gen_mov_tl(cpu_bta, cpu_bta_l2);
        }
    }

    tcg_gen_mov_tl(cpu_pc, src1);
    if (ctx->opt.d == 0) {
        arc_gen_kill_delayslot(ctx);
    } else {
        arc_gen_exec_delayslot(ctx);
    }
    tcg_gen_br(label_done);

gen_set_label(label_fall);
    tcg_gen_movi_tl(cpu_pc, ctx->dpc);
    arc_gen_exec_delayslot(ctx);

gen_set_label(label_done);

    return  BS_BRANCH_DS;
}

/*
    JL
*/
int arc_gen_JL(DisasCtxt *ctx, TCGv src1, ARC_COND cond)
{
    TCGLabel *label_done = gen_new_label();
    TCGLabel *label_fall = gen_new_label();

    arc_gen_jump_ifnot(ctx, cond, label_fall);

    tcg_gen_mov_tl(cpu_pc, src1);
    if (ctx->opt.d == 0) {
        tcg_gen_movi_tl(cpu_blink, ctx->npc);
        arc_gen_kill_delayslot(ctx);
    } else {
        tcg_gen_movi_tl(cpu_blink, ctx->dpc);
        arc_gen_exec_delayslot(ctx);
    }
    tcg_gen_br(label_done);

gen_set_label(label_fall);
    tcg_gen_movi_tl(cpu_pc, ctx->dpc);
    arc_gen_exec_delayslot(ctx);

gen_set_label(label_done);

    return  BS_BRANCH_DS;
}

/*
    LR
*/
int arc_gen_LR(DisasCtxt *ctx, TCGv dest, TCGv src1)
{
    TCGv cpc = tcg_const_local_i32((ctx->cpc + 3) & 0xfffffffc);
    TCGv npc = tcg_const_local_i32((ctx->npc + 3) & 0xfffffffc);

    gen_helper_lr(dest, cpu_env, src1);

    tcg_temp_free_i32(cpc);
    tcg_temp_free_i32(npc);

    return BS_NONE;
}

/*
    SR
*/
int arc_gen_SR(DisasCtxt *ctx, TCGv src1, TCGv src2)
{
    gen_helper_sr(src1, src2);

    return  BS_NONE;
}

