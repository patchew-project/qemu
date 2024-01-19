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
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x"
                  TARGET_FMT_lx "\n", ctx->base.pc_next);
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

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_add_tl(t0, t0, t1);
    tcg_gen_andi_i64(cpu_gpr[a->rd], t0, 0xff);
    return true;
}

static bool trans_DMUL(DisasContext *ctx, arg_DMUL *a)
{
    TCGv t0, t1;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_mul_i64(cpu_gpr[a->rd], t0, t1);
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

static bool trans_SEQNE(DisasContext *ctx, arg_SEQNE *a)
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

static bool trans_SEQNEI(DisasContext *ctx, arg_SEQNEI *a)
{
    TCGv t0;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rs);

    /* Sign-extend to 64 bit value */
    target_ulong imm = a->imm;
    if (a->ne) {
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_gpr[a->rt], t0, imm);
    } else {
        tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_gpr[a->rt], t0, imm);
    }
    return true;
}

/*
 * Octeon+
 *  https://sourceware.org/legacy-ml/binutils/2011-11/msg00085.html
 */
static bool trans_SAA(DisasContext *ctx, arg_SAA *a)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->base], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    tcg_gen_add_tl(t0, t0, cpu_gpr[a->rt]);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->base], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    return true;
}

static bool trans_SAAD(DisasContext *ctx, arg_SAAD *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->base], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    tcg_gen_add_tl(t0, t0, cpu_gpr[a->rt]);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->base], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    return true;
}

/*
 *  Octeon2
 *   https://chromium.googlesource.com/chromiumos/third_party/gdb/+/refs/heads/master/opcodes/mips-opc.c
 *   https://github.com/MarvellEmbeddedProcessors/Octeon-Toolchain
 *   https://bugs.kde.org/show_bug.cgi?id=326444
 *   https://gcc.gnu.org/legacy-ml/gcc-patches/2011-12/msg01134.html
 */
static bool trans_LAI(DisasContext *ctx, arg_LAI *a)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_addi_tl(t0, t0, 1);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    return true;
}

static bool trans_LAID(DisasContext *ctx, arg_LAID *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_addi_tl(t0, t0, 1);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    return true;
}

static bool trans_LAD(DisasContext *ctx, arg_LAD *a)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_subi_tl(t0, t0, 1);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    return true;
}

static bool trans_LADD(DisasContext *ctx, arg_LADD *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_subi_tl(t0, t0, 1);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    return true;
}
/* Load Atomic Set Word - LAS; Cavium OCTEON2 */
static bool trans_LAS(DisasContext *ctx, arg_LAS *a)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_movi_tl(t0, 0xffffffff);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);

    return true;
}
/* Load Atomic Set Doubleword - LASD; Cavium OCTEON2 */
static bool trans_LASD(DisasContext *ctx, arg_LASD *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_movi_tl(t0, 0xffffffffffffffffULL);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    return true;
}
/* Load Atomic Clear Word - LAC; Cavium OCTEON2 */
static bool trans_LAC(DisasContext *ctx, arg_LAC *a)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_movi_tl(t0, 0);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    return true;
}
/* Load Atomic Clear Doubleword - LACD; Cavium OCTEON2 */
static bool trans_LACD(DisasContext *ctx, arg_LACD *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_movi_tl(t0, 0xffffffffffffffffULL);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    return true;
}

/* Load Atomic Add Word - LAA; Cavium OCTEON2 */
static bool trans_LAA(DisasContext *ctx, arg_LAA *a)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_add_tl(t0, t0, cpu_gpr[a->rt]);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    return true;
}

/* Load Atomic Add Doubleword - LAAD; Cavium OCTEON2 */
static bool trans_LAAD(DisasContext *ctx, arg_LAAD *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_add_tl(t0, t0, cpu_gpr[a->rt]);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    return true;
}
/* Load Atomic Swap Word - LAW; Cavium OCTEON2 */
static bool trans_LAW(DisasContext *ctx, arg_LAW *a)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_mov_tl(t0, cpu_gpr[a->rt]);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
    return true;
}

static bool trans_LAWD(DisasContext *ctx, arg_LAWD *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    gen_store_gpr(t0, a->rd);
    tcg_gen_mov_tl(t0, cpu_gpr[a->rt]);

    tcg_gen_qemu_st_tl(t0, cpu_gpr[a->rs], ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
    return true;
}


static bool trans_LWX(DisasContext *ctx, arg_LWX *a)
{
    TCGv t0 = tcg_temp_new();
    gen_op_addr_add(ctx, t0, cpu_gpr[a->rs], cpu_gpr[a->rt]);

    tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESL |
                           ctx->default_tcg_memop_mask);

    /* on mips64, 32 extend to 64 */
    tcg_gen_ext32s_tl(cpu_gpr[a->rd], t0);
    return true;
}

static bool trans_LHX(DisasContext *ctx, arg_LHX *a)
{
    TCGv t0 = tcg_temp_new();
    gen_op_addr_add(ctx, t0, cpu_gpr[a->rs], cpu_gpr[a->rt]);

    tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESW |
                           ctx->default_tcg_memop_mask);

    /* 16 extend to 32/64 */
    tcg_gen_ext16s_tl(cpu_gpr[a->rd], t0);
    return true;
}

static bool trans_LDX(DisasContext *ctx, arg_LDX *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_op_addr_add(ctx, t0, cpu_gpr[a->rs], cpu_gpr[a->rt]);

    tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESQ |
                           ctx->default_tcg_memop_mask);
    /* not extend */
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_LBUX(DisasContext *ctx, arg_LBUX *a)
{
    TCGv t0 = tcg_temp_new();
    gen_op_addr_add(ctx, t0, cpu_gpr[a->rs], cpu_gpr[a->rt]);

    tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_UB |
                           ctx->default_tcg_memop_mask);

    tcg_gen_ext8u_tl(cpu_gpr[a->rd], t0);
    return true;
}

static bool trans_LWUX(DisasContext *ctx, arg_LWUX *a)
{
    TCGv t0 = tcg_temp_new();
    gen_op_addr_add(ctx, t0, cpu_gpr[a->rs], cpu_gpr[a->rt]);

    tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);

    tcg_gen_ext32u_tl(cpu_gpr[a->rd], t0);
    return true;
}

static bool trans_LHUX(DisasContext *ctx, arg_LHUX *a)
{
    TCGv t0 = tcg_temp_new();
    gen_op_addr_add(ctx, t0, cpu_gpr[a->rs], cpu_gpr[a->rt]);

    tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUW |
                           ctx->default_tcg_memop_mask);

    tcg_gen_ext16u_tl(cpu_gpr[a->rd], t0);
    return true;
}

static bool trans_LBX(DisasContext *ctx, arg_LBX *a)
{
    TCGv t0 = tcg_temp_new();
    gen_op_addr_add(ctx, t0, cpu_gpr[a->rs], cpu_gpr[a->rt]);

    tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_SB |
                           ctx->default_tcg_memop_mask);

    tcg_gen_ext8s_tl(cpu_gpr[a->rd], t0);
    return true;
}
