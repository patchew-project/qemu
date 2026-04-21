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

static bool octeon_cop2_is_supported_dmfc2(uint16_t sel)
{
    switch (sel) {
    case OCTEON_COP2_SEL_3DES_KEY0:
    case OCTEON_COP2_SEL_3DES_KEY1:
    case OCTEON_COP2_SEL_3DES_KEY2:
    case OCTEON_COP2_SEL_3DES_IV:
    case OCTEON_COP2_SEL_3DES_RESULT:
    case OCTEON_COP2_SEL_3DES_RESULT_IN:
    case OCTEON_COP2_SEL_AES_RESULT0:
    case OCTEON_COP2_SEL_AES_RESULT1:
    case OCTEON_COP2_SEL_AES_KEY0:
    case OCTEON_COP2_SEL_AES_KEY1:
    case OCTEON_COP2_SEL_AES_KEY2:
    case OCTEON_COP2_SEL_AES_KEY3:
    case OCTEON_COP2_SEL_AES_KEYLENGTH:
    case OCTEON_COP2_SEL_CRC_POLYNOMIAL:
    case OCTEON_COP2_SEL_AES_IV0:
    case OCTEON_COP2_SEL_AES_IV1:
    case OCTEON_COP2_SEL_CRC_IV:
    case OCTEON_COP2_SEL_CRC_LEN:
    case OCTEON_COP2_SEL_CRC_IV_REFLECT:
    case OCTEON_COP2_SEL_HSH_DATW0:
    case OCTEON_COP2_SEL_HSH_DATW1:
    case OCTEON_COP2_SEL_HSH_DATW2:
    case OCTEON_COP2_SEL_HSH_DATW3:
    case OCTEON_COP2_SEL_HSH_DATW4:
    case OCTEON_COP2_SEL_HSH_DATW5:
    case OCTEON_COP2_SEL_HSH_DATW6:
    case OCTEON_COP2_SEL_HSH_DATW7:
    case OCTEON_COP2_SEL_HSH_DATW8:
    case OCTEON_COP2_SEL_HSH_DATW9:
    case OCTEON_COP2_SEL_HSH_DATW10:
    case OCTEON_COP2_SEL_HSH_DATW11:
    case OCTEON_COP2_SEL_HSH_DATW12:
    case OCTEON_COP2_SEL_HSH_DATW13:
    case OCTEON_COP2_SEL_HSH_DATW14:
    case OCTEON_COP2_SEL_HSH_DATW15:
    case OCTEON_COP2_SEL_HSH_IV0:
    case OCTEON_COP2_SEL_HSH_IV1:
    case OCTEON_COP2_SEL_HSH_IV2:
    case OCTEON_COP2_SEL_HSH_IV3:
    case OCTEON_COP2_SEL_HSH_IVW0:
    case OCTEON_COP2_SEL_HSH_IVW1:
    case OCTEON_COP2_SEL_HSH_IVW2:
    case OCTEON_COP2_SEL_HSH_IVW3:
    case OCTEON_COP2_SEL_HSH_IVW4:
    case OCTEON_COP2_SEL_HSH_IVW5:
    case OCTEON_COP2_SEL_HSH_IVW6:
    case OCTEON_COP2_SEL_HSH_IVW7:
    case OCTEON_COP2_SEL_AES_DAT0:
    case OCTEON_COP2_SEL_GFM_MUL_REFLECT0:
    case OCTEON_COP2_SEL_GFM_MUL_REFLECT1:
    case OCTEON_COP2_SEL_GFM_RESINP_REFLECT0:
    case OCTEON_COP2_SEL_GFM_RESINP_REFLECT1:
    case OCTEON_COP2_SEL_GFM_MUL0:
    case OCTEON_COP2_SEL_GFM_MUL1:
    case OCTEON_COP2_SEL_GFM_RESINP0:
    case OCTEON_COP2_SEL_GFM_RESINP1:
    case OCTEON_COP2_SEL_GFM_POLY:
        return true;
    default:
        return false;
    }
}

static bool octeon_cop2_is_supported_dmtc2(uint16_t sel)
{
    switch (sel) {
    case OCTEON_COP2_SEL_3DES_KEY0:
    case OCTEON_COP2_SEL_3DES_KEY1:
    case OCTEON_COP2_SEL_3DES_KEY2:
    case OCTEON_COP2_SEL_3DES_IV:
    case OCTEON_COP2_SEL_3DES_RESULT_IN:
    case OCTEON_COP2_SEL_3DES_ENC_CBC:
    case OCTEON_COP2_SEL_KAS_ENC_CBC:
    case OCTEON_COP2_SEL_3DES_ENC:
    case OCTEON_COP2_SEL_KAS_ENC:
    case OCTEON_COP2_SEL_3DES_DEC_CBC:
    case OCTEON_COP2_SEL_3DES_DEC:
    case OCTEON_COP2_SEL_AES_RESULT0:
    case OCTEON_COP2_SEL_AES_RESULT1:
    case OCTEON_COP2_SEL_AES_IV0:
    case OCTEON_COP2_SEL_AES_IV1:
    case OCTEON_COP2_SEL_AES_KEY0:
    case OCTEON_COP2_SEL_AES_KEY1:
    case OCTEON_COP2_SEL_AES_KEY2:
    case OCTEON_COP2_SEL_AES_KEY3:
    case OCTEON_COP2_SEL_AES_ENC_CBC0:
    case OCTEON_COP2_SEL_AES_ENC0:
    case OCTEON_COP2_SEL_AES_DEC_CBC0:
    case OCTEON_COP2_SEL_AES_DEC0:
    case OCTEON_COP2_SEL_AES_KEYLENGTH:
    case OCTEON_COP2_SEL_CRC_WRITE_POLYNOMIAL:
    case OCTEON_COP2_SEL_CRC_IV:
    case OCTEON_COP2_SEL_CRC_WRITE_LEN:
    case OCTEON_COP2_SEL_CRC_WRITE_IV_REFLECT:
    case OCTEON_COP2_SEL_CRC_WRITE_BYTE:
    case OCTEON_COP2_SEL_CRC_WRITE_HALF:
    case OCTEON_COP2_SEL_CRC_WRITE_WORD:
    case OCTEON_COP2_SEL_CRC_WRITE_DWORD:
    case OCTEON_COP2_SEL_CRC_WRITE_VAR:
    case OCTEON_COP2_SEL_CRC_WRITE_POLYNOMIAL_REFLECT:
    case OCTEON_COP2_SEL_CRC_WRITE_BYTE_REFLECT:
    case OCTEON_COP2_SEL_CRC_WRITE_HALF_REFLECT:
    case OCTEON_COP2_SEL_CRC_WRITE_WORD_REFLECT:
    case OCTEON_COP2_SEL_CRC_WRITE_DWORD_REFLECT:
    case OCTEON_COP2_SEL_CRC_WRITE_VAR_REFLECT:
    case OCTEON_COP2_SEL_HSH_DAT0:
    case OCTEON_COP2_SEL_HSH_DAT1:
    case OCTEON_COP2_SEL_HSH_DAT2:
    case OCTEON_COP2_SEL_HSH_DAT3:
    case OCTEON_COP2_SEL_HSH_DAT4:
    case OCTEON_COP2_SEL_HSH_DAT5:
    case OCTEON_COP2_SEL_HSH_DAT6:
    case OCTEON_COP2_SEL_HSH_IV0:
    case OCTEON_COP2_SEL_HSH_IV1:
    case OCTEON_COP2_SEL_HSH_IV2:
    case OCTEON_COP2_SEL_HSH_IV3:
    case OCTEON_COP2_SEL_HSH_DATW0:
    case OCTEON_COP2_SEL_HSH_DATW1:
    case OCTEON_COP2_SEL_HSH_DATW2:
    case OCTEON_COP2_SEL_HSH_DATW3:
    case OCTEON_COP2_SEL_HSH_DATW4:
    case OCTEON_COP2_SEL_HSH_DATW5:
    case OCTEON_COP2_SEL_HSH_DATW6:
    case OCTEON_COP2_SEL_HSH_DATW7:
    case OCTEON_COP2_SEL_HSH_DATW8:
    case OCTEON_COP2_SEL_HSH_DATW9:
    case OCTEON_COP2_SEL_HSH_DATW10:
    case OCTEON_COP2_SEL_HSH_DATW11:
    case OCTEON_COP2_SEL_HSH_DATW12:
    case OCTEON_COP2_SEL_HSH_DATW13:
    case OCTEON_COP2_SEL_HSH_DATW14:
    case OCTEON_COP2_SEL_HSH_DATW15:
    case OCTEON_COP2_SEL_HSH_IVW0:
    case OCTEON_COP2_SEL_HSH_IVW1:
    case OCTEON_COP2_SEL_HSH_IVW2:
    case OCTEON_COP2_SEL_HSH_IVW3:
    case OCTEON_COP2_SEL_HSH_IVW4:
    case OCTEON_COP2_SEL_HSH_IVW5:
    case OCTEON_COP2_SEL_HSH_IVW6:
    case OCTEON_COP2_SEL_HSH_IVW7:
    case OCTEON_COP2_SEL_GFM_MUL_REFLECT0:
    case OCTEON_COP2_SEL_GFM_MUL_REFLECT1:
    case OCTEON_COP2_SEL_GFM_XOR0_REFLECT:
    case OCTEON_COP2_SEL_GFM_MUL0:
    case OCTEON_COP2_SEL_GFM_MUL1:
    case OCTEON_COP2_SEL_GFM_RESINP0:
    case OCTEON_COP2_SEL_GFM_RESINP1:
    case OCTEON_COP2_SEL_GFM_XOR0:
    case OCTEON_COP2_SEL_GFM_POLY:
    case OCTEON_COP2_SEL_HSH_STARTSHA1_COMPAT:
    case OCTEON_COP2_SEL_HSH_STARTMD5:
    case OCTEON_COP2_SEL_SNOW3G_START:
    case OCTEON_COP2_SEL_SNOW3G_MORE:
    case OCTEON_COP2_SEL_HSH_STARTSHA256:
    case OCTEON_COP2_SEL_HSH_STARTSHA1:
    case OCTEON_COP2_SEL_GFM_XORMUL1_REFLECT:
    case OCTEON_COP2_SEL_HSH_STARTSHA512:
    case OCTEON_COP2_SEL_GFM_XORMUL1:
    case OCTEON_COP2_SEL_AES_ENC_CBC1:
    case OCTEON_COP2_SEL_AES_ENC1:
    case OCTEON_COP2_SEL_AES_DEC_CBC1:
    case OCTEON_COP2_SEL_AES_DEC1:
        return true;
    default:
        return false;
    }
}

bool gen_octeon_cop2(DisasContext *ctx)
{
    enum {
        OCTEON_CP2_RS_DMFC2 = 0x01,
        OCTEON_CP2_RS_DMTC2 = 0x05,
    };
    int rs = extract32(ctx->opcode, 21, 5);
    int rt = extract32(ctx->opcode, 16, 5);
    uint16_t sel = ctx->opcode;
    TCGv_i64 t0;

    switch (rs) {
    case OCTEON_CP2_RS_DMFC2:
        if (!octeon_check_64(ctx)) {
            return true;
        }
        if (!octeon_cop2_is_supported_dmfc2(sel)) {
            return false;
        }
        t0 = tcg_temp_new_i64();
        gen_helper_octeon_cop2_dmfc2(t0, tcg_env, tcg_constant_i32(sel));
        gen_store_gpr(t0, rt);
        return true;
    case OCTEON_CP2_RS_DMTC2:
        if (!octeon_check_64(ctx)) {
            return true;
        }
        if (!octeon_cop2_is_supported_dmtc2(sel)) {
            return false;
        }
        t0 = tcg_temp_new_i64();
        gen_load_gpr(t0, rt);
        gen_helper_octeon_cop2_dmtc2(tcg_env, t0, tcg_constant_i32(sel));
        return true;
    default:
        return false;
    }
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

static bool trans_la_common(DisasContext *ctx, int base, int add_reg, int rd,
                            int64_t imm, bool dw)
{
    if (dw && !octeon_check_64(ctx)) {
        return true;
    }

    TCGv_i64 addr = tcg_temp_new_i64();

    gen_base_offset_addr(ctx, addr, base, 0);

    if (dw) {
#if TARGET_LONG_BITS == 64
        TCGv_i64 value = tcg_temp_new_i64();
        TCGv_i64 old = tcg_temp_new_i64();
        MemOp amo = mo_endian(ctx) | MO_UQ | MO_ALIGN;

        if (add_reg >= 0) {
            gen_load_gpr(value, add_reg);
        } else {
            tcg_gen_movi_i64(value, imm);
        }

        tcg_gen_atomic_fetch_add_i64(old, addr, value, ctx->mem_idx, amo);
        gen_store_gpr(old, rd);
#endif
    } else {
        TCGv_i64 old = tcg_temp_new_i64();
        TCGv_i32 value32 = tcg_temp_new_i32();
        TCGv_i32 old32 = tcg_temp_new_i32();
        MemOp amo = mo_endian(ctx) | MO_UL | MO_ALIGN;

        if (add_reg < 0) {
            tcg_gen_movi_i32(value32, imm);
        } else {
            TCGv_i64 value = tcg_temp_new_i64();

            gen_load_gpr(value, add_reg);
            tcg_gen_extrl_i64_i32(value32, value);
        }

        tcg_gen_atomic_fetch_add_i32(old32, addr, value32, ctx->mem_idx, amo);
        tcg_gen_ext_i32_i64(old, old32);
        gen_store_gpr(old, rd);
    }

    return true;
}

static bool trans_law_common(DisasContext *ctx, int base, int add_reg, int rd,
                             int64_t imm, bool dw)
{
    if (dw && !octeon_check_64(ctx)) {
        return true;
    }

    TCGv_i64 addr = tcg_temp_new_i64();

    gen_base_offset_addr(ctx, addr, base, 0);

    if (dw) {
#if TARGET_LONG_BITS == 64
        TCGv_i64 value = tcg_temp_new_i64();
        TCGv_i64 old = tcg_temp_new_i64();
        MemOp amo = mo_endian(ctx) | MO_UQ | MO_ALIGN;

        if (add_reg >= 0) {
            gen_load_gpr(value, add_reg);
        } else {
            tcg_gen_movi_i64(value, imm);
        }

        tcg_gen_atomic_xchg_i64(old, addr, value, ctx->mem_idx, amo);
        gen_store_gpr(old, rd);
#endif
    } else {
        TCGv_i64 old = tcg_temp_new_i64();
        TCGv_i32 value32 = tcg_temp_new_i32();
        TCGv_i32 old32 = tcg_temp_new_i32();
        MemOp amo = mo_endian(ctx) | MO_UL | MO_ALIGN;

        if (add_reg >= 0) {
            TCGv_i64 value = tcg_temp_new_i64();

            gen_load_gpr(value, add_reg);
            tcg_gen_extrl_i64_i32(value32, value);
        } else {
            tcg_gen_movi_i32(value32, imm);
        }

        tcg_gen_atomic_xchg_i32(old32, addr, value32, ctx->mem_idx, amo);
        tcg_gen_ext_i32_i64(old, old32);
        gen_store_gpr(old, rd);
    }

    return true;
}

static bool trans_lai(DisasContext *ctx, arg_la *a, int unused)
{
    return trans_la_common(ctx, a->base, -1, a->rd, 1, false);
}

static bool trans_laid(DisasContext *ctx, arg_la *a, int unused)
{
    return trans_la_common(ctx, a->base, -1, a->rd, 1, true);
}

static bool trans_lad(DisasContext *ctx, arg_la *a, int unused)
{
    return trans_la_common(ctx, a->base, -1, a->rd, -1, false);
}

static bool trans_ladd(DisasContext *ctx, arg_la *a, int unused)
{
    return trans_la_common(ctx, a->base, -1, a->rd, -1, true);
}

static bool trans_laa(DisasContext *ctx, arg_laa *a, int unused)
{
    return trans_la_common(ctx, a->base, a->add, a->rd, 0, false);
}

static bool trans_laad(DisasContext *ctx, arg_laa *a, int unused)
{
    return trans_la_common(ctx, a->base, a->add, a->rd, 0, true);
}

static bool trans_las(DisasContext *ctx, arg_la *a, int unused)
{
    return trans_law_common(ctx, a->base, -1, a->rd, -1, false);
}

static bool trans_lasd(DisasContext *ctx, arg_la *a, int unused)
{
    return trans_law_common(ctx, a->base, -1, a->rd, -1, true);
}

static bool trans_lac(DisasContext *ctx, arg_la *a, int unused)
{
    return trans_law_common(ctx, a->base, -1, a->rd, 0, false);
}

static bool trans_lacd(DisasContext *ctx, arg_la *a, int unused)
{
    return trans_law_common(ctx, a->base, -1, a->rd, 0, true);
}

static bool trans_law(DisasContext *ctx, arg_laa *a, int unused)
{
    return trans_law_common(ctx, a->base, a->add, a->rd, 0, false);
}

static bool trans_lawd(DisasContext *ctx, arg_laa *a, int unused)
{
    return trans_law_common(ctx, a->base, a->add, a->rd, 0, true);
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
TRANS(LAI,  trans_lai, 0);
TRANS(LAID, trans_laid, 0);
TRANS(LAD,  trans_lad, 0);
TRANS(LADD, trans_ladd, 0);
TRANS(LAA,  trans_laa, 0);
TRANS(LAAD, trans_laad, 0);
TRANS(LAS,  trans_las, 0);
TRANS(LASD, trans_lasd, 0);
TRANS(LAC,  trans_lac, 0);
TRANS(LACD, trans_lacd, 0);
TRANS(LAW,  trans_law, 0);
TRANS(LAWD, trans_lawd, 0);
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
