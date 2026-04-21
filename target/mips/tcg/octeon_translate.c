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

typedef void gen_helper_lmi(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64);

static bool octeon_check_64(DisasContext *ctx)
{
    check_mips_64(ctx);
    return ctx->base.is_jmp == DISAS_NEXT;
}

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

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_andi_i64(t0, t0, 0xff);
    gen_store_gpr(t0, a->rd);
    return true;
}

static bool trans_DMUL(DisasContext *ctx, arg_DMUL *a)
{
    TCGv_i64 t0, t1;

    if (!octeon_check_64(ctx)) {
        return true;
    }

    if (a->rd == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    tcg_gen_mul_i64(t0, t0, t1);
    gen_store_gpr(t0, a->rd);
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

    if (a->dw && !octeon_check_64(ctx)) {
        return true;
    }

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

static bool trans_seqne(DisasContext *ctx, const arg_cmp3 *a)
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
    TCGv_i64 t0;

    if (a->rt == 0) {
        /* nop */
        return true;
    }

    t0 = tcg_temp_new_i64();

    gen_load_gpr(t0, a->rs);

    /* Sign-extend to 64 bit value */
    int64_t imm = a->imm;
    if (a->ne) {
        tcg_gen_setcondi_i64(TCG_COND_NE, cpu_gpr[a->rt], t0, imm);
    } else {
        tcg_gen_setcondi_i64(TCG_COND_EQ, cpu_gpr[a->rt], t0, imm);
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
    if (mop == MO_UQ && !octeon_check_64(ctx)) {
        return true;
    }

    gen_lx(ctx, a->rd, a->base, a->index, mop);

    return true;
}

static bool trans_saa(DisasContext *ctx, arg_saa *a, MemOp mop)
{
    if (mop == MO_UQ && !octeon_check_64(ctx)) {
        return true;
    }

    TCGv_i64 addr = tcg_temp_new_i64();
    MemOp amo = mo_endian(ctx) | mop | MO_ALIGN;

    gen_base_offset_addr(ctx, addr, a->base, 0);

    if (mop == MO_UQ) {
        TCGv_i64 value = tcg_temp_new_i64();
        TCGv_i64 old = tcg_temp_new_i64();

        gen_load_gpr(value, a->rt);
        tcg_gen_atomic_fetch_add_i64(old, addr, value, ctx->mem_idx, amo);
    } else {
        TCGv_i64 value = tcg_temp_new_i64();
        TCGv_i32 value32 = tcg_temp_new_i32();
        TCGv_i32 old = tcg_temp_new_i32();

        gen_load_gpr(value, a->rt);
        tcg_gen_extrl_i64_i32(value32, value);
        tcg_gen_atomic_fetch_add_i32(old, addr, value32, ctx->mem_idx, amo);
    }

    return true;
}

static bool trans_ZCB(DisasContext *ctx, arg_zcb *a)
{
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 line = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);

    gen_base_offset_addr(ctx, addr, a->base, 0);

    /*
     * QEMU models ZCB/ZCBT as zeroing the containing 128-byte cache line
     * in guest memory.
     */
    tcg_gen_andi_i64(line, addr, ~0x7fULL);

    for (int i = 0; i < 16; i++) {
        TCGv_i64 slot = tcg_temp_new_i64();

        tcg_gen_addi_i64(slot, line, i * 8);
        tcg_gen_qemu_st_i64(zero, slot, ctx->mem_idx, mo_endian(ctx) | MO_UQ);
    }

    return true;
}

static bool trans_ZCBT(DisasContext *ctx, arg_zcb *a)
{
    return trans_ZCB(ctx, a);
}

static ptrdiff_t octeon_tc_mpl_offset(unsigned int index)
{
    return offsetof(CPUMIPSState, active_tc.octeon.MPL[index]);
}

static ptrdiff_t octeon_tc_p_offset(unsigned int index)
{
    return offsetof(CPUMIPSState, active_tc.octeon.P[index]);
}

static void octeon_store_tc_field(ptrdiff_t offset, TCGv_i64 value)
{
    tcg_gen_st_i64(value, tcg_env, offset);
}

static void octeon_zero_partial_product_state(void)
{
    TCGv_i64 zero = tcg_constant_i64(0);

    for (int i = 0; i < 2 * 3; i++) {
        octeon_store_tc_field(octeon_tc_p_offset(i), zero);
    }
}

static void octeon_clear_upper_multiplier_state(void)
{
    TCGv_i64 zero = tcg_constant_i64(0);

    /*
     * MTM0 starts a new multiplier chain.  Guest code relies on a single
     * MTM0 load making the remaining multiplier limbs zero unless later
     * MTM1/MTM2 instructions explicitly populate them.
     */
    octeon_store_tc_field(octeon_tc_mpl_offset(1), zero);
    octeon_store_tc_field(octeon_tc_mpl_offset(2), zero);
    octeon_store_tc_field(octeon_tc_mpl_offset(4), zero);
    octeon_store_tc_field(octeon_tc_mpl_offset(5), zero);
}

static bool trans_mtm(DisasContext *ctx, arg_r2 *a, unsigned int index)
{
    if (!octeon_check_64(ctx)) {
        return true;
    }

    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rs);
    octeon_store_tc_field(octeon_tc_mpl_offset(index), value);
    gen_load_gpr(value, a->rt);
    octeon_store_tc_field(octeon_tc_mpl_offset(index + 3), value);
    if (index == 0) {
        octeon_clear_upper_multiplier_state();
    }
    octeon_zero_partial_product_state();
    return true;
}

static bool trans_mtp(DisasContext *ctx, arg_r2 *a, unsigned int index)
{
    if (!octeon_check_64(ctx)) {
        return true;
    }

    TCGv_i64 value = tcg_temp_new_i64();

    gen_load_gpr(value, a->rs);
    octeon_store_tc_field(octeon_tc_p_offset(index), value);
    gen_load_gpr(value, a->rt);
    octeon_store_tc_field(octeon_tc_p_offset(index + 3), value);
    return true;
}

static bool trans_vmul(DisasContext *ctx, arg_decode_ext_octeon1 *a,
                       gen_helper_lmi *helper)
{
    if (!octeon_check_64(ctx)) {
        return true;
    }

    TCGv_i64 rs = tcg_temp_new_i64();
    TCGv_i64 rt = tcg_temp_new_i64();
    TCGv_i64 rd = tcg_temp_new_i64();

    gen_load_gpr(rs, a->rs);
    gen_load_gpr(rt, a->rt);
    helper(rd, tcg_env, rs, rt);
    gen_store_gpr(rd, a->rd);
    return true;
}

TRANS(SAA,  trans_saa, MO_UL);
TRANS(SAAD, trans_saa, MO_UQ);
TRANS(LBX,  trans_lx, MO_SB);
TRANS(LBUX, trans_lx, MO_UB);
TRANS(LHX,  trans_lx, MO_SW);
TRANS(LHUX, trans_lx, MO_UW);
TRANS(LWX,  trans_lx, MO_SL);
TRANS(LWUX, trans_lx, MO_UL);
TRANS(LDX,  trans_lx, MO_UQ);
TRANS(MTM0, trans_mtm, 0);
TRANS(MTM1, trans_mtm, 1);
TRANS(MTM2, trans_mtm, 2);
TRANS(MTP0, trans_mtp, 0);
TRANS(MTP1, trans_mtp, 1);
TRANS(MTP2, trans_mtp, 2);
TRANS(VMULU, trans_vmul, gen_helper_octeon_vmulu);
TRANS(VMM0, trans_vmul, gen_helper_octeon_vmm0);
TRANS(V3MULU, trans_vmul, gen_helper_octeon_v3mulu);
