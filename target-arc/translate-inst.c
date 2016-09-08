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

