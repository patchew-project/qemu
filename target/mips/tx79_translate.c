/*
 * Toshiba TX79-specific instructions translation routines
 *
 *  Copyright (c) 2018 Fredrik Noring
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"

/* Include the auto-generated decoder.  */
#include "decode-tx79.c.inc"

/*
 *     Overview of the TX79-specific instruction set
 *     =============================================
 *
 * The R5900 and the C790 have 128-bit wide GPRs, where the upper 64 bits
 * are only used by the specific quadword (128-bit) LQ/SQ load/store
 * instructions and certain multimedia instructions (MMIs). These MMIs
 * configure the 128-bit data path as two 64-bit, four 32-bit, eight 16-bit
 * or sixteen 8-bit paths.
 *
 * Reference:
 *
 * The Toshiba TX System RISC TX79 Core Architecture manual,
 * https://wiki.qemu.org/File:C790.pdf
 */

bool decode_ext_tx79(DisasContext *ctx, uint32_t insn)
{
    if (TARGET_LONG_BITS == 64 && decode_tx79(ctx, insn)) {
        return true;
    }
    return false;
}

/*
 *     Three-Operand Multiply and Multiply-Add (4 instructions)
 *     --------------------------------------------------------
 * MADD    [rd,] rs, rt      Multiply/Add
 * MADDU   [rd,] rs, rt      Multiply/Add Unsigned
 * MULT    [rd,] rs, rt      Multiply (3-operand)
 * MULTU   [rd,] rs, rt      Multiply Unsigned (3-operand)
 */

/*
 *     Multiply Instructions for Pipeline 1 (10 instructions)
 *     ------------------------------------------------------
 * MULT1   [rd,] rs, rt      Multiply Pipeline 1
 * MULTU1  [rd,] rs, rt      Multiply Unsigned Pipeline 1
 * DIV1    rs, rt            Divide Pipeline 1
 * DIVU1   rs, rt            Divide Unsigned Pipeline 1
 * MADD1   [rd,] rs, rt      Multiply-Add Pipeline 1
 * MADDU1  [rd,] rs, rt      Multiply-Add Unsigned Pipeline 1
 * MFHI1   rd                Move From HI1 Register
 * MFLO1   rd                Move From LO1 Register
 * MTHI1   rs                Move To HI1 Register
 * MTLO1   rs                Move To LO1 Register
 */

static bool trans_MFHI1(DisasContext *ctx, arg_rtype *a)
{
    gen_store_gpr(cpu_HI[1], a->rd);

    return true;
}

static bool trans_MFLO1(DisasContext *ctx, arg_rtype *a)
{
    gen_store_gpr(cpu_LO[1], a->rd);

    return true;
}

static bool trans_MTHI1(DisasContext *ctx, arg_rtype *a)
{
    gen_load_gpr(cpu_HI[1], a->rs);

    return true;
}

static bool trans_MTLO1(DisasContext *ctx, arg_rtype *a)
{
    gen_load_gpr(cpu_LO[1], a->rs);

    return true;
}

/*
 *     Arithmetic (19 instructions)
 *     ----------------------------
 * PADDB   rd, rs, rt        Parallel Add Byte
 * PSUBB   rd, rs, rt        Parallel Subtract Byte
 * PADDH   rd, rs, rt        Parallel Add Halfword
 * PSUBH   rd, rs, rt        Parallel Subtract Halfword
 * PADDW   rd, rs, rt        Parallel Add Word
 * PSUBW   rd, rs, rt        Parallel Subtract Word
 * PADSBH  rd, rs, rt        Parallel Add/Subtract Halfword
 * PADDSB  rd, rs, rt        Parallel Add with Signed Saturation Byte
 * PSUBSB  rd, rs, rt        Parallel Subtract with Signed Saturation Byte
 * PADDSH  rd, rs, rt        Parallel Add with Signed Saturation Halfword
 * PSUBSH  rd, rs, rt        Parallel Subtract with Signed Saturation Halfword
 * PADDSW  rd, rs, rt        Parallel Add with Signed Saturation Word
 * PSUBSW  rd, rs, rt        Parallel Subtract with Signed Saturation Word
 * PADDUB  rd, rs, rt        Parallel Add with Unsigned saturation Byte
 * PSUBUB  rd, rs, rt        Parallel Subtract with Unsigned saturation Byte
 * PADDUH  rd, rs, rt        Parallel Add with Unsigned saturation Halfword
 * PSUBUH  rd, rs, rt        Parallel Subtract with Unsigned saturation Halfword
 * PADDUW  rd, rs, rt        Parallel Add with Unsigned saturation Word
 * PSUBUW  rd, rs, rt        Parallel Subtract with Unsigned saturation Word
 */

/*
 *     Min/Max (4 instructions)
 *     ------------------------
 * PMAXH   rd, rs, rt        Parallel Maximum Halfword
 * PMINH   rd, rs, rt        Parallel Minimum Halfword
 * PMAXW   rd, rs, rt        Parallel Maximum Word
 * PMINW   rd, rs, rt        Parallel Minimum Word
 */

/*
 *     Absolute (2 instructions)
 *     -------------------------
 * PABSH   rd, rt            Parallel Absolute Halfword
 * PABSW   rd, rt            Parallel Absolute Word
 */

/*
 *     Logical (4 instructions)
 *     ------------------------
 * PAND    rd, rs, rt        Parallel AND
 * POR     rd, rs, rt        Parallel OR
 * PXOR    rd, rs, rt        Parallel XOR
 * PNOR    rd, rs, rt        Parallel NOR
 */

static bool trans_parallel_logic(DisasContext *ctx, arg_rtype *a,
                                 void (*gen_logic_i64)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 ax, bx;

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    ax = tcg_temp_new_i64();
    bx = tcg_temp_new_i64();

    /* Lower halve */
    gen_load_gpr(ax, a->rs);
    gen_load_gpr(bx, a->rt);
    gen_logic_i64(cpu_gpr[a->rd], ax, bx);

    /* Upper halve */
    gen_load_gpr_hi(ax, a->rs);
    gen_load_gpr_hi(bx, a->rt);
    gen_logic_i64(cpu_gpr_hi[a->rd], ax, bx);

    tcg_temp_free(bx);
    tcg_temp_free(ax);

    return true;
}

/* Parallel And */
static bool trans_PAND(DisasContext *ctx, arg_rtype *a)
{
    return trans_parallel_logic(ctx, a, tcg_gen_and_i64);
}

/* Parallel Or */
static bool trans_POR(DisasContext *ctx, arg_rtype *a)
{
    return trans_parallel_logic(ctx, a, tcg_gen_or_i64);
}

/* Parallel Exclusive Or */
static bool trans_PXOR(DisasContext *ctx, arg_rtype *a)
{
    return trans_parallel_logic(ctx, a, tcg_gen_xor_i64);
}

/* Parallel Not Or */
static bool trans_PNOR(DisasContext *ctx, arg_rtype *a)
{
    return trans_parallel_logic(ctx, a, tcg_gen_nor_i64);
}

/*
 *     Shift (9 instructions)
 *     ----------------------
 * PSLLH   rd, rt, sa        Parallel Shift Left Logical Halfword
 * PSRLH   rd, rt, sa        Parallel Shift Right Logical Halfword
 * PSRAH   rd, rt, sa        Parallel Shift Right Arithmetic Halfword
 * PSLLW   rd, rt, sa        Parallel Shift Left Logical Word
 * PSRLW   rd, rt, sa        Parallel Shift Right Logical Word
 * PSRAW   rd, rt, sa        Parallel Shift Right Arithmetic Word
 * PSLLVW  rd, rt, rs        Parallel Shift Left Logical Variable Word
 * PSRLVW  rd, rt, rs        Parallel Shift Right Logical Variable Word
 * PSRAVW  rd, rt, rs        Parallel Shift Right Arithmetic Variable Word
 */

/*
 *     Compare (6 instructions)
 *     ------------------------
 * PCGTB   rd, rs, rt        Parallel Compare for Greater Than Byte
 * PCEQB   rd, rs, rt        Parallel Compare for Equal Byte
 * PCGTH   rd, rs, rt        Parallel Compare for Greater Than Halfword
 * PCEQH   rd, rs, rt        Parallel Compare for Equal Halfword
 * PCGTW   rd, rs, rt        Parallel Compare for Greater Than Word
 * PCEQW   rd, rs, rt        Parallel Compare for Equal Word
 */

/*
 *     LZC (1 instruction)
 *     -------------------
 * PLZCW   rd, rs            Parallel Leading Zero or One Count Word
 */

/*
 *     Quadword Load and Store (2 instructions)
 *     ----------------------------------------
 * LQ      rt, offset(base)  Load Quadword
 * SQ      rt, offset(base)  Store Quadword
 */

/*
 *     Multiply and Divide (19 instructions)
 *     -------------------------------------
 * PMULTW  rd, rs, rt        Parallel Multiply Word
 * PMULTUW rd, rs, rt        Parallel Multiply Unsigned Word
 * PDIVW   rs, rt            Parallel Divide Word
 * PDIVUW  rs, rt            Parallel Divide Unsigned Word
 * PMADDW  rd, rs, rt        Parallel Multiply-Add Word
 * PMADDUW rd, rs, rt        Parallel Multiply-Add Unsigned Word
 * PMSUBW  rd, rs, rt        Parallel Multiply-Subtract Word
 * PMULTH  rd, rs, rt        Parallel Multiply Halfword
 * PMADDH  rd, rs, rt        Parallel Multiply-Add Halfword
 * PMSUBH  rd, rs, rt        Parallel Multiply-Subtract Halfword
 * PHMADH  rd, rs, rt        Parallel Horizontal Multiply-Add Halfword
 * PHMSBH  rd, rs, rt        Parallel Horizontal Multiply-Subtract Halfword
 * PDIVBW  rs, rt            Parallel Divide Broadcast Word
 * PMFHI   rd                Parallel Move From HI Register
 * PMFLO   rd                Parallel Move From LO Register
 * PMTHI   rs                Parallel Move To HI Register
 * PMTLO   rs                Parallel Move To LO Register
 * PMFHL   rd                Parallel Move From HI/LO Register
 * PMTHL   rs                Parallel Move To HI/LO Register
 */

/*
 *     Pack/Extend (11 instructions)
 *     -----------------------------
 * PPAC5   rd, rt            Parallel Pack to 5 bits
 * PPACB   rd, rs, rt        Parallel Pack to Byte
 * PPACH   rd, rs, rt        Parallel Pack to Halfword
 * PPACW   rd, rs, rt        Parallel Pack to Word
 * PEXT5   rd, rt            Parallel Extend Upper from 5 bits
 * PEXTUB  rd, rs, rt        Parallel Extend Upper from Byte
 * PEXTLB  rd, rs, rt        Parallel Extend Lower from Byte
 * PEXTUH  rd, rs, rt        Parallel Extend Upper from Halfword
 * PEXTLH  rd, rs, rt        Parallel Extend Lower from Halfword
 * PEXTUW  rd, rs, rt        Parallel Extend Upper from Word
 * PEXTLW  rd, rs, rt        Parallel Extend Lower from Word
 */

/*
 *     Others (16 instructions)
 *     ------------------------
 * PCPYH   rd, rt            Parallel Copy Halfword
 * PCPYLD  rd, rs, rt        Parallel Copy Lower Doubleword
 * PCPYUD  rd, rs, rt        Parallel Copy Upper Doubleword
 * PREVH   rd, rt            Parallel Reverse Halfword
 * PINTH   rd, rs, rt        Parallel Interleave Halfword
 * PINTEH  rd, rs, rt        Parallel Interleave Even Halfword
 * PEXEH   rd, rt            Parallel Exchange Even Halfword
 * PEXCH   rd, rt            Parallel Exchange Center Halfword
 * PEXEW   rd, rt            Parallel Exchange Even Word
 * PEXCW   rd, rt            Parallel Exchange Center Word
 * QFSRV   rd, rs, rt        Quadword Funnel Shift Right Variable
 * MFSA    rd                Move from Shift Amount Register
 * MTSA    rs                Move to Shift Amount Register
 * MTSAB   rs, immediate     Move Byte Count to Shift Amount Register
 * MTSAH   rs, immediate     Move Halfword Count to Shift Amount Register
 * PROT3W  rd, rt            Parallel Rotate 3 Words
 */

/* Parallel Copy Halfword */
static bool trans_PCPYH(DisasContext *s, arg_rtype *a)
{
    if (a->rd == 0) {
        /* nop */
        return true;
    }

    if (a->rt == 0) {
        tcg_gen_movi_i64(cpu_gpr[a->rd], 0);
        tcg_gen_movi_i64(cpu_gpr_hi[a->rd], 0);
        return true;
    }

    tcg_gen_deposit_i64(cpu_gpr[a->rd], cpu_gpr[a->rt], cpu_gpr[a->rt], 16, 16);
    tcg_gen_deposit_i64(cpu_gpr[a->rd], cpu_gpr[a->rd], cpu_gpr[a->rd], 32, 32);
    tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rt], cpu_gpr_hi[a->rt], 16, 16);
    tcg_gen_deposit_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rd], 32, 32);

    return true;
}

/* Parallel Copy Lower Doubleword */
static bool trans_PCPYLD(DisasContext *s, arg_rtype *a)
{
    if (a->rd == 0) {
        /* nop */
        return true;
    }

    if (a->rs == 0) {
        tcg_gen_movi_i64(cpu_gpr_hi[a->rd], 0);
    } else {
        tcg_gen_mov_i64(cpu_gpr_hi[a->rd], cpu_gpr[a->rs]);
    }

    if (a->rt == 0) {
        tcg_gen_movi_i64(cpu_gpr[a->rd], 0);
    } else if (a->rd != a->rt) {
        tcg_gen_mov_i64(cpu_gpr[a->rd], cpu_gpr[a->rt]);
    }

    return true;
}

/* Parallel Copy Upper Doubleword */
static bool trans_PCPYUD(DisasContext *s, arg_rtype *a)
{
    if (a->rd == 0) {
        /* nop */
        return true;
    }

    if (a->rs == 0) {
        tcg_gen_movi_i64(cpu_gpr[a->rd], 0);
    } else {
        tcg_gen_mov_i64(cpu_gpr[a->rd], cpu_gpr_hi[a->rs]);
    }

    if (a->rt == 0) {
        tcg_gen_movi_i64(cpu_gpr_hi[a->rd], 0);
    } else if (a->rd != a->rt) {
        tcg_gen_mov_i64(cpu_gpr_hi[a->rd], cpu_gpr_hi[a->rt]);
    }

    return true;
}
