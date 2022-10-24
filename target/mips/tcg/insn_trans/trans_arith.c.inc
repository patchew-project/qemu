static bool gen_arith_notrap(DisasContext *ctx, arg_r *a,
                             DisasExtend ext, void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs, ext);
    TCGv src2 = get_gpr(ctx, a->rt, ext);

    func(dest, src1, src2);
    gen_set_gpr(a->rd, dest, ext);

    return true;
}

static bool gen_add(DisasContext *ctx, arg_r *a, DisasExtend ext)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = get_gpr(ctx, a->rs, ext);
    TCGv t2 = get_gpr(ctx, a->rt, ext);
    TCGLabel *l1 = gen_new_label();

    tcg_gen_add_tl(t0, t1, t2);
    gen_extend(t0, t0, ext);
    tcg_gen_xor_tl(t1, t1, t2);
    tcg_gen_xor_tl(t2, t0, t2);
    tcg_gen_andc_tl(t1, t2, t1);
    tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
    /* operands of same sign, result different sign */
    generate_exception(ctx, EXCP_OVERFLOW);
    gen_set_label(l1);
    gen_store_gpr(t0, a->rd);
    tcg_temp_free(t0);

    return true;
}

static bool gen_sub(DisasContext *ctx, arg_r *a, DisasExtend ext)
{
    TCGv src1 = get_gpr(ctx, a->rs, ext);
    TCGv src2 = get_gpr(ctx, a->rt, ext);
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGv t2 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_sub_tl(t0, src1, src2);
    gen_extend(t0, t0, ext);
    tcg_gen_xor_tl(t2, src1, src2);
    tcg_gen_xor_tl(t1, t0, src1);
    tcg_gen_and_tl(t1, t1, t2);
    tcg_temp_free(t2);
    tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
    tcg_temp_free(t1);
    /*
     * operands of different sign, first operand and the result
     * of different sign
     */
    generate_exception(ctx, EXCP_OVERFLOW);
    gen_set_label(l1);
    gen_store_gpr(t0, a->rd);
    tcg_temp_free(t0);

    return true;
}

static bool gen_arith_imm_notrap(DisasContext *ctx, arg_i *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, target_long))
{
    TCGv dest = dest_gpr(ctx, a->rt);
    TCGv src1 = get_gpr(ctx, a->rs, ext);

    func(dest, src1, a->imm);
    gen_set_gpr(a->rt, dest, ext);

    return true;
}

static bool gen_add_imm(DisasContext *ctx, arg_i *a, DisasExtend ext)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = get_gpr(ctx, a->rs, ext);
    TCGv t2 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    target_ulong uimm = (target_long)a->imm; /* Sign extend to 32/64 bits */

    gen_load_gpr(t1, a->rs);
    tcg_gen_addi_tl(t0, t1, uimm);
    tcg_gen_ext32s_tl(t0, t0);

    tcg_gen_xori_tl(t1, t1, ~uimm);
    tcg_gen_xori_tl(t2, t0, uimm);
    tcg_gen_and_tl(t1, t1, t2);
    tcg_temp_free(t2);
    tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
    /* operands of same sign, result different sign */
    generate_exception(ctx, EXCP_OVERFLOW);
    gen_set_label(l1);
    tcg_gen_ext32s_tl(t0, t0);
    gen_store_gpr(t0, a->rt);
    tcg_temp_free(t0);

    return true;
}

#define DECLEAR_GEN_CL(suffix, arg_type)                                    \
static bool gen_cl_##suffix(DisasContext *ctx, arg_type * a, bool zero)     \
{                                                                           \
    TCGv dest = dest_gpr(ctx, a->rd);                                       \
    TCGv src = get_gpr(ctx, a->rs, EXT_NONE);                               \
    if (!zero) {                                                            \
        tcg_gen_not_tl(dest, src);                                          \
    }                                                                       \
    tcg_gen_ext32u_tl(dest, dest);                                          \
    tcg_gen_clzi_tl(dest, dest, TARGET_LONG_BITS);                          \
    tcg_gen_subi_tl(dest, dest, TARGET_LONG_BITS - 32);                     \
    gen_set_gpr(a->rd, dest, EXT_NONE);                                     \
    return true;                                                            \
}                                                                           \

DECLEAR_GEN_CL(legacy, arg_r)
#undef DECLEAR_GEN_CL

#ifdef TARGET_MIPS64
#define DECLEAR_GEN_DCL(suffix, arg_type)                                   \
static bool gen_dcl_##suffix(DisasContext *ctx, arg_type * a, bool zero)    \
{                                                                           \
    TCGv dest = dest_gpr(ctx, a->rd);                                       \
    TCGv src = get_gpr(ctx, a->rs, EXT_NONE);                               \
    if (!zero) {                                                            \
        tcg_gen_not_tl(dest, src);                                          \
    }                                                                       \
    tcg_gen_clzi_i64(dest, dest, 64);                                       \
    gen_set_gpr(a->rd, dest, EXT_NONE);                                     \
    return true;                                                            \
}                                                                           \

DECLEAR_GEN_DCL(legacy, arg_r)
#undef DECLEAR_GEN_DCL
#endif

static bool gen_setcond(DisasContext *ctx, arg_r *a, TCGCond cond)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rt, EXT_NONE);

    tcg_gen_setcond_tl(cond, dest, src1, src2);

    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_setcond_imm(DisasContext *ctx, arg_i *a, TCGCond cond)
{
    TCGv dest = dest_gpr(ctx, a->rt);
    TCGv src1 = get_gpr(ctx, a->rs, EXT_NONE);

    tcg_gen_setcondi_tl(cond, dest, src1, a->imm);

    gen_set_gpr(a->rt, dest, EXT_NONE);

    return true;
}

static bool gen_mult32(DisasContext *ctx, arg_r2_s *a,
                       void (*func)(TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv src1 = get_gpr(ctx, a->rs, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rt, EXT_NONE);
    TCGv dst_lo = dest_lo(ctx, 0);
    TCGv dst_hi = dest_hi(ctx, 0);
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t1, src1);
    tcg_gen_trunc_tl_i32(t2, src2);

    func(t1, t2, t1, t2);

    tcg_gen_ext_i32_tl(dst_lo, t1);
    tcg_gen_ext_i32_tl(dst_hi, t2);

    gen_set_lo(0, dst_lo, EXT_NONE);
    gen_set_hi(0, dst_hi, EXT_NONE);

    return true;
}

#if defined(TARGET_MIPS64)
static bool gen_mult64(DisasContext *ctx, arg_r2_s *a,
                       void (*func)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv src1 = get_gpr(ctx, a->rs, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rt, EXT_NONE);
    TCGv dst_lo = dest_lo(ctx, 0);
    TCGv dst_hi = dest_hi(ctx, 0);

    func(dst_lo, dst_hi, src1, src2);

    gen_set_lo(0, dst_lo, EXT_NONE);
    gen_set_hi(0, dst_hi, EXT_NONE);

    return true;
}
#endif

static bool gen_div(DisasContext *ctx, arg_r2_s *a, DisasExtend src_ext,
                    DisasExtend dst_ext)
{
    TCGv temp1, temp2, zero, one, mone, min;
    TCGv src1 = get_gpr(ctx, a->rs, src_ext);
    TCGv src2 = get_gpr(ctx, a->rt, src_ext);
    TCGv dst_lo = dest_lo(ctx, 0);
    TCGv dst_hi = dest_hi(ctx, 0);

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);
    min = tcg_constant_tl(1ull << (TARGET_LONG_BITS - 1));

    /*
     * If overflow, set temp2 to 1, else source2.
     * This produces the required result of min.
     */
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, src1, min);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, src2, mone);
    tcg_gen_and_tl(temp1, temp1, temp2);
    tcg_gen_movcond_tl(TCG_COND_NE, temp2, temp1, zero, one, src2);

    /*
     * If div by zero, set temp1 to -1 and temp2 to 1 to
     * produce the required result of -1.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp1, src2, zero, mone, src1);
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, src2, zero, one, src2);

    tcg_gen_div_tl(dst_lo, temp1, temp2);
    tcg_gen_rem_tl(dst_hi, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);

    gen_set_lo(0, dst_lo, dst_ext);
    gen_set_hi(0, dst_hi, dst_ext);

    return true;
}

static bool gen_divu(DisasContext *ctx, arg_r2_s *a, DisasExtend src_ext,
                    DisasExtend dst_ext)
{
    TCGv temp1, temp2, zero, one, max;
    TCGv src1 = get_gpr(ctx, a->rs, src_ext);
    TCGv src2 = get_gpr(ctx, a->rt, src_ext);
    TCGv dst_lo = dest_lo(ctx, 0);
    TCGv dst_hi = dest_hi(ctx, 0);

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    max = tcg_constant_tl(~0);

    /*
     * If div by zero, set temp1 to max and temp2 to 1 to
     * produce the required result of max.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp1, src2, zero, max, src1);
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, src2, zero, one, src2);

    tcg_gen_divu_tl(dst_lo, temp1, temp2);
    tcg_gen_remu_tl(dst_hi, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);

    gen_set_lo(0, dst_lo, dst_ext);
    gen_set_hi(0, dst_hi, dst_ext);

    return true;
}

static bool gen_mul_addsub(DisasContext *ctx, arg_r2_s *a, DisasExtend ext,
                            void (*func)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv src1 = get_gpr(ctx, a->rs, ext);
    TCGv src2 = get_gpr(ctx, a->rt, ext);
    TCGv_i64 src3 = get_hilo(ctx, 0);
    TCGv_i64 dst = dest_hilo(ctx, 0);
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();

    switch (ext) {
    case EXT_SIGN:
        tcg_gen_ext_tl_i64(t2, src1);
        tcg_gen_ext_tl_i64(t3, src2);
        break;
    case EXT_ZERO:
        tcg_gen_extu_tl_i64(t2, src1);
        tcg_gen_extu_tl_i64(t3, src2);
        break;
    default:
        g_assert_not_reached();
        break;
    }

    tcg_gen_mul_i64(dst, t2, t3);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
    func(dst, dst, src3);

    gen_set_hilo(0, dst);

    return true;
}

TRANS(ADD, gen_add, EXT_SIGN)
TRANS(ADDU, gen_arith_notrap, EXT_SIGN, tcg_gen_add_tl)
TRANS_6R(ADDI, gen_add_imm, EXT_SIGN)
TRANS(ADDIU, gen_arith_imm_notrap, EXT_SIGN, tcg_gen_addi_tl)
TRANS(SUB, gen_sub, EXT_SIGN)
TRANS(SUBU, gen_arith_notrap, EXT_SIGN, tcg_gen_sub_tl)
TRANS_6R(CLO, gen_cl_legacy, false)
TRANS_6R(CLZ, gen_cl_legacy, true)
TRANS(SLT, gen_setcond, TCG_COND_LT)
TRANS(SLTU, gen_setcond, TCG_COND_LTU)
TRANS(SLTI, gen_setcond_imm, TCG_COND_LT)
TRANS(SLTIU, gen_setcond_imm, TCG_COND_LTU)
TRANS_6R(MUL, gen_arith_notrap, EXT_SIGN, tcg_gen_mul_tl)
TRANS_6R(MULT, gen_mult32, tcg_gen_muls2_i32)
TRANS_6R(MULTU, gen_mult32, tcg_gen_mulu2_i32)
TRANS_6R(DIV, gen_div, EXT_SIGN, EXT_SIGN)
TRANS_6R(DIVU, gen_divu, EXT_ZERO, EXT_SIGN)
TRANS_6R(MADD, gen_mul_addsub, EXT_SIGN, tcg_gen_add_i64)
TRANS_6R(MADDU, gen_mul_addsub, EXT_ZERO, tcg_gen_add_i64)
TRANS_6R(MSUB, gen_mul_addsub, EXT_SIGN, tcg_gen_sub_i64)
TRANS_6R(MSUBU, gen_mul_addsub, EXT_ZERO, tcg_gen_sub_i64)

TRANS64(DADD, gen_add, EXT_NONE)
TRANS64(DADDU, gen_arith_notrap, EXT_NONE, tcg_gen_add_tl)
TRANS64_6R(DADDI, gen_add_imm, EXT_NONE)
TRANS64(DADDIU, gen_arith_imm_notrap, EXT_NONE, tcg_gen_addi_tl)
TRANS64(DSUB, gen_sub, EXT_NONE)
TRANS64(DSUBU, gen_arith_notrap, EXT_NONE, tcg_gen_sub_tl)
TRANS64_6R(DCLO, gen_dcl_legacy, false)
TRANS64_6R(DCLZ, gen_dcl_legacy, true)
TRANS64_6R(DMULT, gen_mult64, tcg_gen_muls2_i64)
TRANS64_6R(DMULTU, gen_mult64, tcg_gen_mulu2_i64)
TRANS64_6R(DDIV, gen_div, EXT_NONE, EXT_NONE)
TRANS64_6R(DDIVU, gen_divu, EXT_NONE, EXT_NONE)
