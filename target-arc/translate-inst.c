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
