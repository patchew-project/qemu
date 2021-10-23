/*
 *  MIPS SIMD Architecture (MSA) translation routines
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *  Copyright (c) 2012 Jia Liu & Dongxue Zhang (MIPS ASE DSP support)
 *  Copyright (c) 2020 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"
#include "fpu_helper.h"
#include "internal.h"

/* Include the auto-generated decoder.  */
#include "decode-msa.c.inc"

static const char msaregnames[][6] = {
    "w0.d0",  "w0.d1",  "w1.d0",  "w1.d1",
    "w2.d0",  "w2.d1",  "w3.d0",  "w3.d1",
    "w4.d0",  "w4.d1",  "w5.d0",  "w5.d1",
    "w6.d0",  "w6.d1",  "w7.d0",  "w7.d1",
    "w8.d0",  "w8.d1",  "w9.d0",  "w9.d1",
    "w10.d0", "w10.d1", "w11.d0", "w11.d1",
    "w12.d0", "w12.d1", "w13.d0", "w13.d1",
    "w14.d0", "w14.d1", "w15.d0", "w15.d1",
    "w16.d0", "w16.d1", "w17.d0", "w17.d1",
    "w18.d0", "w18.d1", "w19.d0", "w19.d1",
    "w20.d0", "w20.d1", "w21.d0", "w21.d1",
    "w22.d0", "w22.d1", "w23.d0", "w23.d1",
    "w24.d0", "w24.d1", "w25.d0", "w25.d1",
    "w26.d0", "w26.d1", "w27.d0", "w27.d1",
    "w28.d0", "w28.d1", "w29.d0", "w29.d1",
    "w30.d0", "w30.d1", "w31.d0", "w31.d1",
};

/* Encoding of Operation Field */
static const struct dfe {
    enum CPUMIPSMSADataFormat df;
    int start;
    int length;
    uint32_t value;
} df_elm[] = {
    /* Table 3.26 ELM Instruction Format */
    {DF_BYTE,   4, 2, 0b00},
    {DF_HALF,   3, 3, 0b100},
    {DF_WORD,   2, 4, 0b1100},
    {DF_DOUBLE, 1, 5, 0b11100}
}, df_bit[] = {
    /* Table 3.28 BIT Instruction Format */
    {DF_BYTE,   3, 4, 0b1110},
    {DF_HALF,   4, 3, 0b110},
    {DF_WORD,   5, 2, 0b10},
    {DF_DOUBLE, 6, 1, 0b0}
};

/* Extract Operation Field (used by ELM & BIT instructions) */
static bool df_extract(const struct dfe *s, int value,
                       enum CPUMIPSMSADataFormat *df, uint32_t *x)
{
    for (unsigned i = 0; i < 4; i++) {
        if (extract32(value, s->start, s->length) == s->value) {
            *x = extract32(value, 0, s->start);
            *df = s->df;
            return true;
        }
    }
    return false;
}

static TCGv_i64 msa_wr_d[64];

void msa_translate_init(void)
{
    int i;

    for (i = 0; i < 32; i++) {
        int off = offsetof(CPUMIPSState, active_fpu.fpr[i].wr.d[0]);

        /*
         * The MSA vector registers are mapped on the
         * scalar floating-point unit (FPU) registers.
         */
        msa_wr_d[i * 2] = fpu_f64[i];
        off = offsetof(CPUMIPSState, active_fpu.fpr[i].wr.d[1]);
        msa_wr_d[i * 2 + 1] =
                tcg_global_mem_new_i64(cpu_env, off, msaregnames[i * 2 + 1]);
    }
}

static inline bool check_msa_access(DisasContext *ctx)
{
    if (unlikely((ctx->hflags & MIPS_HFLAG_FPU) &&
                 !(ctx->hflags & MIPS_HFLAG_F64))) {
        gen_reserved_instruction(ctx);
        return false;
    }

    if (unlikely(!(ctx->hflags & MIPS_HFLAG_MSA))) {
        generate_exception_end(ctx, EXCP_MSADIS);
        return false;
    }
    return true;
}

#define TRANS_MSA(NAME, trans_func, gen_func) \
        TRANS_CHECK(NAME, check_msa_access(ctx), trans_func, gen_func)

#define TRANS_DF(NAME, trans_func, df, gen_func) \
        TRANS_CHECK(NAME, check_msa_access(ctx), trans_func, df, gen_func)

#define TRANS_DF_E(NAME, trans_func, gen_func) \
        TRANS_CHECK(NAME, check_msa_access(ctx), trans_func, \
                    gen_func##_b, gen_func##_h, gen_func##_w, gen_func##_d)

#define TRANS_DF_B(NAME, trans_func, gen_func) \
        TRANS_CHECK(NAME, check_msa_access(ctx), trans_func, \
                    NULL, gen_func##_h, gen_func##_w, gen_func##_d)

#define TRANS_DF_D64(NAME, trans_func, gen_func) \
        TRANS_CHECK(NAME, check_msa_access(ctx), trans_func, \
                    DF_WORD, DF_DOUBLE, \
                    gen_func##_b, gen_func##_h, gen_func##_w, gen_func##_d)

#define TRANS_DF_W64(NAME, trans_func, gen_func) \
        TRANS_CHECK(NAME, check_msa_access(ctx), trans_func, \
                    DF_HALF, DF_WORD, \
                    gen_func##_b, gen_func##_h, gen_func##_w, NULL)

static void gen_check_zero_element(TCGv tresult, uint8_t df, uint8_t wt,
                                   TCGCond cond)
{
    /* generates tcg ops to check if any element is 0 */
    /* Note this function only works with MSA_WRLEN = 128 */
    uint64_t eval_zero_or_big = 0;
    uint64_t eval_big = 0;
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    switch (df) {
    case DF_BYTE:
        eval_zero_or_big = 0x0101010101010101ULL;
        eval_big = 0x8080808080808080ULL;
        break;
    case DF_HALF:
        eval_zero_or_big = 0x0001000100010001ULL;
        eval_big = 0x8000800080008000ULL;
        break;
    case DF_WORD:
        eval_zero_or_big = 0x0000000100000001ULL;
        eval_big = 0x8000000080000000ULL;
        break;
    case DF_DOUBLE:
        eval_zero_or_big = 0x0000000000000001ULL;
        eval_big = 0x8000000000000000ULL;
        break;
    }
    tcg_gen_subi_i64(t0, msa_wr_d[wt << 1], eval_zero_or_big);
    tcg_gen_andc_i64(t0, t0, msa_wr_d[wt << 1]);
    tcg_gen_andi_i64(t0, t0, eval_big);
    tcg_gen_subi_i64(t1, msa_wr_d[(wt << 1) + 1], eval_zero_or_big);
    tcg_gen_andc_i64(t1, t1, msa_wr_d[(wt << 1) + 1]);
    tcg_gen_andi_i64(t1, t1, eval_big);
    tcg_gen_or_i64(t0, t0, t1);
    /* if all bits are zero then all elements are not zero */
    /* if some bit is non-zero then some element is zero */
    tcg_gen_setcondi_i64(cond, t0, t0, 0);
    tcg_gen_trunc_i64_tl(tresult, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static bool gen_msa_BxZ_V(DisasContext *ctx, int wt, int sa, TCGCond cond)
{
    TCGv_i64 t0;

    if (!check_msa_access(ctx)) {
        return false;
    }

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        gen_reserved_instruction(ctx);
        return true;
    }
    t0 = tcg_temp_new_i64();
    tcg_gen_or_i64(t0, msa_wr_d[wt << 1], msa_wr_d[(wt << 1) + 1]);
    tcg_gen_setcondi_i64(cond, t0, t0, 0);
    tcg_gen_trunc_i64_tl(bcond, t0);
    tcg_temp_free_i64(t0);

    ctx->btarget = ctx->base.pc_next + (sa << 2) + 4;

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->hflags |= MIPS_HFLAG_BDS32;

    return true;
}

static bool trans_BZ_V(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ_V(ctx, a->wt, a->sa, TCG_COND_EQ);
}

static bool trans_BNZ_V(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ_V(ctx, a->wt, a->sa, TCG_COND_NE);
}

static bool gen_msa_BxZ(DisasContext *ctx, int df, int wt, int sa, bool if_not)
{
    if (!check_msa_access(ctx)) {
        return false;
    }

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        gen_reserved_instruction(ctx);
        return true;
    }

    gen_check_zero_element(bcond, df, wt, if_not ? TCG_COND_EQ : TCG_COND_NE);

    ctx->btarget = ctx->base.pc_next + (sa << 2) + 4;
    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->hflags |= MIPS_HFLAG_BDS32;

    return true;
}

static bool trans_BZ(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ(ctx, a->df, a->wt, a->sa, false);
}

static bool trans_BNZ(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ(ctx, a->df, a->wt, a->sa, true);
}

static bool trans_msa_i8(DisasContext *ctx, arg_msa_ldst *a,
                         void (*gen_msa_i8)(TCGv_ptr, TCGv_i32,
                                            TCGv_i32, TCGv_i32))
{
    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv_i32 tws = tcg_const_i32(a->ws);
    TCGv_i32 timm = tcg_const_i32(a->sa);

    gen_msa_i8(cpu_env, twd, tws, timm);

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(timm);

    return true;
}

TRANS_MSA(ANDI,     trans_msa_i8, gen_helper_msa_andi_b);
TRANS_MSA(ORI,      trans_msa_i8, gen_helper_msa_ori_b);
TRANS_MSA(NORI,     trans_msa_i8, gen_helper_msa_nori_b);
TRANS_MSA(XORI,     trans_msa_i8, gen_helper_msa_xori_b);
TRANS_MSA(BMNZI,    trans_msa_i8, gen_helper_msa_bmnzi_b);
TRANS_MSA(BMZI,     trans_msa_i8, gen_helper_msa_bmzi_b);
TRANS_MSA(BSELI,    trans_msa_i8, gen_helper_msa_bseli_b);

static bool trans_SHF(DisasContext *ctx, arg_msa_ldst *a)
{
    TCGv_i32 tdf;
    TCGv_i32 twd;
    TCGv_i32 tws;
    TCGv_i32 timm;

    if (a->df == DF_DOUBLE) {
        gen_reserved_instruction(ctx);
        return true;
    }

    if (!check_msa_access(ctx)) {
        return false;
    }

    tdf = tcg_constant_i32(a->df);
    twd = tcg_const_i32(a->wd);
    tws = tcg_const_i32(a->ws);
    timm = tcg_const_i32(a->sa);

    gen_helper_msa_shf_df(cpu_env, tdf, twd, tws, timm);

    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(timm);

    return true;
}

static bool trans_msa_i5(DisasContext *ctx, arg_msa_ldst *a,
                         void (*gen_msa_i5)(TCGv_ptr, TCGv_i32, TCGv_i32,
                                            TCGv_i32, TCGv_i32))
{
    TCGv_i32 tdf = tcg_constant_i32(a->df);
    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv_i32 tws = tcg_const_i32(a->ws);
    TCGv_i32 timm = tcg_const_i32(a->sa);

    gen_msa_i5(cpu_env, tdf, twd, tws, timm);

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(timm);

    return true;
}

TRANS_MSA(ADDVI,    trans_msa_i5, gen_helper_msa_addvi_df);
TRANS_MSA(SUBVI,    trans_msa_i5, gen_helper_msa_subvi_df);
TRANS_MSA(MAXI_S,   trans_msa_i5, gen_helper_msa_maxi_s_df);
TRANS_MSA(MAXI_U,   trans_msa_i5, gen_helper_msa_maxi_u_df);
TRANS_MSA(MINI_S,   trans_msa_i5, gen_helper_msa_mini_s_df);
TRANS_MSA(MINI_U,   trans_msa_i5, gen_helper_msa_mini_u_df);
TRANS_MSA(CLTI_S,   trans_msa_i5, gen_helper_msa_clti_s_df);
TRANS_MSA(CLTI_U,   trans_msa_i5, gen_helper_msa_clti_u_df);
TRANS_MSA(CLEI_S,   trans_msa_i5, gen_helper_msa_clei_s_df);
TRANS_MSA(CLEI_U,   trans_msa_i5, gen_helper_msa_clei_u_df);
TRANS_MSA(CEQI,     trans_msa_i5, gen_helper_msa_ceqi_df);

static bool trans_LDI(DisasContext *ctx, arg_msa_ldst *a)
{
    TCGv_i32 tdf;
    TCGv_i32 twd;
    TCGv_i32 timm;

    if (!check_msa_access(ctx)) {
        return false;
    }

    tdf = tcg_constant_i32(a->df);
    twd = tcg_const_i32(a->wd);
    timm = tcg_const_i32(a->sa);

    gen_helper_msa_ldi_df(cpu_env, tdf, twd, timm);

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(timm);

    return true;
}

static bool trans_msa_bit(DisasContext *ctx, arg_msa_ldst *a,
                          void (*gen_msa_bit)(TCGv_ptr, TCGv_i32, TCGv_i32,
                                              TCGv_i32, TCGv_i32))
{
    TCGv_i32 tdf;
    TCGv_i32 tm;
    TCGv_i32 twd;
    TCGv_i32 tws;
    uint32_t df, m;

    if (!df_extract(df_bit, a->df, &df, &m)) {
        gen_reserved_instruction(ctx);
        return true;
    }

    tdf = tcg_constant_i32(df);
    tm  = tcg_const_i32(m);
    twd = tcg_const_i32(a->wd);
    tws = tcg_const_i32(a->ws);

    gen_msa_bit(cpu_env, tdf, twd, tws, tm);

    tcg_temp_free_i32(tm);
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);

    return true;
}

TRANS_MSA(SLLI,     trans_msa_bit, gen_helper_msa_slli_df);
TRANS_MSA(SRAI,     trans_msa_bit, gen_helper_msa_srai_df);
TRANS_MSA(SRLI,     trans_msa_bit, gen_helper_msa_srli_df);
TRANS_MSA(BCLRI,    trans_msa_bit, gen_helper_msa_bclri_df);
TRANS_MSA(BSETI,    trans_msa_bit, gen_helper_msa_bseti_df);
TRANS_MSA(BNEGI,    trans_msa_bit, gen_helper_msa_bnegi_df);
TRANS_MSA(BINSLI,   trans_msa_bit, gen_helper_msa_binsli_df);
TRANS_MSA(BINSRI,   trans_msa_bit, gen_helper_msa_binsri_df);
TRANS_MSA(SAT_S,    trans_msa_bit, gen_helper_msa_sat_u_df);
TRANS_MSA(SAT_U,    trans_msa_bit, gen_helper_msa_sat_u_df);
TRANS_MSA(SRARI,    trans_msa_bit, gen_helper_msa_srari_df);
TRANS_MSA(SRLRI,    trans_msa_bit, gen_helper_msa_srlri_df);

static bool trans_msa_3r_df(DisasContext *ctx, arg_msa_r *a,
                            void (*gen_msa_3r_df)(TCGv_ptr, TCGv_i32, TCGv_i32,
                                                  TCGv_i32, TCGv_i32))
{
    TCGv_i32 tdf = tcg_constant_i32(a->df);
    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv_i32 tws = tcg_const_i32(a->ws);
    TCGv_i32 twt = tcg_const_i32(a->wt);

    gen_msa_3r_df(cpu_env, tdf, twd, tws, twt);

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);

    return true;
}

static bool trans_msa_3r(DisasContext *ctx, arg_msa_r *a,
                         void (*gen_msa_3r_b)(TCGv_ptr, TCGv_i32,
                                              TCGv_i32, TCGv_i32),
                         void (*gen_msa_3r_h)(TCGv_ptr, TCGv_i32,
                                              TCGv_i32, TCGv_i32),
                         void (*gen_msa_3r_w)(TCGv_ptr, TCGv_i32,
                                              TCGv_i32, TCGv_i32),
                         void (*gen_msa_3r_d)(TCGv_ptr, TCGv_i32,
                                              TCGv_i32, TCGv_i32))
{
    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv_i32 tws = tcg_const_i32(a->ws);
    TCGv_i32 twt = tcg_const_i32(a->wt);

    switch (a->df) {
    case DF_BYTE:
        if (gen_msa_3r_b == NULL) {
            gen_reserved_instruction(ctx);
        } else {
            gen_msa_3r_b(cpu_env, twd, tws, twt);
        }
        break;
    case DF_HALF:
        gen_msa_3r_h(cpu_env, twd, tws, twt);
        break;
    case DF_WORD:
        gen_msa_3r_w(cpu_env, twd, tws, twt);
        break;
    case DF_DOUBLE:
        gen_msa_3r_d(cpu_env, twd, tws, twt);
        break;
    }

    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twd);

    return true;
}

TRANS_DF_E(SLL,         trans_msa_3r,    gen_helper_msa_sll);
TRANS_DF_E(SRA,         trans_msa_3r,    gen_helper_msa_sra);
TRANS_DF_E(SRL,         trans_msa_3r,    gen_helper_msa_srl);
TRANS_DF_E(BCLR,        trans_msa_3r,    gen_helper_msa_bclr);
TRANS_DF_E(BSET,        trans_msa_3r,    gen_helper_msa_bset);
TRANS_DF_E(BNEG,        trans_msa_3r,    gen_helper_msa_bneg);
TRANS_DF_E(BINSL,       trans_msa_3r,    gen_helper_msa_binsl);
TRANS_DF_E(BINSR,       trans_msa_3r,    gen_helper_msa_binsr);

TRANS_DF_E(ADDV,        trans_msa_3r,    gen_helper_msa_addv);
TRANS_DF_E(SUBV,        trans_msa_3r,    gen_helper_msa_subv);
TRANS_DF_E(MAX_S,       trans_msa_3r,    gen_helper_msa_max_s);
TRANS_DF_E(MAX_U,       trans_msa_3r,    gen_helper_msa_max_u);
TRANS_DF_E(MIN_S,       trans_msa_3r,    gen_helper_msa_min_s);
TRANS_DF_E(MIN_U,       trans_msa_3r,    gen_helper_msa_min_u);
TRANS_DF_E(MAX_A,       trans_msa_3r,    gen_helper_msa_max_a);
TRANS_DF_E(MIN_A,       trans_msa_3r,    gen_helper_msa_min_a);

TRANS_DF_E(CEQ,         trans_msa_3r,    gen_helper_msa_ceq);
TRANS_DF_E(CLT_S,       trans_msa_3r,    gen_helper_msa_clt_s);
TRANS_DF_E(CLT_U,       trans_msa_3r,    gen_helper_msa_clt_u);
TRANS_DF_E(CLE_S,       trans_msa_3r,    gen_helper_msa_cle_s);
TRANS_DF_E(CLE_U,       trans_msa_3r,    gen_helper_msa_cle_u);

TRANS_DF_E(ADD_A,       trans_msa_3r,    gen_helper_msa_add_a);
TRANS_DF_E(ADDS_A,      trans_msa_3r,    gen_helper_msa_adds_a);
TRANS_DF_E(ADDS_S,      trans_msa_3r,    gen_helper_msa_adds_s);
TRANS_DF_E(ADDS_U,      trans_msa_3r,    gen_helper_msa_adds_u);
TRANS_DF_E(AVE_S,       trans_msa_3r,    gen_helper_msa_ave_s);
TRANS_DF_E(AVE_U,       trans_msa_3r,    gen_helper_msa_ave_u);
TRANS_DF_E(AVER_S,      trans_msa_3r,    gen_helper_msa_aver_s);
TRANS_DF_E(AVER_U,      trans_msa_3r,    gen_helper_msa_aver_u);

TRANS_DF_E(SUBS_S,      trans_msa_3r,    gen_helper_msa_subs_s);
TRANS_DF_E(SUBS_U,      trans_msa_3r,    gen_helper_msa_subs_u);
TRANS_DF_E(SUBSUS_U,    trans_msa_3r,    gen_helper_msa_subsus_u);
TRANS_DF_E(SUBSUU_S,    trans_msa_3r,    gen_helper_msa_subsuu_s);
TRANS_DF_E(ASUB_S,      trans_msa_3r,    gen_helper_msa_asub_s);
TRANS_DF_E(ASUB_U,      trans_msa_3r,    gen_helper_msa_asub_u);

TRANS_DF_E(MULV,        trans_msa_3r,    gen_helper_msa_mulv);
TRANS_DF_E(MADDV,       trans_msa_3r,    gen_helper_msa_maddv);
TRANS_DF_E(MSUBV,       trans_msa_3r,    gen_helper_msa_msubv);
TRANS_DF_E(DIV_S,       trans_msa_3r,    gen_helper_msa_div_s);
TRANS_DF_E(DIV_U,       trans_msa_3r,    gen_helper_msa_div_u);
TRANS_DF_E(MOD_S,       trans_msa_3r,    gen_helper_msa_mod_s);
TRANS_DF_E(MOD_U,       trans_msa_3r,    gen_helper_msa_mod_u);

TRANS_DF_B(DOTP_S,      trans_msa_3r,    gen_helper_msa_dotp_s);
TRANS_DF_B(DOTP_U,      trans_msa_3r,    gen_helper_msa_dotp_u);
TRANS_DF_B(DPADD_S,     trans_msa_3r,    gen_helper_msa_dpadd_s);
TRANS_DF_B(DPADD_U,     trans_msa_3r,    gen_helper_msa_dpadd_u);
TRANS_DF_B(DPSUB_S,     trans_msa_3r,    gen_helper_msa_dpsub_s);
TRANS_DF_B(DPSUB_U,     trans_msa_3r,    gen_helper_msa_dpsub_u);

TRANS_MSA(SLD,          trans_msa_3r_df, gen_helper_msa_sld_df);
TRANS_MSA(SPLAT,        trans_msa_3r_df, gen_helper_msa_splat_df);
TRANS_DF_E(PCKEV,       trans_msa_3r,    gen_helper_msa_pckev);
TRANS_DF_E(PCKOD,       trans_msa_3r,    gen_helper_msa_pckod);
TRANS_DF_E(ILVL,        trans_msa_3r,    gen_helper_msa_ilvl);
TRANS_DF_E(ILVR,        trans_msa_3r,    gen_helper_msa_ilvr);
TRANS_DF_E(ILVEV,       trans_msa_3r,    gen_helper_msa_ilvev);
TRANS_DF_E(ILVOD,       trans_msa_3r,    gen_helper_msa_ilvod);

TRANS_MSA(VSHF,         trans_msa_3r_df, gen_helper_msa_vshf_df);
TRANS_DF_E(SRAR,        trans_msa_3r,    gen_helper_msa_srar);
TRANS_DF_E(SRLR,        trans_msa_3r,    gen_helper_msa_srlr);
TRANS_DF_B(HADD_S,      trans_msa_3r,    gen_helper_msa_hadd_s);
TRANS_DF_B(HADD_U,      trans_msa_3r,    gen_helper_msa_hadd_u);
TRANS_DF_B(HSUB_S,      trans_msa_3r,    gen_helper_msa_hsub_s);
TRANS_DF_B(HSUB_U,      trans_msa_3r,    gen_helper_msa_hsub_u);

static bool trans_MOVE_V(DisasContext *ctx, arg_msa_elm *a)
{
    TCGv_i32 tsr;
    TCGv_i32 tdt;

    if (!check_msa_access(ctx)) {
        return false;
    }

    tsr = tcg_const_i32(a->ws);
    tdt = tcg_const_i32(a->wd);

    gen_helper_msa_move_v(cpu_env, tdt, tsr);

    tcg_temp_free_i32(tdt);
    tcg_temp_free_i32(tsr);

    return true;
}

static bool trans_CTCMSA(DisasContext *ctx, arg_msa_elm *a)
{
    TCGv telm;
    TCGv_i32 tdt;

    if (!check_msa_access(ctx)) {
        return false;
    }

    telm = tcg_temp_new();
    tdt = tcg_const_i32(a->wd);

    gen_load_gpr(telm, a->ws);
    gen_helper_msa_ctcmsa(cpu_env, telm, tdt);

    tcg_temp_free(telm);
    tcg_temp_free_i32(tdt);

    return true;
}

static bool trans_CFCMSA(DisasContext *ctx, arg_msa_elm *a)
{
    TCGv telm;
    TCGv_i32 tsr;

    if (!check_msa_access(ctx)) {
        return false;
    }

    telm = tcg_temp_new();
    tsr = tcg_const_i32(a->ws);

    gen_helper_msa_cfcmsa(telm, cpu_env, tsr);
    gen_store_gpr(telm, a->wd);

    tcg_temp_free(telm);
    tcg_temp_free_i32(tsr);

    return true;
}

static bool trans_msa_elm_df(DisasContext *ctx, arg_msa_elm *a,
                             void (*gen_msa_elm_df)(TCGv_ptr, TCGv_i32,
                                                    TCGv_i32, TCGv_i32,
                                                    TCGv_i32))
{
    TCGv_i32 twd;
    TCGv_i32 tws;
    TCGv_i32 tdf;
    TCGv_i32 tn;
    uint32_t df, n;

    if (!df_extract(df_elm, a->df, &df, &n)) {
        gen_reserved_instruction(ctx);
        return true;
    }

    twd = tcg_const_i32(a->wd);
    tws = tcg_const_i32(a->ws);
    tdf = tcg_constant_i32(df);
    tn = tcg_constant_i32(n);

    gen_msa_elm_df(cpu_env, tdf, twd, tws, tn);

    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twd);

    return true;
}

TRANS_MSA(SLDI,     trans_msa_elm_df, gen_helper_msa_sldi_df);
TRANS_MSA(SPLATI,   trans_msa_elm_df, gen_helper_msa_splati_df);
TRANS_MSA(INSVE,    trans_msa_elm_df, gen_helper_msa_insve_df);

static bool trans_msa_elm_d64(DisasContext *ctx, arg_msa_elm *a,
                              enum CPUMIPSMSADataFormat df_max32,
                              enum CPUMIPSMSADataFormat df_max64,
                              void (*gen_msa_elm_b)(TCGv_ptr, TCGv_i32,
                                                    TCGv_i32, TCGv_i32),
                              void (*gen_msa_elm_h)(TCGv_ptr, TCGv_i32,
                                                    TCGv_i32, TCGv_i32),
                              void (*gen_msa_elm_w)(TCGv_ptr, TCGv_i32,
                                                    TCGv_i32, TCGv_i32),
                              void (*gen_msa_elm_d)(TCGv_ptr, TCGv_i32,
                                                    TCGv_i32, TCGv_i32))
{
    TCGv_i32 twd;
    TCGv_i32 tws;
    TCGv_i32 tn;
    uint32_t df, n;

    if (!df_extract(df_elm, a->df, &df, &n)) {
        gen_reserved_instruction(ctx);
        return true;
    }

    if (df > (TARGET_LONG_BITS == 64 ? df_max64 : df_max32)) {
        gen_reserved_instruction(ctx);
        return true;
    }

    if (a->wd == 0) {
        /* Treat as NOP. */
        return true;
    }

    twd = tcg_const_i32(a->wd);
    tws = tcg_const_i32(a->ws);
    tn = tcg_constant_i32(n);

    switch (a->df) {
    case DF_BYTE:
        gen_msa_elm_b(cpu_env, twd, tws, tn);
        break;
    case DF_HALF:
        gen_msa_elm_h(cpu_env, twd, tws, tn);
        break;
    case DF_WORD:
        gen_msa_elm_w(cpu_env, twd, tws, tn);
        break;
    case DF_DOUBLE:
        assert(gen_msa_elm_d != NULL);
        gen_msa_elm_d(cpu_env, twd, tws, tn);
        break;
    }

    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twd);

    return true;
}

TRANS_DF_D64(COPY_S,    trans_msa_elm_d64, gen_helper_msa_copy_s);
TRANS_DF_W64(COPY_U,    trans_msa_elm_d64, gen_helper_msa_copy_u);
TRANS_DF_D64(INSERT,    trans_msa_elm_d64, gen_helper_msa_insert);

static bool trans_msa_3rf(DisasContext *ctx, arg_msa_r *a,
                          enum CPUMIPSMSADataFormat df_base,
                          void (*gen_msa_3rf)(TCGv_ptr, TCGv_i32, TCGv_i32,
                                              TCGv_i32, TCGv_i32))
{
    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv_i32 tws = tcg_const_i32(a->ws);
    TCGv_i32 twt = tcg_const_i32(a->wt);
    /* adjust df value for floating-point instruction */
    TCGv_i32 tdf = tcg_constant_i32(a->df + df_base);

    gen_msa_3rf(cpu_env, tdf, twd, tws, twt);

    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twd);

    return true;
}

TRANS_DF(FCAF,      trans_msa_3rf, DF_WORD, gen_helper_msa_fcaf_df);
TRANS_DF(FCUN,      trans_msa_3rf, DF_WORD, gen_helper_msa_fcun_df);
TRANS_DF(FCEQ,      trans_msa_3rf, DF_WORD, gen_helper_msa_fceq_df);
TRANS_DF(FCUEQ,     trans_msa_3rf, DF_WORD, gen_helper_msa_fcueq_df);
TRANS_DF(FCLT,      trans_msa_3rf, DF_WORD, gen_helper_msa_fclt_df);
TRANS_DF(FCULT,     trans_msa_3rf, DF_WORD, gen_helper_msa_fcult_df);
TRANS_DF(FCLE,      trans_msa_3rf, DF_WORD, gen_helper_msa_fcle_df);
TRANS_DF(FCULE,     trans_msa_3rf, DF_WORD, gen_helper_msa_fcule_df);
TRANS_DF(FSAF,      trans_msa_3rf, DF_WORD, gen_helper_msa_fsaf_df);
TRANS_DF(FSUN,      trans_msa_3rf, DF_WORD, gen_helper_msa_fsun_df);
TRANS_DF(FSEQ,      trans_msa_3rf, DF_WORD, gen_helper_msa_fseq_df);
TRANS_DF(FSUEQ,     trans_msa_3rf, DF_WORD, gen_helper_msa_fsueq_df);
TRANS_DF(FSLT,      trans_msa_3rf, DF_WORD, gen_helper_msa_fslt_df);
TRANS_DF(FSULT,     trans_msa_3rf, DF_WORD, gen_helper_msa_fsult_df);
TRANS_DF(FSLE,      trans_msa_3rf, DF_WORD, gen_helper_msa_fsle_df);
TRANS_DF(FSULE,     trans_msa_3rf, DF_WORD, gen_helper_msa_fsule_df);

TRANS_DF(FADD,      trans_msa_3rf, DF_WORD, gen_helper_msa_fadd_df);
TRANS_DF(FSUB,      trans_msa_3rf, DF_WORD, gen_helper_msa_fsub_df);
TRANS_DF(FMUL,      trans_msa_3rf, DF_WORD, gen_helper_msa_fmul_df);
TRANS_DF(FDIV,      trans_msa_3rf, DF_WORD, gen_helper_msa_fdiv_df);
TRANS_DF(FMADD,     trans_msa_3rf, DF_WORD, gen_helper_msa_fmadd_df);
TRANS_DF(FMSUB,     trans_msa_3rf, DF_WORD, gen_helper_msa_fmsub_df);
TRANS_DF(FEXP2,     trans_msa_3rf, DF_WORD, gen_helper_msa_fexp2_df);
TRANS_DF(FEXDO,     trans_msa_3rf, DF_WORD, gen_helper_msa_fexdo_df);
TRANS_DF(FTQ,       trans_msa_3rf, DF_WORD, gen_helper_msa_ftq_df);
TRANS_DF(FMIN,      trans_msa_3rf, DF_WORD, gen_helper_msa_fmin_df);
TRANS_DF(FMIN_A,    trans_msa_3rf, DF_WORD, gen_helper_msa_fmin_a_df);
TRANS_DF(FMAX,      trans_msa_3rf, DF_WORD, gen_helper_msa_fmax_df);
TRANS_DF(FMAX_A,    trans_msa_3rf, DF_WORD, gen_helper_msa_fmax_a_df);

TRANS_DF(FCOR,      trans_msa_3rf, DF_WORD, gen_helper_msa_fcor_df);
TRANS_DF(FCUNE,     trans_msa_3rf, DF_WORD, gen_helper_msa_fcune_df);
TRANS_DF(FCNE,      trans_msa_3rf, DF_WORD, gen_helper_msa_fcne_df);
TRANS_DF(MUL_Q,     trans_msa_3rf, DF_HALF, gen_helper_msa_mul_q_df);
TRANS_DF(MADD_Q,    trans_msa_3rf, DF_HALF, gen_helper_msa_madd_q_df);
TRANS_DF(MSUB_Q,    trans_msa_3rf, DF_HALF, gen_helper_msa_msub_q_df);
TRANS_DF(FSOR,      trans_msa_3rf, DF_WORD, gen_helper_msa_fsor_df);
TRANS_DF(FSUNE,     trans_msa_3rf, DF_WORD, gen_helper_msa_fsune_df);
TRANS_DF(FSNE,      trans_msa_3rf, DF_WORD, gen_helper_msa_fsne_df);
TRANS_DF(MULR_Q,    trans_msa_3rf, DF_HALF, gen_helper_msa_mulr_q_df);
TRANS_DF(MADDR_Q,   trans_msa_3rf, DF_HALF, gen_helper_msa_maddr_q_df);
TRANS_DF(MSUBR_Q,   trans_msa_3rf, DF_HALF, gen_helper_msa_msubr_q_df);

static bool trans_msa_2r(DisasContext *ctx, arg_msa_r *a,
                         void (*gen_msa_2r_b)(TCGv_ptr, TCGv_i32, TCGv_i32),
                         void (*gen_msa_2r_h)(TCGv_ptr, TCGv_i32, TCGv_i32),
                         void (*gen_msa_2r_w)(TCGv_ptr, TCGv_i32, TCGv_i32),
                         void (*gen_msa_2r_d)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv_i32 tws = tcg_const_i32(a->ws);

    switch (a->df) {
    case DF_BYTE:
        if (gen_msa_2r_b == NULL) {
            gen_reserved_instruction(ctx);
        } else {
            gen_msa_2r_b(cpu_env, twd, tws);
        }
        break;
    case DF_HALF:
        gen_msa_2r_h(cpu_env, twd, tws);
        break;
    case DF_WORD:
        gen_msa_2r_w(cpu_env, twd, tws);
        break;
    case DF_DOUBLE:
        gen_msa_2r_d(cpu_env, twd, tws);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);

    return true;
}

TRANS_DF_E(PCNT, trans_msa_2r, gen_helper_msa_pcnt);
TRANS_DF_E(NLOC, trans_msa_2r, gen_helper_msa_nloc);
TRANS_DF_E(NLZC, trans_msa_2r, gen_helper_msa_nlzc);

static bool trans_FILL(DisasContext *ctx, arg_msa_r *a)
{
    TCGv_i32 twd;
    TCGv_i32 tws;
    TCGv_i32 tdf;

    if (!check_msa_access(ctx)) {
        return false;
    }

    if (TARGET_LONG_BITS != 64 && a->df == DF_DOUBLE) {
        /* Double format valid only for MIPS64 */
        gen_reserved_instruction(ctx);
        return true;
    }

    twd = tcg_const_i32(a->wd);
    tws = tcg_const_i32(a->ws);
    tdf = tcg_constant_i32(a->df);

    gen_helper_msa_fill_df(cpu_env, tdf, twd, tws); /* trs */

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);

    return true;
}

static bool trans_msa_2rf(DisasContext *ctx, arg_msa_r *a,
                          void (*gen_msa_2rf)(TCGv_ptr, TCGv_i32,
                                              TCGv_i32, TCGv_i32))
{
    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv_i32 tws = tcg_const_i32(a->ws);
    /* adjust df value for floating-point instruction */
    TCGv_i32 tdf = tcg_constant_i32(DF_WORD + a->df);

    gen_msa_2rf(cpu_env, tdf, twd, tws);

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);

    return true;
}

TRANS_MSA(FCLASS,   trans_msa_2rf, gen_helper_msa_fclass_df);
TRANS_MSA(FTRUNC_S, trans_msa_2rf, gen_helper_msa_fclass_df);
TRANS_MSA(FTRUNC_U, trans_msa_2rf, gen_helper_msa_ftrunc_s_df);
TRANS_MSA(FSQRT,    trans_msa_2rf, gen_helper_msa_fsqrt_df);
TRANS_MSA(FRSQRT,   trans_msa_2rf, gen_helper_msa_frsqrt_df);
TRANS_MSA(FRCP,     trans_msa_2rf, gen_helper_msa_frcp_df);
TRANS_MSA(FRINT,    trans_msa_2rf, gen_helper_msa_frint_df);
TRANS_MSA(FLOG2,    trans_msa_2rf, gen_helper_msa_flog2_df);
TRANS_MSA(FEXUPL,   trans_msa_2rf, gen_helper_msa_fexupl_df);
TRANS_MSA(FEXUPR,   trans_msa_2rf, gen_helper_msa_fexupr_df);
TRANS_MSA(FFQL,     trans_msa_2rf, gen_helper_msa_ffql_df);
TRANS_MSA(FFQR,     trans_msa_2rf, gen_helper_msa_ffqr_df);
TRANS_MSA(FTINT_S,  trans_msa_2rf, gen_helper_msa_ftint_s_df);
TRANS_MSA(FTINT_U,  trans_msa_2rf, gen_helper_msa_ftint_u_df);
TRANS_MSA(FFINT_S,  trans_msa_2rf, gen_helper_msa_ffint_s_df);
TRANS_MSA(FFINT_U,  trans_msa_2rf, gen_helper_msa_ffint_u_df);

static bool trans_msa_vec(DisasContext *ctx, arg_msa_r *a,
                          void (*gen_msa_vec)(TCGv_ptr, TCGv_i32,
                                              TCGv_i32, TCGv_i32))
{
    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv_i32 tws = tcg_const_i32(a->ws);
    TCGv_i32 twt = tcg_const_i32(a->wt);

    gen_msa_vec(cpu_env, twd, tws, twt);

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);

    return true;
}

TRANS_MSA(AND_V,    trans_msa_vec, gen_helper_msa_and_v);
TRANS_MSA(OR_V,     trans_msa_vec, gen_helper_msa_or_v);
TRANS_MSA(NOR_V,    trans_msa_vec, gen_helper_msa_nor_v);
TRANS_MSA(XOR_V,    trans_msa_vec, gen_helper_msa_xor_v);
TRANS_MSA(BMNZ_V,   trans_msa_vec, gen_helper_msa_bmnz_v);
TRANS_MSA(BMZ_V,    trans_msa_vec, gen_helper_msa_bmz_v);
TRANS_MSA(BSEL_V,   trans_msa_vec, gen_helper_msa_bsel_v);

static bool trans_msa_ldst(DisasContext *ctx, arg_msa_ldst *a,
                           void (*gen_msa_b)(TCGv_ptr, TCGv_i32, TCGv),
                           void (*gen_msa_h)(TCGv_ptr, TCGv_i32, TCGv),
                           void (*gen_msa_w)(TCGv_ptr, TCGv_i32, TCGv),
                           void (*gen_msa_d)(TCGv_ptr, TCGv_i32, TCGv))
{

    TCGv_i32 twd = tcg_const_i32(a->wd);
    TCGv taddr = tcg_temp_new();

    gen_base_offset_addr(ctx, taddr, a->ws, a->sa << a->df);

    switch (a->df) {
    case DF_BYTE:
        gen_msa_b(cpu_env, twd, taddr);
        break;
    case DF_HALF:
        gen_msa_h(cpu_env, twd, taddr);
        break;
    case DF_WORD:
        gen_msa_w(cpu_env, twd, taddr);
        break;
    case DF_DOUBLE:
        gen_msa_d(cpu_env, twd, taddr);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free(taddr);

    return true;
}

TRANS_DF_E(LD, trans_msa_ldst, gen_helper_msa_ld);
TRANS_DF_E(ST, trans_msa_ldst, gen_helper_msa_st);

static bool trans_LSA(DisasContext *ctx, arg_r *a)
{
    return gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);
}

static bool trans_DLSA(DisasContext *ctx, arg_r *a)
{
    if (TARGET_LONG_BITS != 64) {
        return false;
    }
    return gen_dlsa(ctx, a->rd, a->rt, a->rs, a->sa);
}
