/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 * Contributed by Cupertino Miranda <cmiranda@synopsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#include "qemu/osdep.h"
#include "translate.h"
#include "target/arc/semfunc.h"

/*
 * FLAG
 *    Variables: @src
 *    Functions: getCCFlag, getRegister, getBit, hasInterrupts, Halt, ReplMask,
 *               targetHasOption, setRegister
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       status32 = getRegister (R_STATUS32);
 *       if(((getBit (@src, 0) == 1) && (getBit (status32, 7) == 0)))
 *         {
 *           if((hasInterrupts () > 0))
 *             {
 *               status32 = (status32 | 1);
 *               Halt ();
 *             };
 *         }
 *       else
 *         {
 *           ReplMask (status32, @src, 3840);
 *           if(((getBit (status32, 7) == 0) && (hasInterrupts () > 0)))
 *             {
 *               ReplMask (status32, @src, 30);
 *               if(targetHasOption (DIV_REM_OPTION))
 *                 {
 *                   ReplMask (status32, @src, 8192);
 *                 };
 *               if(targetHasOption (STACK_CHECKING))
 *                 {
 *                   ReplMask (status32, @src, 16384);
 *                 };
 *               if(targetHasOption (LL64_OPTION))
 *                 {
 *                   ReplMask (status32, @src, 524288);
 *                 };
 *               ReplMask (status32, @src, 1048576);
 *             };
 *         };
 *       setRegister (R_STATUS32, status32);
 *     };
 * }
 */

int
arc_gen_FLAG(DisasCtxt *ctx, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_13 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_14 = tcg_temp_local_new_i32();
    TCGv status32 = tcg_temp_local_new_i32();
    TCGv temp_16 = tcg_temp_local_new_i32();
    TCGv temp_15 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_18 = tcg_temp_local_new_i32();
    TCGv temp_17 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_19 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_20 = tcg_temp_local_new_i32();
    TCGv temp_22 = tcg_temp_local_new_i32();
    TCGv temp_21 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_23 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    TCGv temp_24 = tcg_temp_local_new_i32();
    TCGv temp_25 = tcg_temp_local_new_i32();
    TCGv temp_26 = tcg_temp_local_new_i32();
    TCGv temp_27 = tcg_temp_local_new_i32();
    TCGv temp_28 = tcg_temp_local_new_i32();
    getCCFlag(temp_13);
    tcg_gen_mov_i32(cc_flag, temp_13);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    getRegister(temp_14, R_STATUS32);
    tcg_gen_mov_i32(status32, temp_14);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_movi_i32(temp_16, 0);
    getBit(temp_15, src, temp_16);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_3, temp_15, 1);
    tcg_gen_movi_i32(temp_18, 7);
    getBit(temp_17, status32, temp_18);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_4, temp_17, 0);
    tcg_gen_and_i32(temp_5, temp_3, temp_4);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, else_2);
    TCGLabel *done_3 = gen_new_label();
    hasInterrupts(temp_19);
    tcg_gen_setcondi_i32(TCG_COND_GT, temp_7, temp_19, 0);
    tcg_gen_xori_i32(temp_8, temp_7, 1);
    tcg_gen_andi_i32(temp_8, temp_8, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_8, arc_true, done_3);
    tcg_gen_ori_i32(status32, status32, 1);
    Halt();
    gen_set_label(done_3);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    tcg_gen_movi_i32(temp_20, 3840);
    ReplMask(status32, src, temp_20);
    TCGLabel *done_4 = gen_new_label();
    tcg_gen_movi_i32(temp_22, 7);
    getBit(temp_21, status32, temp_22);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_9, temp_21, 0);
    hasInterrupts(temp_23);
    tcg_gen_setcondi_i32(TCG_COND_GT, temp_10, temp_23, 0);
    tcg_gen_and_i32(temp_11, temp_9, temp_10);
    tcg_gen_xori_i32(temp_12, temp_11, 1);
    tcg_gen_andi_i32(temp_12, temp_12, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_12, arc_true, done_4);
    tcg_gen_movi_i32(temp_24, 30);
    ReplMask(status32, src, temp_24);
    if (targetHasOption (DIV_REM_OPTION)) {
        tcg_gen_movi_i32(temp_25, 8192);
        ReplMask(status32, src, temp_25);
    }
    if (targetHasOption (STACK_CHECKING)) {
        tcg_gen_movi_i32(temp_26, 16384);
        ReplMask(status32, src, temp_26);
    }
    if (targetHasOption (LL64_OPTION)) {
        tcg_gen_movi_i32(temp_27, 524288);
        ReplMask(status32, src, temp_27);
    }
    tcg_gen_movi_i32(temp_28, 1048576);
    ReplMask(status32, src, temp_28);
    gen_set_label(done_4);
    gen_set_label(done_2);
    setRegister(R_STATUS32, status32);
    gen_set_label(done_1);
    tcg_temp_free(temp_13);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_14);
    tcg_temp_free(status32);
    tcg_temp_free(temp_16);
    tcg_temp_free(temp_15);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_18);
    tcg_temp_free(temp_17);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_19);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_20);
    tcg_temp_free(temp_22);
    tcg_temp_free(temp_21);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_23);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_11);
    tcg_temp_free(temp_12);
    tcg_temp_free(temp_24);
    tcg_temp_free(temp_25);
    tcg_temp_free(temp_26);
    tcg_temp_free(temp_27);
    tcg_temp_free(temp_28);

    return ret;
}


/*
 * KFLAG
 *    Variables: @src
 *    Functions: getCCFlag, getRegister, getBit, hasInterrupts, Halt, ReplMask,
 *               targetHasOption, setRegister
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       status32 = getRegister (R_STATUS32);
 *       if(((getBit (@src, 0) == 1) && (getBit (status32, 7) == 0)))
 *         {
 *           if((hasInterrupts () > 0))
 *             {
 *               status32 = (status32 | 1);
 *               Halt ();
 *             };
 *         }
 *       else
 *         {
 *           ReplMask (status32, @src, 3840);
 *           if(((getBit (status32, 7) == 0) && (hasInterrupts () > 0)))
 *             {
 *               ReplMask (status32, @src, 62);
 *               if(targetHasOption (DIV_REM_OPTION))
 *                 {
 *                   ReplMask (status32, @src, 8192);
 *                 };
 *               if(targetHasOption (STACK_CHECKING))
 *                 {
 *                   ReplMask (status32, @src, 16384);
 *                 };
 *               ReplMask (status32, @src, 65536);
 *               if(targetHasOption (LL64_OPTION))
 *                 {
 *                   ReplMask (status32, @src, 524288);
 *                 };
 *               ReplMask (status32, @src, 1048576);
 *               ReplMask (status32, @src, 2147483648);
 *             };
 *         };
 *       setRegister (R_STATUS32, status32);
 *     };
 * }
 */

int
arc_gen_KFLAG(DisasCtxt *ctx, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_13 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_14 = tcg_temp_local_new_i32();
    TCGv status32 = tcg_temp_local_new_i32();
    TCGv temp_16 = tcg_temp_local_new_i32();
    TCGv temp_15 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_18 = tcg_temp_local_new_i32();
    TCGv temp_17 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_19 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_20 = tcg_temp_local_new_i32();
    TCGv temp_22 = tcg_temp_local_new_i32();
    TCGv temp_21 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_23 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    TCGv temp_24 = tcg_temp_local_new_i32();
    TCGv temp_25 = tcg_temp_local_new_i32();
    TCGv temp_26 = tcg_temp_local_new_i32();
    TCGv temp_27 = tcg_temp_local_new_i32();
    TCGv temp_28 = tcg_temp_local_new_i32();
    TCGv temp_29 = tcg_temp_local_new_i32();
    TCGv temp_30 = tcg_temp_local_new_i32();
    getCCFlag(temp_13);
    tcg_gen_mov_i32(cc_flag, temp_13);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    getRegister(temp_14, R_STATUS32);
    tcg_gen_mov_i32(status32, temp_14);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_movi_i32(temp_16, 0);
    getBit(temp_15, src, temp_16);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_3, temp_15, 1);
    tcg_gen_movi_i32(temp_18, 7);
    getBit(temp_17, status32, temp_18);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_4, temp_17, 0);
    tcg_gen_and_i32(temp_5, temp_3, temp_4);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, else_2);
    TCGLabel *done_3 = gen_new_label();
    hasInterrupts(temp_19);
    tcg_gen_setcondi_i32(TCG_COND_GT, temp_7, temp_19, 0);
    tcg_gen_xori_i32(temp_8, temp_7, 1);
    tcg_gen_andi_i32(temp_8, temp_8, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_8, arc_true, done_3);
    tcg_gen_ori_i32(status32, status32, 1);
    Halt();
    gen_set_label(done_3);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    tcg_gen_movi_i32(temp_20, 3840);
    ReplMask(status32, src, temp_20);
    TCGLabel *done_4 = gen_new_label();
    tcg_gen_movi_i32(temp_22, 7);
    getBit(temp_21, status32, temp_22);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_9, temp_21, 0);
    hasInterrupts(temp_23);
    tcg_gen_setcondi_i32(TCG_COND_GT, temp_10, temp_23, 0);
    tcg_gen_and_i32(temp_11, temp_9, temp_10);
    tcg_gen_xori_i32(temp_12, temp_11, 1);
    tcg_gen_andi_i32(temp_12, temp_12, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_12, arc_true, done_4);
    tcg_gen_movi_i32(temp_24, 62);
    ReplMask(status32, src, temp_24);
    if (targetHasOption (DIV_REM_OPTION)) {
        tcg_gen_movi_i32(temp_25, 8192);
        ReplMask(status32, src, temp_25);
    }
    if (targetHasOption (STACK_CHECKING)) {
        tcg_gen_movi_i32(temp_26, 16384);
        ReplMask(status32, src, temp_26);
    }
    tcg_gen_movi_i32(temp_27, 65536);
    ReplMask(status32, src, temp_27);
    if (targetHasOption (LL64_OPTION)) {
        tcg_gen_movi_i32(temp_28, 524288);
        ReplMask(status32, src, temp_28);
    }
    tcg_gen_movi_i32(temp_29, 1048576);
    ReplMask(status32, src, temp_29);
    tcg_gen_movi_i32(temp_30, 2147483648);
    ReplMask(status32, src, temp_30);
    gen_set_label(done_4);
    gen_set_label(done_2);
    setRegister(R_STATUS32, status32);
    gen_set_label(done_1);
    tcg_temp_free(temp_13);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_14);
    tcg_temp_free(status32);
    tcg_temp_free(temp_16);
    tcg_temp_free(temp_15);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_18);
    tcg_temp_free(temp_17);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_19);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_20);
    tcg_temp_free(temp_22);
    tcg_temp_free(temp_21);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_23);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_11);
    tcg_temp_free(temp_12);
    tcg_temp_free(temp_24);
    tcg_temp_free(temp_25);
    tcg_temp_free(temp_26);
    tcg_temp_free(temp_27);
    tcg_temp_free(temp_28);
    tcg_temp_free(temp_29);
    tcg_temp_free(temp_30);

    return ret;
}


/*
 * ADD
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarryADD,
 *               setVFlag, OverflowADD
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   lc = @c;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       @a = (@b + @c);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarryADD (@a, lb, lc));
 *           setVFlag (OverflowADD (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_ADD(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_add_i32(a, b, c);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarryADD(temp_5, a, lb, lc);
        tcg_gen_mov_i32(temp_4, temp_5);
        setCFlag(temp_4);
        OverflowADD(temp_7, a, lb, lc);
        tcg_gen_mov_i32(temp_6, temp_7);
        setVFlag(temp_6);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * ADD1
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarryADD,
 *               setVFlag, OverflowADD
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   lc = @c;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       @a = (@b + (@c << 1));
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarryADD (@a, lb, lc));
 *           setVFlag (OverflowADD (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_ADD1(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_shli_i32(temp_4, c, 1);
    tcg_gen_add_i32(a, b, temp_4);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarryADD(temp_6, a, lb, lc);
        tcg_gen_mov_i32(temp_5, temp_6);
        setCFlag(temp_5);
        OverflowADD(temp_8, a, lb, lc);
        tcg_gen_mov_i32(temp_7, temp_8);
        setVFlag(temp_7);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);

    return ret;
}


/*
 * ADD2
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarryADD,
 *               setVFlag, OverflowADD
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   lc = @c;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       @a = (@b + (@c << 2));
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarryADD (@a, lb, lc));
 *           setVFlag (OverflowADD (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_ADD2(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_shli_i32(temp_4, c, 2);
    tcg_gen_add_i32(a, b, temp_4);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarryADD(temp_6, a, lb, lc);
        tcg_gen_mov_i32(temp_5, temp_6);
        setCFlag(temp_5);
        OverflowADD(temp_8, a, lb, lc);
        tcg_gen_mov_i32(temp_7, temp_8);
        setVFlag(temp_7);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);

    return ret;
}


/*
 * ADD3
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarryADD,
 *               setVFlag, OverflowADD
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   lc = @c;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       @a = (@b + (@c << 3));
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarryADD (@a, lb, lc));
 *           setVFlag (OverflowADD (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_ADD3(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_shli_i32(temp_4, c, 3);
    tcg_gen_add_i32(a, b, temp_4);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarryADD(temp_6, a, lb, lc);
        tcg_gen_mov_i32(temp_5, temp_6);
        setCFlag(temp_5);
        OverflowADD(temp_8, a, lb, lc);
        tcg_gen_mov_i32(temp_7, temp_8);
        setVFlag(temp_7);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);

    return ret;
}


/*
 * ADC
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getCFlag, getFFlag, setZFlag, setNFlag, setCFlag,
 *               CarryADD, setVFlag, OverflowADD
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   lc = @c;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       @a = ((@b + @c) + getCFlag ());
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarryADD (@a, lb, lc));
 *           setVFlag (OverflowADD (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_ADC(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_add_i32(temp_4, b, c);
    getCFlag(temp_6);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_add_i32(a, temp_4, temp_5);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarryADD(temp_8, a, lb, lc);
        tcg_gen_mov_i32(temp_7, temp_8);
        setCFlag(temp_7);
        OverflowADD(temp_10, a, lb, lc);
        tcg_gen_mov_i32(temp_9, temp_10);
        setVFlag(temp_9);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_9);

    return ret;
}


/*
 * SBC
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getCFlag, getFFlag, setZFlag, setNFlag, setCFlag,
 *               CarryADD, setVFlag, OverflowADD
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   lc = @c;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       @a = ((@b - @c) - getCFlag ());
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarryADD (@a, lb, lc));
 *           setVFlag (OverflowADD (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_SBC(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_sub_i32(temp_4, b, c);
    getCFlag(temp_6);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_sub_i32(a, temp_4, temp_5);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarryADD(temp_8, a, lb, lc);
        tcg_gen_mov_i32(temp_7, temp_8);
        setCFlag(temp_7);
        OverflowADD(temp_10, a, lb, lc);
        tcg_gen_mov_i32(temp_9, temp_10);
        setVFlag(temp_9);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_9);

    return ret;
}


/*
 * NEG
 *    Variables: @b, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarrySUB,
 *               setVFlag, OverflowSUB
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       @a = (0 - @b);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarrySUB (@a, 0, lb));
 *           setVFlag (OverflowSUB (@a, 0, lb));
 *         };
 *     };
 * }
 */

int
arc_gen_NEG(DisasCtxt *ctx, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_subfi_i32(a, 0, b);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        tcg_gen_movi_i32(temp_6, 0);
        CarrySUB(temp_5, a, temp_6, lb);
        tcg_gen_mov_i32(temp_4, temp_5);
        setCFlag(temp_4);
        tcg_gen_movi_i32(temp_9, 0);
        OverflowSUB(temp_8, a, temp_9, lb);
        tcg_gen_mov_i32(temp_7, temp_8);
        setVFlag(temp_7);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);

    return ret;
}


/*
 * SUB
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarrySUB,
 *               setVFlag, OverflowSUB
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       @a = (@b - @c);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarrySUB (@a, lb, lc));
 *           setVFlag (OverflowSUB (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_SUB(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_sub_i32(a, b, c);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarrySUB(temp_5, a, lb, lc);
        tcg_gen_mov_i32(temp_4, temp_5);
        setCFlag(temp_4);
        OverflowSUB(temp_7, a, lb, lc);
        tcg_gen_mov_i32(temp_6, temp_7);
        setVFlag(temp_6);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lc);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * SUB1
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarrySUB,
 *               setVFlag, OverflowSUB
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = (@c << 1);
 *       @a = (@b - lc);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarrySUB (@a, lb, lc));
 *           setVFlag (OverflowSUB (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_SUB1(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_shli_i32(lc, c, 1);
    tcg_gen_sub_i32(a, b, lc);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarrySUB(temp_5, a, lb, lc);
        tcg_gen_mov_i32(temp_4, temp_5);
        setCFlag(temp_4);
        OverflowSUB(temp_7, a, lb, lc);
        tcg_gen_mov_i32(temp_6, temp_7);
        setVFlag(temp_6);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lc);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * SUB2
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarrySUB,
 *               setVFlag, OverflowSUB
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = (@c << 2);
 *       @a = (@b - lc);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarrySUB (@a, lb, lc));
 *           setVFlag (OverflowSUB (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_SUB2(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_shli_i32(lc, c, 2);
    tcg_gen_sub_i32(a, b, lc);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarrySUB(temp_5, a, lb, lc);
        tcg_gen_mov_i32(temp_4, temp_5);
        setCFlag(temp_4);
        OverflowSUB(temp_7, a, lb, lc);
        tcg_gen_mov_i32(temp_6, temp_7);
        setVFlag(temp_6);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lc);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * SUB3
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarrySUB,
 *               setVFlag, OverflowSUB
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = (@c << 3);
 *       @a = (@b - lc);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setCFlag (CarrySUB (@a, lb, lc));
 *           setVFlag (OverflowSUB (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_SUB3(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    tcg_gen_mov_i32(lb, b);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_shli_i32(lc, c, 3);
    tcg_gen_sub_i32(a, b, lc);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        CarrySUB(temp_5, a, lb, lc);
        tcg_gen_mov_i32(temp_4, temp_5);
        setCFlag(temp_4);
        OverflowSUB(temp_7, a, lb, lc);
        tcg_gen_mov_i32(temp_6, temp_7);
        setVFlag(temp_6);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lc);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * MAX
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarrySUB,
 *               setVFlag, OverflowSUB
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       alu = (lb - lc);
 *       if((lc >= lb))
 *         {
 *           @a = lc;
 *         }
 *       else
 *         {
 *           @a = lb;
 *         };
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (alu);
 *           setNFlag (alu);
 *           setCFlag (CarrySUB (@a, lb, lc));
 *           setVFlag (OverflowSUB (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_MAX(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv alu = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    tcg_gen_mov_i32(lb, b);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_sub_i32(alu, lb, lc);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_GE, temp_3, lc, lb);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_mov_i32(a, lc);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    tcg_gen_mov_i32(a, lb);
    gen_set_label(done_2);
    if ((getFFlag () == true)) {
        setZFlag(alu);
        setNFlag(alu);
        CarrySUB(temp_7, a, lb, lc);
        tcg_gen_mov_i32(temp_6, temp_7);
        setCFlag(temp_6);
        OverflowSUB(temp_9, a, lb, lc);
        tcg_gen_mov_i32(temp_8, temp_9);
        setVFlag(temp_8);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lc);
    tcg_temp_free(alu);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);

    return ret;
}


/*
 * MIN
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, CarrySUB,
 *               setVFlag, OverflowSUB
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   lb = @b;
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = @c;
 *       alu = (lb - lc);
 *       if((lc <= lb))
 *         {
 *           @a = lc;
 *         }
 *       else
 *         {
 *           @a = lb;
 *         };
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (alu);
 *           setNFlag (alu);
 *           setCFlag (CarrySUB (@a, lb, lc));
 *           setVFlag (OverflowSUB (@a, lb, lc));
 *         };
 *     };
 * }
 */

int
arc_gen_MIN(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv alu = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    tcg_gen_mov_i32(lb, b);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_mov_i32(lc, c);
    tcg_gen_sub_i32(alu, lb, lc);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_LE, temp_3, lc, lb);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_mov_i32(a, lc);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    tcg_gen_mov_i32(a, lb);
    gen_set_label(done_2);
    if ((getFFlag () == true)) {
        setZFlag(alu);
        setNFlag(alu);
        CarrySUB(temp_7, a, lb, lc);
        tcg_gen_mov_i32(temp_6, temp_7);
        setCFlag(temp_6);
        OverflowSUB(temp_9, a, lb, lc);
        tcg_gen_mov_i32(temp_8, temp_9);
        setVFlag(temp_8);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(lb);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lc);
    tcg_temp_free(alu);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);

    return ret;
}


/*
 * CMP
 *    Variables: @b, @c
 *    Functions: getCCFlag, setZFlag, setNFlag, setCFlag, CarrySUB, setVFlag,
 *               OverflowSUB
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       alu = (@b - @c);
 *       setZFlag (alu);
 *       setNFlag (alu);
 *       setCFlag (CarrySUB (alu, @b, @c));
 *       setVFlag (OverflowSUB (alu, @b, @c));
 *     };
 * }
 */

int
arc_gen_CMP(DisasCtxt *ctx, TCGv b, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv alu = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_sub_i32(alu, b, c);
    setZFlag(alu);
    setNFlag(alu);
    CarrySUB(temp_5, alu, b, c);
    tcg_gen_mov_i32(temp_4, temp_5);
    setCFlag(temp_4);
    OverflowSUB(temp_7, alu, b, c);
    tcg_gen_mov_i32(temp_6, temp_7);
    setVFlag(temp_6);
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(alu);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * AND
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = (@b & @c);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_AND(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_and_i32(la, b, c);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(la);

    return ret;
}


/*
 * OR
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = (@b | @c);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_OR(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_or_i32(la, b, c);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(la);

    return ret;
}


/*
 * XOR
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = (@b ^ @c);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_XOR(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_xor_i32(la, b, c);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(la);

    return ret;
}


/*
 * MOV
 *    Variables: @b, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = @b;
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_MOV(DisasCtxt *ctx, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(la, b);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(la);

    return ret;
}


/*
 * ASL
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setCFlag, getBit,
 *               setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = (@c & 31);
 *       la = (lb << lc);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *           if((lc == 0))
 *             {
 *               setCFlag (0);
 *             }
 *           else
 *             {
 *               setCFlag (getBit (lb, (32 - lc)));
 *             };
 *           if((@c == 268435457))
 *             {
 *               t1 = getBit (la, 31);
 *               t2 = getBit (lb, 31);
 *               if((t1 == t2))
 *                 {
 *                   setVFlag (0);
 *                 }
 *               else
 *                 {
 *                   setVFlag (1);
 *                 };
 *             };
 *         };
 *     };
 * }
 */

int
arc_gen_ASL(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_13 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_15 = tcg_temp_local_new_i32();
    TCGv temp_14 = tcg_temp_local_new_i32();
    TCGv t1 = tcg_temp_local_new_i32();
    TCGv temp_17 = tcg_temp_local_new_i32();
    TCGv temp_16 = tcg_temp_local_new_i32();
    TCGv t2 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_18 = tcg_temp_local_new_i32();
    TCGv temp_19 = tcg_temp_local_new_i32();
    getCCFlag(temp_9);
    tcg_gen_mov_i32(cc_flag, temp_9);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_andi_i32(lc, c, 31);
    tcg_gen_shl_i32(la, lb, lc);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
        TCGLabel *else_2 = gen_new_label();
        TCGLabel *done_2 = gen_new_label();
        tcg_gen_setcondi_i32(TCG_COND_EQ, temp_3, lc, 0);
        tcg_gen_xori_i32(temp_4, temp_3, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
        tcg_gen_movi_i32(temp_10, 0);
        setCFlag(temp_10);
        tcg_gen_br(done_2);
        gen_set_label(else_2);
        tcg_gen_subfi_i32(temp_13, 32, lc);
        getBit(temp_12, lb, temp_13);
        tcg_gen_mov_i32(temp_11, temp_12);
        setCFlag(temp_11);
        gen_set_label(done_2);
        TCGLabel *done_3 = gen_new_label();
        tcg_gen_setcondi_i32(TCG_COND_EQ, temp_5, c, 268435457);
        tcg_gen_xori_i32(temp_6, temp_5, 1);
        tcg_gen_andi_i32(temp_6, temp_6, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, done_3);
        tcg_gen_movi_i32(temp_15, 31);
        getBit(temp_14, la, temp_15);
        tcg_gen_mov_i32(t1, temp_14);
        tcg_gen_movi_i32(temp_17, 31);
        getBit(temp_16, lb, temp_17);
        tcg_gen_mov_i32(t2, temp_16);
        TCGLabel *else_4 = gen_new_label();
        TCGLabel *done_4 = gen_new_label();
        tcg_gen_setcond_i32(TCG_COND_EQ, temp_7, t1, t2);
        tcg_gen_xori_i32(temp_8, temp_7, 1);
        tcg_gen_andi_i32(temp_8, temp_8, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_8, arc_true, else_4);
        tcg_gen_movi_i32(temp_18, 0);
        setVFlag(temp_18);
        tcg_gen_br(done_4);
        gen_set_label(else_4);
        tcg_gen_movi_i32(temp_19, 1);
        setVFlag(temp_19);
        gen_set_label(done_4);
        gen_set_label(done_3);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_9);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(la);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_13);
    tcg_temp_free(temp_12);
    tcg_temp_free(temp_11);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_15);
    tcg_temp_free(temp_14);
    tcg_temp_free(t1);
    tcg_temp_free(temp_17);
    tcg_temp_free(temp_16);
    tcg_temp_free(t2);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_18);
    tcg_temp_free(temp_19);

    return ret;
}


/*
 * ASR
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, arithmeticShiftRight, getFFlag, setZFlag, setNFlag,
 *               setCFlag, getBit
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = (@c & 31);
 *       la = arithmeticShiftRight (lb, lc);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *           if((lc == 0))
 *             {
 *               setCFlag (0);
 *             }
 *           else
 *             {
 *               setCFlag (getBit (lb, (lc - 1)));
 *             };
 *         };
 *     };
 * }
 */

int
arc_gen_ASR(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_andi_i32(lc, c, 31);
    arithmeticShiftRight(temp_6, lb, lc);
    tcg_gen_mov_i32(la, temp_6);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
        TCGLabel *else_2 = gen_new_label();
        TCGLabel *done_2 = gen_new_label();
        tcg_gen_setcondi_i32(TCG_COND_EQ, temp_3, lc, 0);
        tcg_gen_xori_i32(temp_4, temp_3, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
        tcg_gen_movi_i32(temp_7, 0);
        setCFlag(temp_7);
        tcg_gen_br(done_2);
        gen_set_label(else_2);
        tcg_gen_subi_i32(temp_10, lc, 1);
        getBit(temp_9, lb, temp_10);
        tcg_gen_mov_i32(temp_8, temp_9);
        setCFlag(temp_8);
        gen_set_label(done_2);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(temp_6);
    tcg_temp_free(la);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);

    return ret;
}


/*
 * ASR8
 *    Variables: @b, @a
 *    Functions: getCCFlag, arithmeticShiftRight, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       la = arithmeticShiftRight (lb, 8);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_ASR8(DisasCtxt *ctx, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_movi_i32(temp_5, 8);
    arithmeticShiftRight(temp_4, lb, temp_5);
    tcg_gen_mov_i32(la, temp_4);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lb);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(la);

    return ret;
}


/*
 * ASR16
 *    Variables: @b, @a
 *    Functions: getCCFlag, arithmeticShiftRight, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       la = arithmeticShiftRight (lb, 16);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_ASR16(DisasCtxt *ctx, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_movi_i32(temp_5, 16);
    arithmeticShiftRight(temp_4, lb, temp_5);
    tcg_gen_mov_i32(la, temp_4);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lb);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(la);

    return ret;
}


/*
 * LSL16
 *    Variables: @b, @a
 *    Functions: getCCFlag, logicalShiftLeft, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = logicalShiftLeft (@b, 16);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_LSL16(DisasCtxt *ctx, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_movi_i32(temp_5, 16);
    logicalShiftLeft(temp_4, b, temp_5);
    tcg_gen_mov_i32(la, temp_4);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(la);

    return ret;
}


/*
 * LSL8
 *    Variables: @b, @a
 *    Functions: getCCFlag, logicalShiftLeft, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = logicalShiftLeft (@b, 8);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_LSL8(DisasCtxt *ctx, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_movi_i32(temp_5, 8);
    logicalShiftLeft(temp_4, b, temp_5);
    tcg_gen_mov_i32(la, temp_4);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(la);

    return ret;
}


/*
 * LSR
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, logicalShiftRight, getFFlag, setZFlag, setNFlag,
 *               setCFlag, getBit
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lb = @b;
 *       lc = (@c & 31);
 *       la = logicalShiftRight (lb, lc);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *           if((lc == 0))
 *             {
 *               setCFlag (0);
 *             }
 *           else
 *             {
 *               setCFlag (getBit (lb, (lc - 1)));
 *             };
 *         };
 *     };
 * }
 */

int
arc_gen_LSR(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lb = tcg_temp_local_new_i32();
    TCGv lc = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lb, b);
    tcg_gen_andi_i32(lc, c, 31);
    logicalShiftRight(temp_6, lb, lc);
    tcg_gen_mov_i32(la, temp_6);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
        TCGLabel *else_2 = gen_new_label();
        TCGLabel *done_2 = gen_new_label();
        tcg_gen_setcondi_i32(TCG_COND_EQ, temp_3, lc, 0);
        tcg_gen_xori_i32(temp_4, temp_3, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
        tcg_gen_movi_i32(temp_7, 0);
        setCFlag(temp_7);
        tcg_gen_br(done_2);
        gen_set_label(else_2);
        tcg_gen_subi_i32(temp_10, lc, 1);
        getBit(temp_9, lb, temp_10);
        tcg_gen_mov_i32(temp_8, temp_9);
        setCFlag(temp_8);
        gen_set_label(done_2);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lb);
    tcg_temp_free(lc);
    tcg_temp_free(temp_6);
    tcg_temp_free(la);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);

    return ret;
}


/*
 * LSR16
 *    Variables: @b, @a
 *    Functions: getCCFlag, logicalShiftRight, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = logicalShiftRight (@b, 16);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_LSR16(DisasCtxt *ctx, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_movi_i32(temp_5, 16);
    logicalShiftRight(temp_4, b, temp_5);
    tcg_gen_mov_i32(la, temp_4);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(la);

    return ret;
}


/*
 * LSR8
 *    Variables: @b, @a
 *    Functions: getCCFlag, logicalShiftRight, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = logicalShiftRight (@b, 8);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_LSR8(DisasCtxt *ctx, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_movi_i32(temp_5, 8);
    logicalShiftRight(temp_4, b, temp_5);
    tcg_gen_mov_i32(la, temp_4);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(la);

    return ret;
}


/*
 * BIC
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       la = (@b & ~@c);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_BIC(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_not_i32(temp_4, c);
    tcg_gen_and_i32(la, b, temp_4);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(la);

    return ret;
}


/*
 * BCLR
 *    Variables: @c, @b, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       tmp = (1 << (@c & 31));
 *       la = (@b & ~tmp);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_BCLR(DisasCtxt *ctx, TCGv c, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv tmp = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_andi_i32(temp_4, c, 31);
    tcg_gen_shlfi_i32(tmp, 1, temp_4);
    tcg_gen_not_i32(temp_5, tmp);
    tcg_gen_and_i32(la, b, temp_5);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(tmp);
    tcg_temp_free(temp_5);
    tcg_temp_free(la);

    return ret;
}


/*
 * BMSK
 *    Variables: @c, @b, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       tmp1 = ((@c & 31) + 1);
 *       if((tmp1 == 32))
 *         {
 *           tmp2 = 4294967295;
 *         }
 *       else
 *         {
 *           tmp2 = ((1 << tmp1) - 1);
 *         };
 *       la = (@b & tmp2);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_BMSK(DisasCtxt *ctx, TCGv c, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv tmp1 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv tmp2 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_andi_i32(temp_6, c, 31);
    tcg_gen_addi_i32(tmp1, temp_6, 1);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_3, tmp1, 32);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_movi_i32(tmp2, 4294967295);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    tcg_gen_shlfi_i32(temp_7, 1, tmp1);
    tcg_gen_subi_i32(tmp2, temp_7, 1);
    gen_set_label(done_2);
    tcg_gen_and_i32(la, b, tmp2);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(tmp1);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(tmp2);
    tcg_temp_free(temp_7);
    tcg_temp_free(la);

    return ret;
}


/*
 * BMSKN
 *    Variables: @c, @b, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       tmp1 = ((@c & 31) + 1);
 *       if((tmp1 == 32))
 *         {
 *           tmp2 = 4294967295;
 *         }
 *       else
 *         {
 *           tmp2 = ((1 << tmp1) - 1);
 *         };
 *       la = (@b & ~tmp2);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_BMSKN(DisasCtxt *ctx, TCGv c, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv tmp1 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv tmp2 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_andi_i32(temp_6, c, 31);
    tcg_gen_addi_i32(tmp1, temp_6, 1);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_3, tmp1, 32);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_movi_i32(tmp2, 4294967295);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    tcg_gen_shlfi_i32(temp_7, 1, tmp1);
    tcg_gen_subi_i32(tmp2, temp_7, 1);
    gen_set_label(done_2);
    tcg_gen_not_i32(temp_8, tmp2);
    tcg_gen_and_i32(la, b, temp_8);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(tmp1);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(tmp2);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);
    tcg_temp_free(la);

    return ret;
}


/*
 * BSET
 *    Variables: @c, @b, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       tmp = (1 << (@c & 31));
 *       la = (@b | tmp);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_BSET(DisasCtxt *ctx, TCGv c, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv tmp = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_andi_i32(temp_4, c, 31);
    tcg_gen_shlfi_i32(tmp, 1, temp_4);
    tcg_gen_or_i32(la, b, tmp);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(tmp);
    tcg_temp_free(la);

    return ret;
}


/*
 * BXOR
 *    Variables: @c, @b, @a
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       tmp = (1 << @c);
 *       la = (@b ^ tmp);
 *       @a = la;
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (la);
 *           setNFlag (la);
 *         };
 *     };
 * }
 */

int
arc_gen_BXOR(DisasCtxt *ctx, TCGv c, TCGv b, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv tmp = tcg_temp_local_new_i32();
    TCGv la = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_shlfi_i32(tmp, 1, c);
    tcg_gen_xor_i32(la, b, tmp);
    tcg_gen_mov_i32(a, la);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(la);
        setNFlag(la);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(tmp);
    tcg_temp_free(la);

    return ret;
}


/*
 * ROL
 *    Variables: @src, @dest
 *    Functions: getCCFlag, rotateLeft, getFFlag, setZFlag, setNFlag, setCFlag,
 *               extractBits
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lsrc = @src;
 *       @dest = rotateLeft (lsrc, 1);
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *           setCFlag (extractBits (lsrc, 31, 31));
 *         };
 *     };
 * }
 */

int
arc_gen_ROL(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lsrc = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    int f_flag;
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lsrc, src);
    tcg_gen_movi_i32(temp_5, 1);
    rotateLeft(temp_4, lsrc, temp_5);
    tcg_gen_mov_i32(dest, temp_4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
        tcg_gen_movi_i32(temp_9, 31);
        tcg_gen_movi_i32(temp_8, 31);
        extractBits(temp_7, lsrc, temp_8, temp_9);
        tcg_gen_mov_i32(temp_6, temp_7);
        setCFlag(temp_6);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lsrc);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * ROL8
 *    Variables: @src, @dest
 *    Functions: getCCFlag, rotateLeft, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lsrc = @src;
 *       @dest = rotateLeft (lsrc, 8);
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *         };
 *     };
 * }
 */

int
arc_gen_ROL8(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lsrc = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lsrc, src);
    tcg_gen_movi_i32(temp_5, 8);
    rotateLeft(temp_4, lsrc, temp_5);
    tcg_gen_mov_i32(dest, temp_4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lsrc);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * ROR
 *    Variables: @src, @n, @dest
 *    Functions: getCCFlag, rotateRight, getFFlag, setZFlag, setNFlag,
 *               setCFlag, extractBits
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lsrc = @src;
 *       ln = (@n & 31);
 *       @dest = rotateRight (lsrc, ln);
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *           setCFlag (extractBits (lsrc, (ln - 1), (ln - 1)));
 *         };
 *     };
 * }
 */

int
arc_gen_ROR(DisasCtxt *ctx, TCGv src, TCGv n, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lsrc = tcg_temp_local_new_i32();
    TCGv ln = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    int f_flag;
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lsrc, src);
    tcg_gen_andi_i32(ln, n, 31);
    rotateRight(temp_4, lsrc, ln);
    tcg_gen_mov_i32(dest, temp_4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
        tcg_gen_subi_i32(temp_8, ln, 1);
        tcg_gen_subi_i32(temp_7, ln, 1);
        extractBits(temp_6, lsrc, temp_7, temp_8);
        tcg_gen_mov_i32(temp_5, temp_6);
        setCFlag(temp_5);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lsrc);
    tcg_temp_free(ln);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);

    return ret;
}


/*
 * ROR8
 *    Variables: @src, @dest
 *    Functions: getCCFlag, rotateRight, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lsrc = @src;
 *       @dest = rotateRight (lsrc, 8);
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *         };
 *     };
 * }
 */

int
arc_gen_ROR8(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lsrc = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lsrc, src);
    tcg_gen_movi_i32(temp_5, 8);
    rotateRight(temp_4, lsrc, temp_5);
    tcg_gen_mov_i32(dest, temp_4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lsrc);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * RLC
 *    Variables: @src, @dest
 *    Functions: getCCFlag, getCFlag, getFFlag, setZFlag, setNFlag, setCFlag,
 *               extractBits
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lsrc = @src;
 *       @dest = (lsrc << 1);
 *       @dest = (@dest | getCFlag ());
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *           setCFlag (extractBits (lsrc, 31, 31));
 *         };
 *     };
 * }
 */

int
arc_gen_RLC(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lsrc = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    int f_flag;
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lsrc, src);
    tcg_gen_shli_i32(dest, lsrc, 1);
    getCFlag(temp_5);
    tcg_gen_mov_i32(temp_4, temp_5);
    tcg_gen_or_i32(dest, dest, temp_4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
        tcg_gen_movi_i32(temp_9, 31);
        tcg_gen_movi_i32(temp_8, 31);
        extractBits(temp_7, lsrc, temp_8, temp_9);
        tcg_gen_mov_i32(temp_6, temp_7);
        setCFlag(temp_6);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lsrc);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * RRC
 *    Variables: @src, @dest
 *    Functions: getCCFlag, getCFlag, getFFlag, setZFlag, setNFlag, setCFlag,
 *               extractBits
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       lsrc = @src;
 *       @dest = (lsrc >> 1);
 *       @dest = (@dest | (getCFlag () << 31));
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *           setCFlag (extractBits (lsrc, 0, 0));
 *         };
 *     };
 * }
 */

int
arc_gen_RRC(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv lsrc = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    int f_flag;
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(lsrc, src);
    tcg_gen_shri_i32(dest, lsrc, 1);
    getCFlag(temp_6);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_shli_i32(temp_4, temp_5, 31);
    tcg_gen_or_i32(dest, dest, temp_4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
        tcg_gen_movi_i32(temp_10, 0);
        tcg_gen_movi_i32(temp_9, 0);
        extractBits(temp_8, lsrc, temp_9, temp_10);
        tcg_gen_mov_i32(temp_7, temp_8);
        setCFlag(temp_7);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(lsrc);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);

    return ret;
}


/*
 * SEXB
 *    Variables: @dest, @src
 *    Functions: getCCFlag, arithmeticShiftRight, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       @dest = arithmeticShiftRight ((@src << 24), 24);
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *         };
 *     };
 * }
 */

int
arc_gen_SEXB(DisasCtxt *ctx, TCGv dest, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_movi_i32(temp_6, 24);
    tcg_gen_shli_i32(temp_5, src, 24);
    arithmeticShiftRight(temp_4, temp_5, temp_6);
    tcg_gen_mov_i32(dest, temp_4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * SEXH
 *    Variables: @dest, @src
 *    Functions: getCCFlag, arithmeticShiftRight, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       @dest = arithmeticShiftRight ((@src << 16), 16);
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *         };
 *     };
 * }
 */

int
arc_gen_SEXH(DisasCtxt *ctx, TCGv dest, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_movi_i32(temp_6, 16);
    tcg_gen_shli_i32(temp_5, src, 16);
    arithmeticShiftRight(temp_4, temp_5, temp_6);
    tcg_gen_mov_i32(dest, temp_4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * EXTB
 *    Variables: @dest, @src
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       @dest = (@src & 255);
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *         };
 *     };
 * }
 */

int
arc_gen_EXTB(DisasCtxt *ctx, TCGv dest, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_andi_i32(dest, src, 255);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);

    return ret;
}


/*
 * EXTH
 *    Variables: @dest, @src
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       @dest = (@src & 65535);
 *       f_flag = getFFlag ();
 *       if((f_flag == true))
 *         {
 *           setZFlag (@dest);
 *           setNFlag (@dest);
 *         };
 *     };
 * }
 */

int
arc_gen_EXTH(DisasCtxt *ctx, TCGv dest, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    int f_flag;
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_andi_i32(dest, src, 65535);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);

    return ret;
}


/*
 * BTST
 *    Variables: @c, @b
 *    Functions: getCCFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       tmp = (1 << (@c & 31));
 *       alu = (@b & tmp);
 *       setZFlag (alu);
 *       setNFlag (alu);
 *     };
 * }
 */

int
arc_gen_BTST(DisasCtxt *ctx, TCGv c, TCGv b)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv tmp = tcg_temp_local_new_i32();
    TCGv alu = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_andi_i32(temp_4, c, 31);
    tcg_gen_shlfi_i32(tmp, 1, temp_4);
    tcg_gen_and_i32(alu, b, tmp);
    setZFlag(alu);
    setNFlag(alu);
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(tmp);
    tcg_temp_free(alu);

    return ret;
}


/*
 * TST
 *    Variables: @b, @c
 *    Functions: getCCFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       alu = (@b & @c);
 *       setZFlag (alu);
 *       setNFlag (alu);
 *     };
 * }
 */

int
arc_gen_TST(DisasCtxt *ctx, TCGv b, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv alu = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_and_i32(alu, b, c);
    setZFlag(alu);
    setNFlag(alu);
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(alu);

    return ret;
}


/*
 * XBFU
 *    Variables: @src2, @src1, @dest
 *    Functions: getCCFlag, extractBits, getFFlag, setZFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       N = extractBits (@src2, 4, 0);
 *       M = (extractBits (@src2, 9, 5) + 1);
 *       tmp1 = (@src1 >> N);
 *       tmp2 = ((1 << M) - 1);
 *       @dest = (tmp1 & tmp2);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@dest);
 *         };
 *     };
 * }
 */

int
arc_gen_XBFU(DisasCtxt *ctx, TCGv src2, TCGv src1, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv N = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv M = tcg_temp_local_new_i32();
    TCGv tmp1 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv tmp2 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_movi_i32(temp_6, 0);
    tcg_gen_movi_i32(temp_5, 4);
    extractBits(temp_4, src2, temp_5, temp_6);
    tcg_gen_mov_i32(N, temp_4);
    tcg_gen_movi_i32(temp_10, 5);
    tcg_gen_movi_i32(temp_9, 9);
    extractBits(temp_8, src2, temp_9, temp_10);
    tcg_gen_mov_i32(temp_7, temp_8);
    tcg_gen_addi_i32(M, temp_7, 1);
    tcg_gen_shr_i32(tmp1, src1, N);
    tcg_gen_shlfi_i32(temp_11, 1, M);
    tcg_gen_subi_i32(tmp2, temp_11, 1);
    tcg_gen_and_i32(dest, tmp1, tmp2);
    if ((getFFlag () == true)) {
        setZFlag(dest);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(N);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);
    tcg_temp_free(M);
    tcg_temp_free(tmp1);
    tcg_temp_free(temp_11);
    tcg_temp_free(tmp2);

    return ret;
}


/*
 * AEX
 *    Variables: @src2, @b
 *    Functions: getCCFlag, readAuxReg, writeAuxReg
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       tmp = readAuxReg (@src2);
 *       writeAuxReg (@src2, @b);
 *       @b = tmp;
 *     };
 * }
 */

int
arc_gen_AEX(DisasCtxt *ctx, TCGv src2, TCGv b)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv tmp = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    readAuxReg(temp_4, src2);
    tcg_gen_mov_i32(tmp, temp_4);
    writeAuxReg(src2, b);
    tcg_gen_mov_i32(b, tmp);
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(tmp);

    return ret;
}


/*
 * LR
 *    Variables: @dest, @src
 *    Functions: readAuxReg
 * --- code ---
 * {
 *   @dest = readAuxReg (@src);
 * }
 */

int
arc_gen_LR(DisasCtxt *ctx, TCGv dest, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_1 = tcg_temp_local_new_i32();
    readAuxReg(temp_1, src);
    tcg_gen_mov_i32(dest, temp_1);
    tcg_temp_free(temp_1);

    return ret;
}


/*
 * SR
 *    Variables: @src2, @src1
 *    Functions: writeAuxReg
 * --- code ---
 * {
 *   writeAuxReg (@src2, @src1);
 * }
 */

int
arc_gen_SR(DisasCtxt *ctx, TCGv src2, TCGv src1)
{
    int ret = DISAS_NEXT;

    writeAuxReg(src2, src1);
    return ret;
}


/*
 * SYNC
 *    Variables:
 *    Functions: syncReturnDisasUpdate
 * --- code ---
 * {
 *   syncReturnDisasUpdate ();
 * }
 */

int
arc_gen_SYNC(DisasCtxt *ctx)
{
    int ret = DISAS_NEXT;

    syncReturnDisasUpdate();
    return ret;
}


/*
 * CLRI
 *    Variables: @c
 *    Functions: getRegister, setRegister
 * --- code ---
 * {
 *   status32 = getRegister (R_STATUS32);
 *   ie = (status32 & 2147483648);
 *   ie = (ie >> 27);
 *   e = ((status32 & 30) >> 1);
 *   a = 32;
 *   @c = ((ie | e) | a);
 *   mask = 2147483648;
 *   mask = ~mask;
 *   status32 = (status32 & mask);
 *   setRegister (R_STATUS32, status32);
 * }
 */

int
arc_gen_CLRI(DisasCtxt *ctx, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv status32 = tcg_temp_local_new_i32();
    TCGv ie = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv e = tcg_temp_local_new_i32();
    TCGv a = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv mask = tcg_temp_local_new_i32();
    getRegister(temp_1, R_STATUS32);
    tcg_gen_mov_i32(status32, temp_1);
    tcg_gen_andi_i32(ie, status32, 2147483648);
    tcg_gen_shri_i32(ie, ie, 27);
    tcg_gen_andi_i32(temp_2, status32, 30);
    tcg_gen_shri_i32(e, temp_2, 1);
    tcg_gen_movi_i32(a, 32);
    tcg_gen_or_i32(temp_3, ie, e);
    tcg_gen_or_i32(c, temp_3, a);
    tcg_gen_movi_i32(mask, 2147483648);
    tcg_gen_not_i32(mask, mask);
    tcg_gen_and_i32(status32, status32, mask);
    setRegister(R_STATUS32, status32);
    tcg_temp_free(temp_1);
    tcg_temp_free(status32);
    tcg_temp_free(ie);
    tcg_temp_free(temp_2);
    tcg_temp_free(e);
    tcg_temp_free(a);
    tcg_temp_free(temp_3);
    tcg_temp_free(mask);

    return ret;
}


/*
 * SETI
 *    Variables: @c
 *    Functions: getRegister, setRegister
 * --- code ---
 * {
 *   status32 = getRegister (R_STATUS32);
 *   e_mask = 30;
 *   e_mask = ~e_mask;
 *   e_value = ((@c & 15) << 1);
 *   temp1 = (@c & 32);
 *   if((temp1 != 0))
 *     {
 *       status32 = ((status32 & e_mask) | e_value);
 *       ie_mask = 2147483648;
 *       ie_mask = ~ie_mask;
 *       ie_value = ((@c & 16) << 27);
 *       status32 = ((status32 & ie_mask) | ie_value);
 *     }
 *   else
 *     {
 *       status32 = (status32 | 2147483648);
 *       temp2 = (@c & 16);
 *       if((temp2 != 0))
 *         {
 *           status32 = ((status32 & e_mask) | e_value);
 *         };
 *     };
 *   setRegister (R_STATUS32, status32);
 * }
 */

int
arc_gen_SETI(DisasCtxt *ctx, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv status32 = tcg_temp_local_new_i32();
    TCGv e_mask = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv e_value = tcg_temp_local_new_i32();
    TCGv temp1 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv ie_mask = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv ie_value = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    getRegister(temp_5, R_STATUS32);
    tcg_gen_mov_i32(status32, temp_5);
    tcg_gen_movi_i32(e_mask, 30);
    tcg_gen_not_i32(e_mask, e_mask);
    tcg_gen_andi_i32(temp_6, c, 15);
    tcg_gen_shli_i32(e_value, temp_6, 1);
    tcg_gen_andi_i32(temp1, c, 32);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_1, temp1, 0);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_1);
    tcg_gen_and_i32(temp_7, status32, e_mask);
    tcg_gen_or_i32(status32, temp_7, e_value);
    tcg_gen_movi_i32(ie_mask, 2147483648);
    tcg_gen_not_i32(ie_mask, ie_mask);
    tcg_gen_andi_i32(temp_8, c, 16);
    tcg_gen_shli_i32(ie_value, temp_8, 27);
    tcg_gen_and_i32(temp_9, status32, ie_mask);
    tcg_gen_or_i32(status32, temp_9, ie_value);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    tcg_gen_ori_i32(status32, status32, 2147483648);
    tcg_gen_andi_i32(temp2, c, 16);
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_3, temp2, 0);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_2);
    tcg_gen_and_i32(temp_10, status32, e_mask);
    tcg_gen_or_i32(status32, temp_10, e_value);
    gen_set_label(done_2);
    gen_set_label(done_1);
    setRegister(R_STATUS32, status32);
    tcg_temp_free(temp_5);
    tcg_temp_free(status32);
    tcg_temp_free(e_mask);
    tcg_temp_free(temp_6);
    tcg_temp_free(e_value);
    tcg_temp_free(temp1);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_7);
    tcg_temp_free(ie_mask);
    tcg_temp_free(temp_8);
    tcg_temp_free(ie_value);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp2);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_10);

    return ret;
}


/*
 * NOP
 *    Variables:
 *    Functions: doNothing
 * --- code ---
 * {
 *   doNothing ();
 * }
 */

int
arc_gen_NOP(DisasCtxt *ctx)
{
    int ret = DISAS_NEXT;

    return ret;
}


/*
 * PREALLOC
 *    Variables:
 *    Functions: doNothing
 * --- code ---
 * {
 *   doNothing ();
 * }
 */

int
arc_gen_PREALLOC(DisasCtxt *ctx)
{
    int ret = DISAS_NEXT;

    return ret;
}


/*
 * PREFETCH
 *    Variables: @src1, @src2
 *    Functions: getAAFlag, doNothing
 * --- code ---
 * {
 *   AA = getAAFlag ();
 *   if(((AA == 1) || (AA == 2)))
 *     {
 *       @src1 = (@src1 + @src2);
 *     }
 *   else
 *     {
 *       doNothing ();
 *     };
 * }
 */

int
arc_gen_PREFETCH(DisasCtxt *ctx, TCGv src1, TCGv src2)
{
    int ret = DISAS_NEXT;
    int AA;
    AA = getAAFlag ();
    if (((AA == 1) || (AA == 2))) {
        tcg_gen_add_i32(src1, src1, src2);
    } else {
        doNothing();
    }


    return ret;
}


/*
 * MPY
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, HELPER, setZFlag, setNFlag, setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       _b = @b;
 *       _c = @c;
 *       @a = ((_b * _c) & 4294967295);
 *       if((getFFlag () == true))
 *         {
 *           high_part = HELPER (mpym, _b, _c);
 *           tmp1 = (high_part & 2147483648);
 *           tmp2 = (@a & 2147483648);
 *           setZFlag (@a);
 *           setNFlag (high_part);
 *           setVFlag ((tmp1 != tmp2));
 *         };
 *     };
 * }
 */

int
arc_gen_MPY(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv _b = tcg_temp_local_new_i32();
    TCGv _c = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv high_part = tcg_temp_local_new_i32();
    TCGv tmp1 = tcg_temp_local_new_i32();
    TCGv tmp2 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(_b, b);
    tcg_gen_mov_i32(_c, c);
    tcg_gen_mul_i32(temp_4, _b, _c);
    tcg_gen_andi_i32(a, temp_4, 4294967295);
    if ((getFFlag () == true)) {
        ARC_HELPER(mpym, high_part, _b, _c);
        tcg_gen_andi_i32(tmp1, high_part, 2147483648);
        tcg_gen_andi_i32(tmp2, a, 2147483648);
        setZFlag(a);
        setNFlag(high_part);
        tcg_gen_setcond_i32(TCG_COND_NE, temp_5, tmp1, tmp2);
        setVFlag(temp_5);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(_b);
    tcg_temp_free(_c);
    tcg_temp_free(temp_4);
    tcg_temp_free(high_part);
    tcg_temp_free(tmp1);
    tcg_temp_free(tmp2);
    tcg_temp_free(temp_5);

    return ret;
}


/*
 * MPYMU
 *    Variables: @a, @b, @c
 *    Functions: getCCFlag, HELPER, getFFlag, setZFlag, setNFlag, setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       @a = HELPER (mpymu, @b, @c);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (0);
 *           setVFlag (0);
 *         };
 *     };
 * }
 */

int
arc_gen_MPYMU(DisasCtxt *ctx, TCGv a, TCGv b, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    ARC_HELPER(mpymu, a, b, c);
    if ((getFFlag () == true)) {
        setZFlag(a);
        tcg_gen_movi_i32(temp_4, 0);
        setNFlag(temp_4);
        tcg_gen_movi_i32(temp_5, 0);
        setVFlag(temp_5);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);

    return ret;
}


/*
 * MPYM
 *    Variables: @a, @b, @c
 *    Functions: getCCFlag, HELPER, getFFlag, setZFlag, setNFlag, setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       @a = HELPER (mpym, @b, @c);
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setVFlag (0);
 *         };
 *     };
 * }
 */

int
arc_gen_MPYM(DisasCtxt *ctx, TCGv a, TCGv b, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    ARC_HELPER(mpym, a, b, c);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        tcg_gen_movi_i32(temp_4, 0);
        setVFlag(temp_4);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * MPYU
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getFFlag, HELPER, setZFlag, setNFlag, setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       _b = @b;
 *       _c = @c;
 *       @a = ((_b * _c) & 4294967295);
 *       if((getFFlag () == true))
 *         {
 *           high_part = HELPER (mpym, _b, _c);
 *           setZFlag (@a);
 *           setNFlag (0);
 *           setVFlag ((high_part > 0));
 *         };
 *     };
 * }
 */

int
arc_gen_MPYU(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv _b = tcg_temp_local_new_i32();
    TCGv _c = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv high_part = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(_b, b);
    tcg_gen_mov_i32(_c, c);
    tcg_gen_mul_i32(temp_4, _b, _c);
    tcg_gen_andi_i32(a, temp_4, 4294967295);
    if ((getFFlag () == true)) {
        ARC_HELPER(mpym, high_part, _b, _c);
        setZFlag(a);
        tcg_gen_movi_i32(temp_5, 0);
        setNFlag(temp_5);
        tcg_gen_setcondi_i32(TCG_COND_GT, temp_6, high_part, 0);
        setVFlag(temp_6);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(_b);
    tcg_temp_free(_c);
    tcg_temp_free(temp_4);
    tcg_temp_free(high_part);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * MPYUW
 *    Variables: @a, @b, @c
 *    Functions: getCCFlag, getFFlag, setZFlag, setNFlag, setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       @a = ((@b & 65535) * (@c & 65535));
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (0);
 *           setVFlag (0);
 *         };
 *     };
 * }
 */

int
arc_gen_MPYUW(DisasCtxt *ctx, TCGv a, TCGv b, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_andi_i32(temp_5, c, 65535);
    tcg_gen_andi_i32(temp_4, b, 65535);
    tcg_gen_mul_i32(a, temp_4, temp_5);
    if ((getFFlag () == true)) {
        setZFlag(a);
        tcg_gen_movi_i32(temp_6, 0);
        setNFlag(temp_6);
        tcg_gen_movi_i32(temp_7, 0);
        setVFlag(temp_7);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_7);

    return ret;
}


/*
 * MPYW
 *    Variables: @a, @b, @c
 *    Functions: getCCFlag, arithmeticShiftRight, getFFlag, setZFlag, setNFlag,
 *               setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       @a = (arithmeticShiftRight ((@b << 16), 16)
 *            * arithmeticShiftRight ((@c << 16), 16));
 *       if((getFFlag () == true))
 *         {
 *           setZFlag (@a);
 *           setNFlag (@a);
 *           setVFlag (0);
 *         };
 *     };
 * }
 */

int
arc_gen_MPYW(DisasCtxt *ctx, TCGv a, TCGv b, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    getCCFlag(temp_3);
    tcg_gen_mov_i32(cc_flag, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_movi_i32(temp_11, 16);
    tcg_gen_shli_i32(temp_10, c, 16);
    tcg_gen_movi_i32(temp_7, 16);
    tcg_gen_shli_i32(temp_6, b, 16);
    arithmeticShiftRight(temp_5, temp_6, temp_7);
    tcg_gen_mov_i32(temp_4, temp_5);
    arithmeticShiftRight(temp_9, temp_10, temp_11);
    tcg_gen_mov_i32(temp_8, temp_9);
    tcg_gen_mul_i32(a, temp_4, temp_8);
    if ((getFFlag () == true)) {
        setZFlag(a);
        setNFlag(a);
        tcg_gen_movi_i32(temp_12, 0);
        setVFlag(temp_12);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_11);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_12);

    return ret;
}


/*
 * DIV
 *    Variables: @src2, @src1, @dest
 *    Functions: getCCFlag, divSigned, getFFlag, setZFlag, setNFlag, setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       if(((@src2 != 0) && ((@src1 != 2147483648) || (@src2 != 4294967295))))
 *         {
 *           @dest = divSigned (@src1, @src2);
 *           if((getFFlag () == true))
 *             {
 *               setZFlag (@dest);
 *               setNFlag (@dest);
 *               setVFlag (0);
 *             };
 *         }
 *       else
 *         {
 *         };
 *     };
 * }
 */

int
arc_gen_DIV(DisasCtxt *ctx, TCGv src2, TCGv src1, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    getCCFlag(temp_9);
    tcg_gen_mov_i32(cc_flag, temp_9);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_3, src2, 0);
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_4, src1, 2147483648);
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_5, src2, 4294967295);
    tcg_gen_or_i32(temp_6, temp_4, temp_5);
    tcg_gen_and_i32(temp_7, temp_3, temp_6);
    tcg_gen_xori_i32(temp_8, temp_7, 1);
    tcg_gen_andi_i32(temp_8, temp_8, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_8, arc_true, else_2);
    divSigned(temp_10, src1, src2);
    tcg_gen_mov_i32(dest, temp_10);
    if ((getFFlag () == true)) {
        setZFlag(dest);
        setNFlag(dest);
        tcg_gen_movi_i32(temp_11, 0);
        setVFlag(temp_11);
    }
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    gen_set_label(done_1);
    tcg_temp_free(temp_9);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_11);

    return ret;
}


/*
 * DIVU
 *    Variables: @src2, @dest, @src1
 *    Functions: getCCFlag, divUnsigned, getFFlag, setZFlag, setNFlag,
 *               setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       if((@src2 != 0))
 *         {
 *           @dest = divUnsigned (@src1, @src2);
 *           if((getFFlag () == true))
 *             {
 *               setZFlag (@dest);
 *               setNFlag (0);
 *               setVFlag (0);
 *             };
 *         }
 *       else
 *         {
 *         };
 *     };
 * }
 */

int
arc_gen_DIVU(DisasCtxt *ctx, TCGv src2, TCGv dest, TCGv src1)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_3, src2, 0);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    divUnsigned(temp_6, src1, src2);
    tcg_gen_mov_i32(dest, temp_6);
    if ((getFFlag () == true)) {
        setZFlag(dest);
        tcg_gen_movi_i32(temp_7, 0);
        setNFlag(temp_7);
        tcg_gen_movi_i32(temp_8, 0);
        setVFlag(temp_8);
    }
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);

    return ret;
}


/*
 * REM
 *    Variables: @src2, @src1, @dest
 *    Functions: getCCFlag, divRemainingSigned, getFFlag, setZFlag, setNFlag,
 *               setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       if(((@src2 != 0) && ((@src1 != 2147483648) || (@src2 != 4294967295))))
 *         {
 *           @dest = divRemainingSigned (@src1, @src2);
 *           if((getFFlag () == true))
 *             {
 *               setZFlag (@dest);
 *               setNFlag (@dest);
 *               setVFlag (0);
 *             };
 *         }
 *       else
 *         {
 *         };
 *     };
 * }
 */

int
arc_gen_REM(DisasCtxt *ctx, TCGv src2, TCGv src1, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    getCCFlag(temp_9);
    tcg_gen_mov_i32(cc_flag, temp_9);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_3, src2, 0);
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_4, src1, 2147483648);
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_5, src2, 4294967295);
    tcg_gen_or_i32(temp_6, temp_4, temp_5);
    tcg_gen_and_i32(temp_7, temp_3, temp_6);
    tcg_gen_xori_i32(temp_8, temp_7, 1);
    tcg_gen_andi_i32(temp_8, temp_8, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_8, arc_true, else_2);
    divRemainingSigned(temp_10, src1, src2);
    tcg_gen_mov_i32(dest, temp_10);
    if ((getFFlag () == true)) {
        setZFlag(dest);
        setNFlag(dest);
        tcg_gen_movi_i32(temp_11, 0);
        setVFlag(temp_11);
    }
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    gen_set_label(done_1);
    tcg_temp_free(temp_9);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_11);

    return ret;
}


/*
 * REMU
 *    Variables: @src2, @dest, @src1
 *    Functions: getCCFlag, divRemainingUnsigned, getFFlag, setZFlag, setNFlag,
 *               setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       if((@src2 != 0))
 *         {
 *           @dest = divRemainingUnsigned (@src1, @src2);
 *           if((getFFlag () == true))
 *             {
 *               setZFlag (@dest);
 *               setNFlag (0);
 *               setVFlag (0);
 *             };
 *         }
 *       else
 *         {
 *         };
 *     };
 * }
 */

int
arc_gen_REMU(DisasCtxt *ctx, TCGv src2, TCGv dest, TCGv src1)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_3, src2, 0);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    divRemainingUnsigned(temp_6, src1, src2);
    tcg_gen_mov_i32(dest, temp_6);
    if ((getFFlag () == true)) {
        setZFlag(dest);
        tcg_gen_movi_i32(temp_7, 0);
        setNFlag(temp_7);
        tcg_gen_movi_i32(temp_8, 0);
        setVFlag(temp_8);
    }
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);

    return ret;
}


/*
 * MAC
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getRegister, MAC, getFFlag, setNFlag, OverflowADD,
 *               setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       old_acchi = getRegister (R_ACCHI);
 *       high_mul = MAC (@b, @c);
 *       @a = getRegister (R_ACCLO);
 *       if((getFFlag () == true))
 *         {
 *           new_acchi = getRegister (R_ACCHI);
 *           setNFlag (new_acchi);
 *           if((OverflowADD (new_acchi, old_acchi, high_mul) == true))
 *             {
 *               setVFlag (1);
 *             };
 *         };
 *     };
 * }
 */

int
arc_gen_MAC(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv old_acchi = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv high_mul = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv new_acchi = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    getRegister(temp_6, R_ACCHI);
    tcg_gen_mov_i32(old_acchi, temp_6);
    MAC(temp_7, b, c);
    tcg_gen_mov_i32(high_mul, temp_7);
    getRegister(temp_8, R_ACCLO);
    tcg_gen_mov_i32(a, temp_8);
    if ((getFFlag () == true)) {
        getRegister(temp_9, R_ACCHI);
        tcg_gen_mov_i32(new_acchi, temp_9);
        setNFlag(new_acchi);
        TCGLabel *done_2 = gen_new_label();
        OverflowADD(temp_10, new_acchi, old_acchi, high_mul);
        tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, temp_10, arc_true);
        tcg_gen_xori_i32(temp_4, temp_3, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_2);
        tcg_gen_movi_i32(temp_11, 1);
        setVFlag(temp_11);
        gen_set_label(done_2);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(old_acchi);
    tcg_temp_free(temp_7);
    tcg_temp_free(high_mul);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_9);
    tcg_temp_free(new_acchi);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_11);

    return ret;
}


/*
 * MACU
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getRegister, MACU, getFFlag, CarryADD, setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       old_acchi = getRegister (R_ACCHI);
 *       high_mul = MACU (@b, @c);
 *       @a = getRegister (R_ACCLO);
 *       if((getFFlag () == true))
 *         {
 *           new_acchi = getRegister (R_ACCHI);
 *           if((CarryADD (new_acchi, old_acchi, high_mul) == true))
 *             {
 *               setVFlag (1);
 *             };
 *         };
 *     };
 * }
 */

int
arc_gen_MACU(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv old_acchi = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv high_mul = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv new_acchi = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    getRegister(temp_6, R_ACCHI);
    tcg_gen_mov_i32(old_acchi, temp_6);
    MACU(temp_7, b, c);
    tcg_gen_mov_i32(high_mul, temp_7);
    getRegister(temp_8, R_ACCLO);
    tcg_gen_mov_i32(a, temp_8);
    if ((getFFlag () == true)) {
        getRegister(temp_9, R_ACCHI);
        tcg_gen_mov_i32(new_acchi, temp_9);
        TCGLabel *done_2 = gen_new_label();
        CarryADD(temp_10, new_acchi, old_acchi, high_mul);
        tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, temp_10, arc_true);
        tcg_gen_xori_i32(temp_4, temp_3, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_2);
        tcg_gen_movi_i32(temp_11, 1);
        setVFlag(temp_11);
        gen_set_label(done_2);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(old_acchi);
    tcg_temp_free(temp_7);
    tcg_temp_free(high_mul);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_9);
    tcg_temp_free(new_acchi);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_11);

    return ret;
}


/*
 * MACD
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getRegister, MAC, nextReg, getFFlag, setNFlag,
 *               OverflowADD, setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       old_acchi = getRegister (R_ACCHI);
 *       high_mul = MAC (@b, @c);
 *       @a = getRegister (R_ACCLO);
 *       pair = nextReg (a);
 *       pair = getRegister (R_ACCHI);
 *       if((getFFlag () == true))
 *         {
 *           new_acchi = getRegister (R_ACCHI);
 *           setNFlag (new_acchi);
 *           if((OverflowADD (new_acchi, old_acchi, high_mul) == true))
 *             {
 *               setVFlag (1);
 *             };
 *         };
 *     };
 * }
 */

int
arc_gen_MACD(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv old_acchi = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv high_mul = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv pair = NULL;
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv new_acchi = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    getRegister(temp_6, R_ACCHI);
    tcg_gen_mov_i32(old_acchi, temp_6);
    MAC(temp_7, b, c);
    tcg_gen_mov_i32(high_mul, temp_7);
    getRegister(temp_8, R_ACCLO);
    tcg_gen_mov_i32(a, temp_8);
    pair = nextReg (a);
    getRegister(temp_9, R_ACCHI);
    tcg_gen_mov_i32(pair, temp_9);
    if ((getFFlag () == true)) {
        getRegister(temp_10, R_ACCHI);
        tcg_gen_mov_i32(new_acchi, temp_10);
        setNFlag(new_acchi);
        TCGLabel *done_2 = gen_new_label();
        OverflowADD(temp_11, new_acchi, old_acchi, high_mul);
        tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, temp_11, arc_true);
        tcg_gen_xori_i32(temp_4, temp_3, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_2);
        tcg_gen_movi_i32(temp_12, 1);
        setVFlag(temp_12);
        gen_set_label(done_2);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(old_acchi);
    tcg_temp_free(temp_7);
    tcg_temp_free(high_mul);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_10);
    tcg_temp_free(new_acchi);
    tcg_temp_free(temp_11);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_12);

    return ret;
}


/*
 * MACDU
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag, getRegister, MACU, nextReg, getFFlag, CarryADD,
 *               setVFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       old_acchi = getRegister (R_ACCHI);
 *       high_mul = MACU (@b, @c);
 *       @a = getRegister (R_ACCLO);
 *       pair = nextReg (a);
 *       pair = getRegister (R_ACCHI);
 *       if((getFFlag () == true))
 *         {
 *           new_acchi = getRegister (R_ACCHI);
 *           if((CarryADD (new_acchi, old_acchi, high_mul) == true))
 *             {
 *               setVFlag (1);
 *             };
 *         };
 *     };
 * }
 */

int
arc_gen_MACDU(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv old_acchi = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv high_mul = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv pair = NULL;
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv new_acchi = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    getRegister(temp_6, R_ACCHI);
    tcg_gen_mov_i32(old_acchi, temp_6);
    MACU(temp_7, b, c);
    tcg_gen_mov_i32(high_mul, temp_7);
    getRegister(temp_8, R_ACCLO);
    tcg_gen_mov_i32(a, temp_8);
    pair = nextReg (a);
    getRegister(temp_9, R_ACCHI);
    tcg_gen_mov_i32(pair, temp_9);
    if ((getFFlag () == true)) {
        getRegister(temp_10, R_ACCHI);
        tcg_gen_mov_i32(new_acchi, temp_10);
        TCGLabel *done_2 = gen_new_label();
        CarryADD(temp_11, new_acchi, old_acchi, high_mul);
        tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, temp_11, arc_true);
        tcg_gen_xori_i32(temp_4, temp_3, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_2);
        tcg_gen_movi_i32(temp_12, 1);
        setVFlag(temp_12);
        gen_set_label(done_2);
    }
    gen_set_label(done_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(old_acchi);
    tcg_temp_free(temp_7);
    tcg_temp_free(high_mul);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_10);
    tcg_temp_free(new_acchi);
    tcg_temp_free(temp_11);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_12);

    return ret;
}


/*
 * ABS
 *    Variables: @src, @dest
 *    Functions: Carry, getFFlag, setZFlag, setNFlag, setCFlag, Zero, setVFlag,
 *               getNFlag
 * --- code ---
 * {
 *   lsrc = @src;
 *   alu = (0 - lsrc);
 *   if((Carry (lsrc) == 1))
 *     {
 *       @dest = alu;
 *     }
 *   else
 *     {
 *       @dest = lsrc;
 *     };
 *   if((getFFlag () == true))
 *     {
 *       setZFlag (@dest);
 *       setNFlag (@dest);
 *       setCFlag (Zero ());
 *       setVFlag (getNFlag ());
 *     };
 * }
 */

int
arc_gen_ABS(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv lsrc = tcg_temp_local_new_i32();
    TCGv alu = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(lsrc, src);
    tcg_gen_subfi_i32(alu, 0, lsrc);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    Carry(temp_3, lsrc);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_1, temp_3, 1);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_1);
    tcg_gen_mov_i32(dest, alu);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    tcg_gen_mov_i32(dest, lsrc);
    gen_set_label(done_1);
    if ((getFFlag () == true)) {
        setZFlag(dest);
        setNFlag(dest);
        tcg_gen_mov_i32(temp_4, Zero());
        setCFlag(temp_4);
        tcg_gen_mov_i32(temp_5, getNFlag());
        setVFlag(temp_5);
    }
    tcg_temp_free(lsrc);
    tcg_temp_free(alu);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);

    return ret;
}


/*
 * SWAP
 *    Variables: @src, @dest
 *    Functions: getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   tmp1 = (@src << 16);
 *   tmp2 = ((@src >> 16) & 65535);
 *   @dest = (tmp1 | tmp2);
 *   f_flag = getFFlag ();
 *   if((f_flag == true))
 *     {
 *       setZFlag (@dest);
 *       setNFlag (@dest);
 *     };
 * }
 */

int
arc_gen_SWAP(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv tmp1 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv tmp2 = tcg_temp_local_new_i32();
    int f_flag;
    tcg_gen_shli_i32(tmp1, src, 16);
    tcg_gen_shri_i32(temp_1, src, 16);
    tcg_gen_andi_i32(tmp2, temp_1, 65535);
    tcg_gen_or_i32(dest, tmp1, tmp2);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }
    tcg_temp_free(tmp1);
    tcg_temp_free(temp_1);
    tcg_temp_free(tmp2);

    return ret;
}


/*
 * SWAPE
 *    Variables: @src, @dest
 *    Functions: getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   tmp1 = ((@src << 24) & 4278190080);
 *   tmp2 = ((@src << 8) & 16711680);
 *   tmp3 = ((@src >> 8) & 65280);
 *   tmp4 = ((@src >> 24) & 255);
 *   @dest = (((tmp1 | tmp2) | tmp3) | tmp4);
 *   f_flag = getFFlag ();
 *   if((f_flag == true))
 *     {
 *       setZFlag (@dest);
 *       setNFlag (@dest);
 *     };
 * }
 */

int
arc_gen_SWAPE(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv tmp1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv tmp2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv tmp3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv tmp4 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    int f_flag;
    tcg_gen_shli_i32(temp_1, src, 24);
    tcg_gen_andi_i32(tmp1, temp_1, 4278190080);
    tcg_gen_shli_i32(temp_2, src, 8);
    tcg_gen_andi_i32(tmp2, temp_2, 16711680);
    tcg_gen_shri_i32(temp_3, src, 8);
    tcg_gen_andi_i32(tmp3, temp_3, 65280);
    tcg_gen_shri_i32(temp_4, src, 24);
    tcg_gen_andi_i32(tmp4, temp_4, 255);
    tcg_gen_or_i32(temp_6, tmp1, tmp2);
    tcg_gen_or_i32(temp_5, temp_6, tmp3);
    tcg_gen_or_i32(dest, temp_5, tmp4);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }
    tcg_temp_free(temp_1);
    tcg_temp_free(tmp1);
    tcg_temp_free(temp_2);
    tcg_temp_free(tmp2);
    tcg_temp_free(temp_3);
    tcg_temp_free(tmp3);
    tcg_temp_free(temp_4);
    tcg_temp_free(tmp4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);

    return ret;
}


/*
 * NOT
 *    Variables: @dest, @src
 *    Functions: getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   @dest = ~@src;
 *   f_flag = getFFlag ();
 *   if((f_flag == true))
 *     {
 *       setZFlag (@dest);
 *       setNFlag (@dest);
 *     };
 * }
 */

int
arc_gen_NOT(DisasCtxt *ctx, TCGv dest, TCGv src)
{
    int ret = DISAS_NEXT;
    int f_flag;
    tcg_gen_not_i32(dest, src);
    f_flag = getFFlag ();
    if ((f_flag == true)) {
        setZFlag(dest);
        setNFlag(dest);
    }

    return ret;
}


/*
 * BI
 *    Variables: @c
 *    Functions: setPC, getPCL
 * --- code ---
 * {
 *   setPC ((nextInsnAddress () + (@c << 2)));
 * }
 */

int
arc_gen_BI(DisasCtxt *ctx, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    tcg_gen_shli_i32(temp_4, c, 2);
    nextInsnAddress(temp_3);
    tcg_gen_mov_i32(temp_2, temp_3);
    tcg_gen_add_i32(temp_1, temp_2, temp_4);
    setPC(temp_1);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_1);

    return ret;
}


/*
 * BIH
 *    Variables: @c
 *    Functions: setPC, getPCL
 * --- code ---
 * {
 *   setPC ((nextInsnAddress () + (@c << 1)));
 * }
 */

int
arc_gen_BIH(DisasCtxt *ctx, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    tcg_gen_shli_i32(temp_4, c, 1);
    nextInsnAddress(temp_3);
    tcg_gen_mov_i32(temp_2, temp_3);
    tcg_gen_add_i32(temp_1, temp_2, temp_4);
    setPC(temp_1);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_1);

    return ret;
}


/*
 * B
 *    Variables: @rd
 *    Functions: getCCFlag, getPCL, shouldExecuteDelaySlot, executeDelaySlot,
 *               setPC
 * --- code ---
 * {
 *   take_branch = false;
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       take_branch = true;
 *     };
 *   bta = (getPCL () + @rd);
 *   if((shouldExecuteDelaySlot () == true))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if((cc_flag == true))
 *     {
 *       setPC (bta);
 *     };
 * }
 */

int
arc_gen_B(DisasCtxt *ctx, TCGv rd)
{
    int ret = DISAS_NEXT;
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(take_branch, arc_false);
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    gen_set_label(done_1);
    getPCL(temp_7);
    tcg_gen_mov_i32(temp_6, temp_7);
    tcg_gen_add_i32(bta, temp_6, rd);
    if ((shouldExecuteDelaySlot () == true)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_2);
    setPC(bta);
    gen_set_label(done_2);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);
    tcg_temp_free(bta);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * B_S
 *    Variables: @rd
 *    Functions: getCCFlag, killDelaySlot, setPC, getPCL
 * --- code ---
 * {
 *   take_branch = false;
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *     };
 *   if((cc_flag == true))
 *     {
 *       killDelaySlot ();
 *       setPC ((getPCL () + @rd));
 *     };
 * }
 */

int
arc_gen_B_S(DisasCtxt *ctx, TCGv rd)
{
    int ret = DISAS_NEXT;
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(take_branch, arc_false);
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    gen_set_label(done_1);
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_2);
    killDelaySlot();
    getPCL(temp_8);
    tcg_gen_mov_i32(temp_7, temp_8);
    tcg_gen_add_i32(temp_6, temp_7, rd);
    setPC(temp_6);
    gen_set_label(done_2);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * BBIT0
 *    Variables: @b, @c, @rd
 *    Functions: getCCFlag, getPCL, shouldExecuteDelaySlot, executeDelaySlot,
 *               setPC
 * --- code ---
 * {
 *   take_branch = false;
 *   cc_flag = getCCFlag ();
 *   p_b = @b;
 *   p_c = (@c & 31);
 *   tmp = (1 << p_c);
 *   if((cc_flag == true))
 *     {
 *       if(((p_b && tmp) == 0))
 *         {
 *           take_branch = true;
 *         };
 *     };
 *   bta = (getPCL () + @rd);
 *   if((shouldExecuteDelaySlot () == true))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if((cc_flag == true))
 *     {
 *       if(((p_b && tmp) == 0))
 *         {
 *           setPC (bta);
 *         };
 *     };
 * }
 */

int
arc_gen_BBIT0(DisasCtxt *ctx, TCGv b, TCGv c, TCGv rd)
{
    int ret = DISAS_NEXT;
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv tmp = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_13 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(take_branch, arc_false);
    getCCFlag(temp_11);
    tcg_gen_mov_i32(cc_flag, temp_11);
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_andi_i32(p_c, c, 31);
    tcg_gen_shlfi_i32(tmp, 1, p_c);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_and_i32(temp_3, p_b, tmp);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_4, temp_3, 0);
    tcg_gen_xori_i32(temp_5, temp_4, 1);
    tcg_gen_andi_i32(temp_5, temp_5, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_5, arc_true, done_2);
    tcg_gen_mov_i32(take_branch, arc_true);
    gen_set_label(done_2);
    gen_set_label(done_1);
    getPCL(temp_13);
    tcg_gen_mov_i32(temp_12, temp_13);
    tcg_gen_add_i32(bta, temp_12, rd);
    if ((shouldExecuteDelaySlot () == true)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *done_3 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_6, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_7, temp_6, 1);
    tcg_gen_andi_i32(temp_7, temp_7, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_7, arc_true, done_3);
    TCGLabel *done_4 = gen_new_label();
    tcg_gen_and_i32(temp_8, p_b, tmp);
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_9, temp_8, 0);
    tcg_gen_xori_i32(temp_10, temp_9, 1);
    tcg_gen_andi_i32(temp_10, temp_10, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_10, arc_true, done_4);
    setPC(bta);
    gen_set_label(done_4);
    gen_set_label(done_3);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_11);
    tcg_temp_free(cc_flag);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(tmp);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_13);
    tcg_temp_free(temp_12);
    tcg_temp_free(bta);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_10);

    return ret;
}


/*
 * BBIT1
 *    Variables: @b, @c, @rd
 *    Functions: getCCFlag, getPCL, shouldExecuteDelaySlot, executeDelaySlot,
 *               setPC
 * --- code ---
 * {
 *   take_branch = false;
 *   cc_flag = getCCFlag ();
 *   p_b = @b;
 *   p_c = (@c & 31);
 *   tmp = (1 << p_c);
 *   if((cc_flag == true))
 *     {
 *       if(((p_b && tmp) != 0))
 *         {
 *           take_branch = true;
 *         };
 *     };
 *   bta = (getPCL () + @rd);
 *   if((shouldExecuteDelaySlot () == true))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if((cc_flag == true))
 *     {
 *       if(((p_b && tmp) != 0))
 *         {
 *           setPC (bta);
 *         };
 *     };
 * }
 */

int
arc_gen_BBIT1(DisasCtxt *ctx, TCGv b, TCGv c, TCGv rd)
{
    int ret = DISAS_NEXT;
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv tmp = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_13 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(take_branch, arc_false);
    getCCFlag(temp_11);
    tcg_gen_mov_i32(cc_flag, temp_11);
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_andi_i32(p_c, c, 31);
    tcg_gen_shlfi_i32(tmp, 1, p_c);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_and_i32(temp_3, p_b, tmp);
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_4, temp_3, 0);
    tcg_gen_xori_i32(temp_5, temp_4, 1);
    tcg_gen_andi_i32(temp_5, temp_5, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_5, arc_true, done_2);
    tcg_gen_mov_i32(take_branch, arc_true);
    gen_set_label(done_2);
    gen_set_label(done_1);
    getPCL(temp_13);
    tcg_gen_mov_i32(temp_12, temp_13);
    tcg_gen_add_i32(bta, temp_12, rd);
    if ((shouldExecuteDelaySlot () == true)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *done_3 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_6, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_7, temp_6, 1);
    tcg_gen_andi_i32(temp_7, temp_7, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_7, arc_true, done_3);
    TCGLabel *done_4 = gen_new_label();
    tcg_gen_and_i32(temp_8, p_b, tmp);
    tcg_gen_setcondi_i32(TCG_COND_NE, temp_9, temp_8, 0);
    tcg_gen_xori_i32(temp_10, temp_9, 1);
    tcg_gen_andi_i32(temp_10, temp_10, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_10, arc_true, done_4);
    setPC(bta);
    gen_set_label(done_4);
    gen_set_label(done_3);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_11);
    tcg_temp_free(cc_flag);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(tmp);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_13);
    tcg_temp_free(temp_12);
    tcg_temp_free(bta);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_10);

    return ret;
}


/*
 * BL
 *    Variables: @rd
 *    Functions: getCCFlag, getPCL, shouldExecuteDelaySlot, setBLINK,
 *               nextInsnAddressAfterDelaySlot, executeDelaySlot,
 *               nextInsnAddress, setPC
 * --- code ---
 * {
 *   take_branch = false;
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       take_branch = true;
 *     };
 *   bta = (getPCL () + @rd);
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       if(take_branch)
 *         {
 *           setBLINK (nextInsnAddressAfterDelaySlot ());
 *         };
 *       executeDelaySlot (bta, take_branch);
 *     }
 *   else
 *     {
 *       if(take_branch)
 *         {
 *           setBLINK (nextInsnAddress ());
 *         };
 *     };
 *   if((cc_flag == true))
 *     {
 *       setPC (bta);
 *     };
 * }
 */

int
arc_gen_BL(DisasCtxt *ctx, TCGv rd)
{
    int ret = DISAS_NEXT;
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_13 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(take_branch, arc_false);
    getCCFlag(temp_7);
    tcg_gen_mov_i32(cc_flag, temp_7);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    gen_set_label(done_1);
    getPCL(temp_9);
    tcg_gen_mov_i32(temp_8, temp_9);
    tcg_gen_add_i32(bta, temp_8, rd);
    if ((shouldExecuteDelaySlot () == 1)) {
        TCGLabel *done_2 = gen_new_label();
        tcg_gen_xori_i32(temp_3, take_branch, 1);
        tcg_gen_andi_i32(temp_3, temp_3, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_3, arc_true, done_2);
        nextInsnAddressAfterDelaySlot(temp_11);
        tcg_gen_mov_i32(temp_10, temp_11);
        setBLINK(temp_10);
        gen_set_label(done_2);
        executeDelaySlot(bta, take_branch);
    } else {
        TCGLabel *done_3 = gen_new_label();
        tcg_gen_xori_i32(temp_4, take_branch, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_3);
        nextInsnAddress(temp_13);
        tcg_gen_mov_i32(temp_12, temp_13);
        setBLINK(temp_12);
        gen_set_label(done_3);
    }

    TCGLabel *done_4 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_5, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, done_4);
    setPC(bta);
    gen_set_label(done_4);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_7);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(bta);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_11);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_13);
    tcg_temp_free(temp_12);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * J
 *    Variables: @src
 *    Functions: getCCFlag, shouldExecuteDelaySlot, executeDelaySlot, setPC
 * --- code ---
 * {
 *   take_branch = false;
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       take_branch = true;
 *     };
 *   bta = @src;
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if((cc_flag == true))
 *     {
 *       setPC (bta);
 *     };
 * }
 */

int
arc_gen_J(DisasCtxt *ctx, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(take_branch, arc_false);
    getCCFlag(temp_5);
    tcg_gen_mov_i32(cc_flag, temp_5);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    gen_set_label(done_1);
    tcg_gen_mov_i32(bta, src);
    if ((shouldExecuteDelaySlot () == 1)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_2);
    setPC(bta);
    gen_set_label(done_2);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_5);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(bta);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * JL
 *    Variables: @src
 *    Functions: getCCFlag, shouldExecuteDelaySlot, setBLINK,
 *               nextInsnAddressAfterDelaySlot, executeDelaySlot,
 *               nextInsnAddress, setPC
 * --- code ---
 * {
 *   take_branch = false;
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       take_branch = true;
 *     };
 *   bta = @src;
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       if(take_branch)
 *         {
 *           setBLINK (nextInsnAddressAfterDelaySlot ());
 *         };
 *       executeDelaySlot (bta, take_branch);
 *     }
 *   else
 *     {
 *       if(take_branch)
 *         {
 *           setBLINK (nextInsnAddress ());
 *         };
 *     };
 *   if((cc_flag == true))
 *     {
 *       setPC (bta);
 *     };
 * }
 */

int
arc_gen_JL(DisasCtxt *ctx, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(take_branch, arc_false);
    getCCFlag(temp_7);
    tcg_gen_mov_i32(cc_flag, temp_7);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    gen_set_label(done_1);
    tcg_gen_mov_i32(bta, src);
    if ((shouldExecuteDelaySlot () == 1)) {
        TCGLabel *done_2 = gen_new_label();
        tcg_gen_xori_i32(temp_3, take_branch, 1);
        tcg_gen_andi_i32(temp_3, temp_3, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_3, arc_true, done_2);
        nextInsnAddressAfterDelaySlot(temp_9);
        tcg_gen_mov_i32(temp_8, temp_9);
        setBLINK(temp_8);
        gen_set_label(done_2);
        executeDelaySlot(bta, take_branch);
    } else {
        TCGLabel *done_3 = gen_new_label();
        tcg_gen_xori_i32(temp_4, take_branch, 1);
        tcg_gen_andi_i32(temp_4, temp_4, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, done_3);
        nextInsnAddress(temp_11);
        tcg_gen_mov_i32(temp_10, temp_11);
        setBLINK(temp_10);
        gen_set_label(done_3);
    }

    TCGLabel *done_4 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_5, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, done_4);
    setPC(bta);
    gen_set_label(done_4);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_7);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(bta);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_11);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * SETEQ
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       p_b = @b;
 *       p_c = @c;
 *       take_branch = false;
 *       if((p_b == p_c))
 *         {
 *         }
 *       else
 *         {
 *         };
 *       if((p_b == p_c))
 *         {
 *           @a = true;
 *         }
 *       else
 *         {
 *           @a = false;
 *         };
 *     };
 * }
 */

int
arc_gen_SETEQ(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_7);
    tcg_gen_mov_i32(cc_flag, temp_7);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    TCGLabel *else_3 = gen_new_label();
    TCGLabel *done_3 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_5, p_b, p_c);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, else_3);
    tcg_gen_mov_i32(a, arc_true);
    tcg_gen_br(done_3);
    gen_set_label(else_3);
    tcg_gen_mov_i32(a, arc_false);
    gen_set_label(done_3);
    gen_set_label(done_1);
    tcg_temp_free(temp_7);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * BREQ
 *    Variables: @b, @c, @offset
 *    Functions: getPCL, shouldExecuteDelaySlot, executeDelaySlot, setPC
 * --- code ---
 * {
 *   p_b = @b;
 *   p_c = @c;
 *   take_branch = false;
 *   if((p_b == p_c))
 *     {
 *       take_branch = true;
 *     }
 *   else
 *     {
 *     };
 *   bta = (getPCL () + @offset);
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if((p_b == p_c))
 *     {
 *       setPC (bta);
 *     }
 *   else
 *     {
 *     };
 * }
 */

int
arc_gen_BREQ(DisasCtxt *ctx, TCGv b, TCGv c, TCGv offset)
{
    int ret = DISAS_NEXT;
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, p_b, p_c);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    gen_set_label(done_1);
    getPCL(temp_6);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_add_i32(bta, temp_5, offset);
    if ((shouldExecuteDelaySlot () == 1)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    setPC(bta);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(bta);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * SETNE
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       p_b = @b;
 *       p_c = @c;
 *       take_branch = false;
 *       if((p_b != p_c))
 *         {
 *         }
 *       else
 *         {
 *         };
 *       if((p_b != p_c))
 *         {
 *           @a = true;
 *         }
 *       else
 *         {
 *           @a = false;
 *         };
 *     };
 * }
 */

int
arc_gen_SETNE(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_7);
    tcg_gen_mov_i32(cc_flag, temp_7);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_NE, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    TCGLabel *else_3 = gen_new_label();
    TCGLabel *done_3 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_NE, temp_5, p_b, p_c);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, else_3);
    tcg_gen_mov_i32(a, arc_true);
    tcg_gen_br(done_3);
    gen_set_label(else_3);
    tcg_gen_mov_i32(a, arc_false);
    gen_set_label(done_3);
    gen_set_label(done_1);
    tcg_temp_free(temp_7);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * BRNE
 *    Variables: @b, @c, @offset
 *    Functions: getPCL, shouldExecuteDelaySlot, executeDelaySlot, setPC
 * --- code ---
 * {
 *   p_b = @b;
 *   p_c = @c;
 *   take_branch = false;
 *   if((p_b != p_c))
 *     {
 *       take_branch = true;
 *     }
 *   else
 *     {
 *     };
 *   bta = (getPCL () + @offset);
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if((p_b != p_c))
 *     {
 *       setPC (bta);
 *     }
 *   else
 *     {
 *     };
 * }
 */

int
arc_gen_BRNE(DisasCtxt *ctx, TCGv b, TCGv c, TCGv offset)
{
    int ret = DISAS_NEXT;
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_NE, temp_1, p_b, p_c);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    gen_set_label(done_1);
    getPCL(temp_6);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_add_i32(bta, temp_5, offset);
    if ((shouldExecuteDelaySlot () == 1)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_NE, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    setPC(bta);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(bta);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * SETLT
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       p_b = @b;
 *       p_c = @c;
 *       take_branch = false;
 *       if((p_b < p_c))
 *         {
 *         }
 *       else
 *         {
 *         };
 *       if((p_b < p_c))
 *         {
 *           @a = true;
 *         }
 *       else
 *         {
 *           @a = false;
 *         };
 *     };
 * }
 */

int
arc_gen_SETLT(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_7);
    tcg_gen_mov_i32(cc_flag, temp_7);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_LT, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    TCGLabel *else_3 = gen_new_label();
    TCGLabel *done_3 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_LT, temp_5, p_b, p_c);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, else_3);
    tcg_gen_mov_i32(a, arc_true);
    tcg_gen_br(done_3);
    gen_set_label(else_3);
    tcg_gen_mov_i32(a, arc_false);
    gen_set_label(done_3);
    gen_set_label(done_1);
    tcg_temp_free(temp_7);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * BRLT
 *    Variables: @b, @c, @offset
 *    Functions: getPCL, shouldExecuteDelaySlot, executeDelaySlot, setPC
 * --- code ---
 * {
 *   p_b = @b;
 *   p_c = @c;
 *   take_branch = false;
 *   if((p_b < p_c))
 *     {
 *       take_branch = true;
 *     }
 *   else
 *     {
 *     };
 *   bta = (getPCL () + @offset);
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if((p_b < p_c))
 *     {
 *       setPC (bta);
 *     }
 *   else
 *     {
 *     };
 * }
 */

int
arc_gen_BRLT(DisasCtxt *ctx, TCGv b, TCGv c, TCGv offset)
{
    int ret = DISAS_NEXT;
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_LT, temp_1, p_b, p_c);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    gen_set_label(done_1);
    getPCL(temp_6);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_add_i32(bta, temp_5, offset);
    if ((shouldExecuteDelaySlot () == 1)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_LT, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    setPC(bta);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(bta);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * SETGE
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       p_b = @b;
 *       p_c = @c;
 *       take_branch = false;
 *       if((p_b >= p_c))
 *         {
 *         }
 *       else
 *         {
 *         };
 *       if((p_b >= p_c))
 *         {
 *           @a = true;
 *         }
 *       else
 *         {
 *           @a = false;
 *         };
 *     };
 * }
 */

int
arc_gen_SETGE(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_7);
    tcg_gen_mov_i32(cc_flag, temp_7);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_GE, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    TCGLabel *else_3 = gen_new_label();
    TCGLabel *done_3 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_GE, temp_5, p_b, p_c);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, else_3);
    tcg_gen_mov_i32(a, arc_true);
    tcg_gen_br(done_3);
    gen_set_label(else_3);
    tcg_gen_mov_i32(a, arc_false);
    gen_set_label(done_3);
    gen_set_label(done_1);
    tcg_temp_free(temp_7);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * BRGE
 *    Variables: @b, @c, @offset
 *    Functions: getPCL, shouldExecuteDelaySlot, executeDelaySlot, setPC
 * --- code ---
 * {
 *   p_b = @b;
 *   p_c = @c;
 *   take_branch = false;
 *   if((p_b >= p_c))
 *     {
 *       take_branch = true;
 *     }
 *   else
 *     {
 *     };
 *   bta = (getPCL () + @offset);
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if((p_b >= p_c))
 *     {
 *       setPC (bta);
 *     }
 *   else
 *     {
 *     };
 * }
 */

int
arc_gen_BRGE(DisasCtxt *ctx, TCGv b, TCGv c, TCGv offset)
{
    int ret = DISAS_NEXT;
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_GE, temp_1, p_b, p_c);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    gen_set_label(done_1);
    getPCL(temp_6);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_add_i32(bta, temp_5, offset);
    if ((shouldExecuteDelaySlot () == 1)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_GE, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    setPC(bta);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(bta);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * SETLE
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       p_b = @b;
 *       p_c = @c;
 *       take_branch = false;
 *       if((p_b <= p_c))
 *         {
 *         }
 *       else
 *         {
 *         };
 *       if((p_b <= p_c))
 *         {
 *           @a = true;
 *         }
 *       else
 *         {
 *           @a = false;
 *         };
 *     };
 * }
 */

int
arc_gen_SETLE(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_7);
    tcg_gen_mov_i32(cc_flag, temp_7);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_LE, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    TCGLabel *else_3 = gen_new_label();
    TCGLabel *done_3 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_LE, temp_5, p_b, p_c);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, else_3);
    tcg_gen_mov_i32(a, arc_true);
    tcg_gen_br(done_3);
    gen_set_label(else_3);
    tcg_gen_mov_i32(a, arc_false);
    gen_set_label(done_3);
    gen_set_label(done_1);
    tcg_temp_free(temp_7);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * SETGT
 *    Variables: @b, @c, @a
 *    Functions: getCCFlag
 * --- code ---
 * {
 *   cc_flag = getCCFlag ();
 *   if((cc_flag == true))
 *     {
 *       p_b = @b;
 *       p_c = @c;
 *       take_branch = false;
 *       if((p_b > p_c))
 *         {
 *         }
 *       else
 *         {
 *         };
 *       if((p_b > p_c))
 *         {
 *           @a = true;
 *         }
 *       else
 *         {
 *           @a = false;
 *         };
 *     };
 * }
 */

int
arc_gen_SETGT(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv cc_flag = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getCCFlag(temp_7);
    tcg_gen_mov_i32(cc_flag, temp_7);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, cc_flag, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_GT, temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_4, temp_3, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_4, arc_true, else_2);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    TCGLabel *else_3 = gen_new_label();
    TCGLabel *done_3 = gen_new_label();
    tcg_gen_setcond_i32(TCG_COND_GT, temp_5, p_b, p_c);
    tcg_gen_xori_i32(temp_6, temp_5, 1);
    tcg_gen_andi_i32(temp_6, temp_6, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_6, arc_true, else_3);
    tcg_gen_mov_i32(a, arc_true);
    tcg_gen_br(done_3);
    gen_set_label(else_3);
    tcg_gen_mov_i32(a, arc_false);
    gen_set_label(done_3);
    gen_set_label(done_1);
    tcg_temp_free(temp_7);
    tcg_temp_free(cc_flag);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * BRLO
 *    Variables: @b, @c, @offset
 *    Functions: unsignedLT, getPCL, shouldExecuteDelaySlot, executeDelaySlot,
 *               setPC
 * --- code ---
 * {
 *   p_b = @b;
 *   p_c = @c;
 *   take_branch = false;
 *   if(unsignedLT (p_b, p_c))
 *     {
 *       take_branch = true;
 *     }
 *   else
 *     {
 *     };
 *   bta = (getPCL () + @offset);
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if(unsignedLT (p_b, p_c))
 *     {
 *       setPC (bta);
 *     }
 *   else
 *     {
 *     };
 * }
 */

int
arc_gen_BRLO(DisasCtxt *ctx, TCGv b, TCGv c, TCGv offset)
{
    int ret = DISAS_NEXT;
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    unsignedLT(temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_1, temp_3, 1);
    tcg_gen_andi_i32(temp_1, temp_1, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_1, arc_true, else_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    gen_set_label(done_1);
    getPCL(temp_5);
    tcg_gen_mov_i32(temp_4, temp_5);
    tcg_gen_add_i32(bta, temp_4, offset);
    if ((shouldExecuteDelaySlot () == 1)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    unsignedLT(temp_6, p_b, p_c);
    tcg_gen_xori_i32(temp_2, temp_6, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_2);
    setPC(bta);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(bta);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_2);

    return ret;
}


/*
 * SETLO
 *    Variables: @b, @c, @a
 *    Functions: unsignedLT
 * --- code ---
 * {
 *   p_b = @b;
 *   p_c = @c;
 *   take_branch = false;
 *   if(unsignedLT (p_b, p_c))
 *     {
 *     }
 *   else
 *     {
 *     };
 *   if(unsignedLT (p_b, p_c))
 *     {
 *       @a = true;
 *     }
 *   else
 *     {
 *       @a = false;
 *     };
 * }
 */

int
arc_gen_SETLO(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    unsignedLT(temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_1, temp_3, 1);
    tcg_gen_andi_i32(temp_1, temp_1, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_1, arc_true, else_1);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    gen_set_label(done_1);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    unsignedLT(temp_4, p_b, p_c);
    tcg_gen_xori_i32(temp_2, temp_4, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_2);
    tcg_gen_mov_i32(a, arc_true);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    tcg_gen_mov_i32(a, arc_false);
    gen_set_label(done_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_2);

    return ret;
}


/*
 * BRHS
 *    Variables: @b, @c, @offset
 *    Functions: unsignedGE, getPCL, shouldExecuteDelaySlot, executeDelaySlot,
 *               setPC
 * --- code ---
 * {
 *   p_b = @b;
 *   p_c = @c;
 *   take_branch = false;
 *   if(unsignedGE (p_b, p_c))
 *     {
 *       take_branch = true;
 *     }
 *   else
 *     {
 *     };
 *   bta = (getPCL () + @offset);
 *   if((shouldExecuteDelaySlot () == 1))
 *     {
 *       executeDelaySlot (bta, take_branch);
 *     };
 *   if(unsignedGE (p_b, p_c))
 *     {
 *       setPC (bta);
 *     }
 *   else
 *     {
 *     };
 * }
 */

int
arc_gen_BRHS(DisasCtxt *ctx, TCGv b, TCGv c, TCGv offset)
{
    int ret = DISAS_NEXT;
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv bta = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    unsignedGE(temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_1, temp_3, 1);
    tcg_gen_andi_i32(temp_1, temp_1, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_1, arc_true, else_1);
    tcg_gen_mov_i32(take_branch, arc_true);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    gen_set_label(done_1);
    getPCL(temp_5);
    tcg_gen_mov_i32(temp_4, temp_5);
    tcg_gen_add_i32(bta, temp_4, offset);
    if ((shouldExecuteDelaySlot () == 1)) {
        executeDelaySlot(bta, take_branch);
    }
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    unsignedGE(temp_6, p_b, p_c);
    tcg_gen_xori_i32(temp_2, temp_6, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_2);
    setPC(bta);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    gen_set_label(done_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);
    tcg_temp_free(bta);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_2);

    return ret;
}


/*
 * SETHS
 *    Variables: @b, @c, @a
 *    Functions: unsignedGE
 * --- code ---
 * {
 *   p_b = @b;
 *   p_c = @c;
 *   take_branch = false;
 *   if(unsignedGE (p_b, p_c))
 *     {
 *     }
 *   else
 *     {
 *     };
 *   if(unsignedGE (p_b, p_c))
 *     {
 *       @a = true;
 *     }
 *   else
 *     {
 *       @a = false;
 *     };
 * }
 */

int
arc_gen_SETHS(DisasCtxt *ctx, TCGv b, TCGv c, TCGv a)
{
    int ret = DISAS_NEXT;
    TCGv p_b = tcg_temp_local_new_i32();
    TCGv p_c = tcg_temp_local_new_i32();
    TCGv take_branch = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(p_b, b);
    tcg_gen_mov_i32(p_c, c);
    tcg_gen_mov_i32(take_branch, arc_false);
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    unsignedGE(temp_3, p_b, p_c);
    tcg_gen_xori_i32(temp_1, temp_3, 1);
    tcg_gen_andi_i32(temp_1, temp_1, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_1, arc_true, else_1);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    gen_set_label(done_1);
    TCGLabel *else_2 = gen_new_label();
    TCGLabel *done_2 = gen_new_label();
    unsignedGE(temp_4, p_b, p_c);
    tcg_gen_xori_i32(temp_2, temp_4, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_2);
    tcg_gen_mov_i32(a, arc_true);
    tcg_gen_br(done_2);
    gen_set_label(else_2);
    tcg_gen_mov_i32(a, arc_false);
    gen_set_label(done_2);
    tcg_temp_free(p_b);
    tcg_temp_free(p_c);
    tcg_temp_free(take_branch);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_2);

    return ret;
}


/*
 * EX
 *    Variables: @b, @c
 *    Functions: getMemory, setMemory
 * --- code ---
 * {
 *   temp = @b;
 *   @b = getMemory (@c, LONG);
 *   setMemory (@c, LONG, temp);
 * }
 */

int
arc_gen_EX(DisasCtxt *ctx, TCGv b, TCGv c)
{
    int ret = DISAS_NEXT;
    TCGv temp = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(temp, b);
    getMemory(temp_1, c, LONG);
    tcg_gen_mov_i32(b, temp_1);
    setMemory(c, LONG, temp);
    tcg_temp_free(temp);
    tcg_temp_free(temp_1);

    return ret;
}


/*
 * LLOCK
 *    Variables: @dest, @src
 *    Functions: getMemory, setLF
 * --- code ---
 * {
 *   @dest = getMemory (@src, LONG);
 *   setLF (1);
 * }
 */

int
arc_gen_LLOCK(DisasCtxt *ctx, TCGv dest, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    getMemory(temp_1, src, LONG);
    tcg_gen_mov_i32(dest, temp_1);
    tcg_gen_movi_i32(temp_2, 1);
    setLF(temp_2);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);

    return ret;
}


/*
 * LLOCKD
 *    Variables: @dest, @src
 *    Functions: getMemory, nextReg, setLF
 * --- code ---
 * {
 *   @dest = getMemory (@src, LONG);
 *   pair = nextReg (dest);
 *   pair = getMemory ((@src + 4), LONG);
 *   setLF (1);
 * }
 */

int
arc_gen_LLOCKD(DisasCtxt *ctx, TCGv dest, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv pair = NULL;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    getMemory(temp_1, src, LONG);
    tcg_gen_mov_i32(dest, temp_1);
    pair = nextReg (dest);
    tcg_gen_addi_i32(temp_3, src, 4);
    getMemory(temp_2, temp_3, LONG);
    tcg_gen_mov_i32(pair, temp_2);
    tcg_gen_movi_i32(temp_4, 1);
    setLF(temp_4);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * SCOND
 *    Variables: @src, @dest
 *    Functions: getLF, setMemory, setZFlag, setLF
 * --- code ---
 * {
 *   lf = getLF ();
 *   if((lf == 1))
 *     {
 *       setMemory (@src, LONG, @dest);
 *     };
 *   setZFlag (!lf);
 *   setLF (0);
 * }
 */

int
arc_gen_SCOND(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv lf = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    getLF(temp_3);
    tcg_gen_mov_i32(lf, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_1, lf, 1);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    setMemory(src, LONG, dest);
    gen_set_label(done_1);
    tcg_gen_xori_i32(temp_4, lf, 1);
    tcg_gen_andi_i32(temp_4, temp_4, 1);
    setZFlag(temp_4);
    tcg_gen_movi_i32(temp_5, 0);
    setLF(temp_5);
    tcg_temp_free(temp_3);
    tcg_temp_free(lf);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);

    return ret;
}


/*
 * SCONDD
 *    Variables: @src, @dest
 *    Functions: getLF, setMemory, nextReg, setZFlag, setLF
 * --- code ---
 * {
 *   lf = getLF ();
 *   if((lf == 1))
 *     {
 *       setMemory (@src, LONG, @dest);
 *       pair = nextReg (dest);
 *       setMemory ((@src + 4), LONG, pair);
 *     };
 *   setZFlag (!lf);
 *   setLF (0);
 * }
 */

int
arc_gen_SCONDD(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv lf = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv pair = NULL;
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    getLF(temp_3);
    tcg_gen_mov_i32(lf, temp_3);
    TCGLabel *done_1 = gen_new_label();
    tcg_gen_setcondi_i32(TCG_COND_EQ, temp_1, lf, 1);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, done_1);
    setMemory(src, LONG, dest);
    pair = nextReg (dest);
    tcg_gen_addi_i32(temp_4, src, 4);
    setMemory(temp_4, LONG, pair);
    gen_set_label(done_1);
    tcg_gen_xori_i32(temp_5, lf, 1);
    tcg_gen_andi_i32(temp_5, temp_5, 1);
    setZFlag(temp_5);
    tcg_gen_movi_i32(temp_6, 0);
    setLF(temp_6);
    tcg_temp_free(temp_3);
    tcg_temp_free(lf);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_6);

    return ret;
}


/*
 * DMB
 *    Variables: @a
 *    Functions:
 * --- code ---
 * {
 *   @a = @a;
 * }
 */

int
arc_gen_DMB(DisasCtxt *ctx, TCGv a)
{
    int ret = DISAS_NEXT;

    return ret;
}


/*
 * LD
 *    Variables: @src1, @src2, @dest
 *    Functions: getAAFlag, getZZFlag, setDebugLD, getMemory, getFlagX,
 *               SignExtend, NoFurtherLoadsPending
 * --- code ---
 * {
 *   AA = getAAFlag ();
 *   ZZ = getZZFlag ();
 *   address = 0;
 *   if(((AA == 0) || (AA == 1)))
 *     {
 *       address = (@src1 + @src2);
 *     };
 *   if((AA == 2))
 *     {
 *       address = @src1;
 *     };
 *   if(((AA == 3) && ((ZZ == 0) || (ZZ == 3))))
 *     {
 *       address = (@src1 + (@src2 << 2));
 *     };
 *   if(((AA == 3) && (ZZ == 2)))
 *     {
 *       address = (@src1 + (@src2 << 1));
 *     };
 *   l_src1 = @src1;
 *   l_src2 = @src2;
 *   setDebugLD (1);
 *   new_dest = getMemory (address, ZZ);
 *   if(((AA == 1) || (AA == 2)))
 *     {
 *       @src1 = (l_src1 + l_src2);
 *     };
 *   if((getFlagX () == 1))
 *     {
 *       new_dest = SignExtend (new_dest, ZZ);
 *     };
 *   if(NoFurtherLoadsPending ())
 *     {
 *       setDebugLD (0);
 *     };
 *   @dest = new_dest;
 * }
 */

int
arc_gen_LD(DisasCtxt *ctx, TCGv src1, TCGv src2, TCGv dest)
{
    int ret = DISAS_NEXT;
    int AA;
    int ZZ;
    TCGv address = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv l_src1 = tcg_temp_local_new_i32();
    TCGv l_src2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv new_dest = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    AA = getAAFlag ();
    ZZ = getZZFlag ();
    tcg_gen_movi_i32(address, 0);
    if (((AA == 0) || (AA == 1))) {
        tcg_gen_add_i32(address, src1, src2);
    }
    if ((AA == 2)) {
        tcg_gen_mov_i32(address, src1);
    }
    if (((AA == 3) && ((ZZ == 0) || (ZZ == 3)))) {
        tcg_gen_shli_i32(temp_2, src2, 2);
        tcg_gen_add_i32(address, src1, temp_2);
    }
    if (((AA == 3) && (ZZ == 2))) {
        tcg_gen_shli_i32(temp_3, src2, 1);
        tcg_gen_add_i32(address, src1, temp_3);
    }
    tcg_gen_mov_i32(l_src1, src1);
    tcg_gen_mov_i32(l_src2, src2);
    tcg_gen_movi_i32(temp_4, 1);
    setDebugLD(temp_4);
    getMemory(temp_5, address, ZZ);
    tcg_gen_mov_i32(new_dest, temp_5);
    if (((AA == 1) || (AA == 2))) {
        tcg_gen_add_i32(src1, l_src1, l_src2);
    }
    if ((getFlagX () == 1)) {
        new_dest = SignExtend (new_dest, ZZ);
    }
    TCGLabel *done_1 = gen_new_label();
    NoFurtherLoadsPending(temp_6);
    tcg_gen_xori_i32(temp_1, temp_6, 1);
    tcg_gen_andi_i32(temp_1, temp_1, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_1, arc_true, done_1);
    tcg_gen_movi_i32(temp_7, 0);
    setDebugLD(temp_7);
    gen_set_label(done_1);
    tcg_gen_mov_i32(dest, new_dest);
    tcg_temp_free(address);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(l_src1);
    tcg_temp_free(l_src2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(new_dest);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_7);

    return ret;
}


/*
 * LDD
 *    Variables: @src1, @src2, @dest
 *    Functions: getAAFlag, getZZFlag, setDebugLD, getMemory, nextReg,
 *               NoFurtherLoadsPending
 * --- code ---
 * {
 *   AA = getAAFlag ();
 *   ZZ = getZZFlag ();
 *   address = 0;
 *   if(((AA == 0) || (AA == 1)))
 *     {
 *       address = (@src1 + @src2);
 *     };
 *   if((AA == 2))
 *     {
 *       address = @src1;
 *     };
 *   if(((AA == 3) && ((ZZ == 0) || (ZZ == 3))))
 *     {
 *       address = (@src1 + (@src2 << 2));
 *     };
 *   if(((AA == 3) && (ZZ == 2)))
 *     {
 *       address = (@src1 + (@src2 << 1));
 *     };
 *   l_src1 = @src1;
 *   l_src2 = @src2;
 *   setDebugLD (1);
 *   new_dest = getMemory (address, LONG);
 *   pair = nextReg (dest);
 *   pair = getMemory ((address + 4), LONG);
 *   if(((AA == 1) || (AA == 2)))
 *     {
 *       @src1 = (l_src1 + l_src2);
 *     };
 *   if(NoFurtherLoadsPending ())
 *     {
 *       setDebugLD (0);
 *     };
 *   @dest = new_dest;
 * }
 */

int
arc_gen_LDD(DisasCtxt *ctx, TCGv src1, TCGv src2, TCGv dest)
{
    int ret = DISAS_NEXT;
    int AA;
    int ZZ;
    TCGv address = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv l_src1 = tcg_temp_local_new_i32();
    TCGv l_src2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv new_dest = tcg_temp_local_new_i32();
    TCGv pair = NULL;
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    AA = getAAFlag ();
    ZZ = getZZFlag ();
    tcg_gen_movi_i32(address, 0);
    if (((AA == 0) || (AA == 1))) {
        tcg_gen_add_i32(address, src1, src2);
    }
    if ((AA == 2)) {
        tcg_gen_mov_i32(address, src1);
    }
    if (((AA == 3) && ((ZZ == 0) || (ZZ == 3)))) {
        tcg_gen_shli_i32(temp_2, src2, 2);
        tcg_gen_add_i32(address, src1, temp_2);
    }
    if (((AA == 3) && (ZZ == 2))) {
        tcg_gen_shli_i32(temp_3, src2, 1);
        tcg_gen_add_i32(address, src1, temp_3);
    }
    tcg_gen_mov_i32(l_src1, src1);
    tcg_gen_mov_i32(l_src2, src2);
    tcg_gen_movi_i32(temp_4, 1);
    setDebugLD(temp_4);
    getMemory(temp_5, address, LONG);
    tcg_gen_mov_i32(new_dest, temp_5);
    pair = nextReg (dest);
    tcg_gen_addi_i32(temp_7, address, 4);
    getMemory(temp_6, temp_7, LONG);
    tcg_gen_mov_i32(pair, temp_6);
    if (((AA == 1) || (AA == 2))) {
        tcg_gen_add_i32(src1, l_src1, l_src2);
    }
    TCGLabel *done_1 = gen_new_label();
    NoFurtherLoadsPending(temp_8);
    tcg_gen_xori_i32(temp_1, temp_8, 1);
    tcg_gen_andi_i32(temp_1, temp_1, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_1, arc_true, done_1);
    tcg_gen_movi_i32(temp_9, 0);
    setDebugLD(temp_9);
    gen_set_label(done_1);
    tcg_gen_mov_i32(dest, new_dest);
    tcg_temp_free(address);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_3);
    tcg_temp_free(l_src1);
    tcg_temp_free(l_src2);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_5);
    tcg_temp_free(new_dest);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_9);

    return ret;
}


/*
 * ST
 *    Variables: @src1, @src2, @dest
 *    Functions: getAAFlag, getZZFlag, setMemory
 * --- code ---
 * {
 *   AA = getAAFlag ();
 *   ZZ = getZZFlag ();
 *   address = 0;
 *   if(((AA == 0) || (AA == 1)))
 *     {
 *       address = (@src1 + @src2);
 *     };
 *   if((AA == 2))
 *     {
 *       address = @src1;
 *     };
 *   if(((AA == 3) && ((ZZ == 0) || (ZZ == 3))))
 *     {
 *       address = (@src1 + (@src2 << 2));
 *     };
 *   if(((AA == 3) && (ZZ == 2)))
 *     {
 *       address = (@src1 + (@src2 << 1));
 *     };
 *   setMemory (address, ZZ, @dest);
 *   if(((AA == 1) || (AA == 2)))
 *     {
 *       @src1 = (@src1 + @src2);
 *     };
 * }
 */

int
arc_gen_ST(DisasCtxt *ctx, TCGv src1, TCGv src2, TCGv dest)
{
    int ret = DISAS_NEXT;
    int AA;
    int ZZ;
    TCGv address = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    AA = getAAFlag ();
    ZZ = getZZFlag ();
    tcg_gen_movi_i32(address, 0);
    if (((AA == 0) || (AA == 1))) {
        tcg_gen_add_i32(address, src1, src2);
    }
    if ((AA == 2)) {
        tcg_gen_mov_i32(address, src1);
    }
    if (((AA == 3) && ((ZZ == 0) || (ZZ == 3)))) {
        tcg_gen_shli_i32(temp_1, src2, 2);
        tcg_gen_add_i32(address, src1, temp_1);
    }
    if (((AA == 3) && (ZZ == 2))) {
        tcg_gen_shli_i32(temp_2, src2, 1);
        tcg_gen_add_i32(address, src1, temp_2);
    }
    setMemory(address, ZZ, dest);
    if (((AA == 1) || (AA == 2))) {
        tcg_gen_add_i32(src1, src1, src2);
    }
    tcg_temp_free(address);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);

    return ret;
}


/*
 * STD
 *    Variables: @src1, @src2, @dest
 *    Functions: getAAFlag, getZZFlag, setMemory,
 *               instructionHasRegisterOperandIn, nextReg, getBit
 * --- code ---
 * {
 *   AA = getAAFlag ();
 *   ZZ = getZZFlag ();
 *   address = 0;
 *   if(((AA == 0) || (AA == 1)))
 *     {
 *       address = (@src1 + @src2);
 *     };
 *   if((AA == 2))
 *     {
 *       address = @src1;
 *     };
 *   if(((AA == 3) && ((ZZ == 0) || (ZZ == 3))))
 *     {
 *       address = (@src1 + (@src2 << 2));
 *     };
 *   if(((AA == 3) && (ZZ == 2)))
 *     {
 *       address = (@src1 + (@src2 << 1));
 *     };
 *   setMemory (address, LONG, @dest);
 *   if(instructionHasRegisterOperandIn (0))
 *     {
 *       pair = nextReg (dest);
 *     }
 *   else
 *     {
 *       if((getBit (@dest, 31) == 1))
 *         {
 *           pair = 4294967295;
 *         }
 *       else
 *         {
 *           pair = 0;
 *         };
 *     };
 *   setMemory ((address + 4), LONG, pair);
 *   if(((AA == 1) || (AA == 2)))
 *     {
 *       @src1 = (@src1 + @src2);
 *     };
 * }
 */

int
arc_gen_STD(DisasCtxt *ctx, TCGv src1, TCGv src2, TCGv dest)
{
    int ret = DISAS_NEXT;
    int AA;
    int ZZ;
    TCGv address = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv pair = NULL;
    bool pair_initialized = FALSE;
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    AA = getAAFlag ();
    ZZ = getZZFlag ();
    tcg_gen_movi_i32(address, 0);
    if (((AA == 0) || (AA == 1))) {
        tcg_gen_add_i32(address, src1, src2);
    }
    if ((AA == 2)) {
        tcg_gen_mov_i32(address, src1);
    }
    if (((AA == 3) && ((ZZ == 0) || (ZZ == 3)))) {
        tcg_gen_shli_i32(temp_3, src2, 2);
        tcg_gen_add_i32(address, src1, temp_3);
    }
    if (((AA == 3) && (ZZ == 2))) {
        tcg_gen_shli_i32(temp_4, src2, 1);
        tcg_gen_add_i32(address, src1, temp_4);
    }
    setMemory(address, LONG, dest);
    if (instructionHasRegisterOperandIn (0)) {
        pair = nextReg (dest);
    } else {
        TCGLabel *else_1 = gen_new_label();
        TCGLabel *done_1 = gen_new_label();
        tcg_gen_movi_i32(temp_6, 31);
        getBit(temp_5, dest, temp_6);
        tcg_gen_setcondi_i32(TCG_COND_EQ, temp_1, temp_5, 1);
        tcg_gen_xori_i32(temp_2, temp_1, 1);
        tcg_gen_andi_i32(temp_2, temp_2, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_1);
        pair = tcg_temp_local_new_i32();
        pair_initialized = TRUE;
        tcg_gen_movi_i32(pair, 4294967295);
        tcg_gen_br(done_1);
        gen_set_label(else_1);
        tcg_gen_movi_i32(pair, 0);
        gen_set_label(done_1);
    }

    tcg_gen_addi_i32(temp_7, address, 4);
    setMemory(temp_7, LONG, pair);
    if (((AA == 1) || (AA == 2))) {
        tcg_gen_add_i32(src1, src1, src2);
    }
    tcg_temp_free(address);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_4);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_7);
    if (pair_initialized) {
        tcg_temp_free(pair);
    }

    return ret;
}


/*
 * ENTER_S
 *    Variables: @u6
 *    Functions: helperEnter
 * --- code ---
 * {
 *   helperEnter (@u6);
 * }
 */

int
arc_gen_ENTER_S(DisasCtxt *ctx, TCGv u6)
{
    int ret = DISAS_NEXT;

    helperEnter(u6);
    return ret;
}


/*
 * LEAVE_S
 *    Variables: @u7
 *    Functions: helperLeave
 * --- code ---
 * {
 *   helperLeave (@u7);
 * }
 */

int
arc_gen_LEAVE_S(DisasCtxt *ctx, TCGv u7)
{
    int ret = DISAS_NEXT;

    helperLeave(u7);
    return ret;
}


/*
 * POP
 *    Variables: @dest
 *    Functions: getMemory, getRegister, setRegister
 * --- code ---
 * {
 *   new_dest = getMemory (getRegister (R_SP), LONG);
 *   setRegister (R_SP, (getRegister (R_SP) + 4));
 *   @dest = new_dest;
 * }
 */

int
arc_gen_POP(DisasCtxt *ctx, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv new_dest = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    getRegister(temp_3, R_SP);
    tcg_gen_mov_i32(temp_2, temp_3);
    getMemory(temp_1, temp_2, LONG);
    tcg_gen_mov_i32(new_dest, temp_1);
    getRegister(temp_6, R_SP);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_addi_i32(temp_4, temp_5, 4);
    setRegister(R_SP, temp_4);
    tcg_gen_mov_i32(dest, new_dest);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_1);
    tcg_temp_free(new_dest);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * PUSH
 *    Variables: @src
 *    Functions: setMemory, getRegister, setRegister
 * --- code ---
 * {
 *   local_src = @src;
 *   setMemory ((getRegister (R_SP) - 4), LONG, local_src);
 *   setRegister (R_SP, (getRegister (R_SP) - 4));
 * }
 */

int
arc_gen_PUSH(DisasCtxt *ctx, TCGv src)
{
    int ret = DISAS_NEXT;
    TCGv local_src = tcg_temp_local_new_i32();
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(local_src, src);
    getRegister(temp_3, R_SP);
    tcg_gen_mov_i32(temp_2, temp_3);
    tcg_gen_subi_i32(temp_1, temp_2, 4);
    setMemory(temp_1, LONG, local_src);
    getRegister(temp_6, R_SP);
    tcg_gen_mov_i32(temp_5, temp_6);
    tcg_gen_subi_i32(temp_4, temp_5, 4);
    setRegister(R_SP, temp_4);
    tcg_temp_free(local_src);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_5);
    tcg_temp_free(temp_4);

    return ret;
}


/*
 * LP
 *    Variables: @rd
 *    Functions: getCCFlag, getRegIndex, writeAuxReg, nextInsnAddress, getPCL,
 *               setPC
 * --- code ---
 * {
 *   if((getCCFlag () == true))
 *     {
 *       lp_start_index = getRegIndex (LP_START);
 *       lp_end_index = getRegIndex (LP_END);
 *       writeAuxReg (lp_start_index, nextInsnAddress ());
 *       writeAuxReg (lp_end_index, (getPCL () + @rd));
 *     }
 *   else
 *     {
 *       setPC ((getPCL () + @rd));
 *     };
 * }
 */

int
arc_gen_LP(DisasCtxt *ctx, TCGv rd)
{
    int ret = DISAS_NEXT;
    TCGv temp_3 = tcg_temp_local_new_i32();
    TCGv temp_1 = tcg_temp_local_new_i32();
    TCGv temp_2 = tcg_temp_local_new_i32();
    TCGv temp_4 = tcg_temp_local_new_i32();
    TCGv lp_start_index = tcg_temp_local_new_i32();
    TCGv temp_5 = tcg_temp_local_new_i32();
    TCGv lp_end_index = tcg_temp_local_new_i32();
    TCGv temp_7 = tcg_temp_local_new_i32();
    TCGv temp_6 = tcg_temp_local_new_i32();
    TCGv temp_10 = tcg_temp_local_new_i32();
    TCGv temp_9 = tcg_temp_local_new_i32();
    TCGv temp_8 = tcg_temp_local_new_i32();
    TCGv temp_13 = tcg_temp_local_new_i32();
    TCGv temp_12 = tcg_temp_local_new_i32();
    TCGv temp_11 = tcg_temp_local_new_i32();
    TCGLabel *else_1 = gen_new_label();
    TCGLabel *done_1 = gen_new_label();
    getCCFlag(temp_3);
    tcg_gen_setcond_i32(TCG_COND_EQ, temp_1, temp_3, arc_true);
    tcg_gen_xori_i32(temp_2, temp_1, 1);
    tcg_gen_andi_i32(temp_2, temp_2, 1);
    tcg_gen_brcond_i32(TCG_COND_EQ, temp_2, arc_true, else_1);
    getRegIndex(temp_4, LP_START);
    tcg_gen_mov_i32(lp_start_index, temp_4);
    getRegIndex(temp_5, LP_END);
    tcg_gen_mov_i32(lp_end_index, temp_5);
    nextInsnAddress(temp_7);
    tcg_gen_mov_i32(temp_6, temp_7);
    writeAuxReg(lp_start_index, temp_6);
    getPCL(temp_10);
    tcg_gen_mov_i32(temp_9, temp_10);
    tcg_gen_add_i32(temp_8, temp_9, rd);
    writeAuxReg(lp_end_index, temp_8);
    tcg_gen_br(done_1);
    gen_set_label(else_1);
    getPCL(temp_13);
    tcg_gen_mov_i32(temp_12, temp_13);
    tcg_gen_add_i32(temp_11, temp_12, rd);
    setPC(temp_11);
    gen_set_label(done_1);
    tcg_temp_free(temp_3);
    tcg_temp_free(temp_1);
    tcg_temp_free(temp_2);
    tcg_temp_free(temp_4);
    tcg_temp_free(lp_start_index);
    tcg_temp_free(temp_5);
    tcg_temp_free(lp_end_index);
    tcg_temp_free(temp_7);
    tcg_temp_free(temp_6);
    tcg_temp_free(temp_10);
    tcg_temp_free(temp_9);
    tcg_temp_free(temp_8);
    tcg_temp_free(temp_13);
    tcg_temp_free(temp_12);
    tcg_temp_free(temp_11);

    return ret;
}


/*
 * NORM
 *    Variables: @src, @dest
 *    Functions: HELPER, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   psrc = @src;
 *   i = HELPER (norm, psrc);
 *   @dest = (31 - i);
 *   if((getFFlag () == true))
 *     {
 *       setZFlag (psrc);
 *       setNFlag (psrc);
 *     };
 * }
 */

int
arc_gen_NORM(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv psrc = tcg_temp_local_new_i32();
    TCGv i = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(psrc, src);
    ARC_HELPER(norm, i, psrc);
    tcg_gen_subfi_i32(dest, 31, i);
    if ((getFFlag () == true)) {
        setZFlag(psrc);
        setNFlag(psrc);
    }
    tcg_temp_free(psrc);
    tcg_temp_free(i);

    return ret;
}


/*
 * NORMH
 *    Variables: @src, @dest
 *    Functions: HELPER, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   lsrc = (@src & 65535);
 *   i = HELPER (normh, lsrc);
 *   @dest = (15 - i);
 *   if((getFFlag () == true))
 *     {
 *       setZFlag (lsrc);
 *       setNFlag (lsrc);
 *     };
 * }
 */

int
arc_gen_NORMH(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv lsrc = tcg_temp_local_new_i32();
    TCGv i = tcg_temp_local_new_i32();
    tcg_gen_andi_i32(lsrc, src, 65535);
    ARC_HELPER(normh, i, lsrc);
    tcg_gen_subfi_i32(dest, 15, i);
    if ((getFFlag () == true)) {
        setZFlag(lsrc);
        setNFlag(lsrc);
    }
    tcg_temp_free(lsrc);
    tcg_temp_free(i);

    return ret;
}


/*
 * FLS
 *    Variables: @src, @dest
 *    Functions: HELPER, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   psrc = @src;
 *   @dest = HELPER (fls, psrc);
 *   if((getFFlag () == true))
 *     {
 *       setZFlag (psrc);
 *       setNFlag (psrc);
 *     };
 * }
 */

int
arc_gen_FLS(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv psrc = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(psrc, src);
    ARC_HELPER(fls, dest, psrc);
    if ((getFFlag () == true)) {
        setZFlag(psrc);
        setNFlag(psrc);
    }
    tcg_temp_free(psrc);

    return ret;
}


/*
 * FFS
 *    Variables: @src, @dest
 *    Functions: HELPER, getFFlag, setZFlag, setNFlag
 * --- code ---
 * {
 *   psrc = @src;
 *   @dest = HELPER (ffs, psrc);
 *   if((getFFlag () == true))
 *     {
 *       setZFlag (psrc);
 *       setNFlag (psrc);
 *     };
 * }
 */

int
arc_gen_FFS(DisasCtxt *ctx, TCGv src, TCGv dest)
{
    int ret = DISAS_NEXT;
    TCGv psrc = tcg_temp_local_new_i32();
    tcg_gen_mov_i32(psrc, src);
    ARC_HELPER(ffs, dest, psrc);
    if ((getFFlag () == true)) {
        setZFlag(psrc);
        setNFlag(psrc);
    }
    tcg_temp_free(psrc);

    return ret;
}
