/***                           DMF extension                               ***/

static inline void set_dmr_row(int reg, int row, TCGv_i128 src)
{
    tcg_gen_st_i128(src, tcg_env, dmr_row_offset(reg, row));
}

static inline void get_dmr_row(TCGv_i128 dst, int reg, int row)
{
    tcg_gen_ld_i128(dst, tcg_env, dmr_row_offset(reg, row));
}

static inline TCGv_ptr gen_dmr_ptr(int reg)
{
    TCGv_ptr r = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(r, tcg_env, offsetof(CPUPPCState, dmr[reg]));
    return r;
}

static bool trans_DMSETDMRZ(DisasContext *ctx, arg_DMSETDMRZ *a)
{
    TCGv_i128 zero;
    TCGv_i64 hi, lo;

    REQUIRE_INSNS_FLAGS2(ctx, ISA320);
    REQUIRE_DM(ctx);

    zero = tcg_temp_new_i128();
    hi = tcg_constant_i64(0);
    lo = tcg_constant_i64(0);

    tcg_gen_concat_i64_i128(zero, lo, hi);
    for (int row = 0; row < 8; row++) {
        set_dmr_row(a->ra, row, zero);
    }
    return true;
}

static bool do_ext_inst_512(DisasContext *ctx, arg_XX3_abp_as_p *a, bool ext)
{
    TCGv_i128 temp;

    REQUIRE_DM(ctx);
    REQUIRE_VSX(ctx);

    temp = tcg_temp_new_i128();
    int start_row = (a->p > 0) ? 4 : 0;

    for (int i = 0; i < 4; i++) {
        int vsr_reg = (i < 2) ? a->xap : a->xbp;
        if (ext) {
            get_dmr_row(temp, a->as, start_row + i);
            set_vsr_full(vsr_reg + (i % 2), temp);
        } else {
            get_vsr_full(temp, vsr_reg + (i % 2));
            set_dmr_row(a->as, start_row + i, temp);
        }
    }
    return true;
}

static bool do_ext_inst_256(DisasContext *ctx, arg_XX2_bp_as_p *a, bool ext)
{
    TCGv_i128 temp;

    REQUIRE_DM(ctx);
    REQUIRE_VSX(ctx);

    temp = tcg_temp_new_i128();
    int start_row = 2 * a->p;

    for (int i = 0; i < 2; i++) {
        if (ext) {
            get_dmr_row(temp, a->as, start_row + i);
            set_vsr_full(a->xbp + i, temp);
        } else {
            get_vsr_full(temp, a->xbp + i);
            set_dmr_row(start_row, a->xbp + i, temp);
        }
    }
    return true;
}

TRANS_FLAGS2(ISA320, DMXXEXTFDMR512, do_ext_inst_512, true)
TRANS_FLAGS2(ISA320, DMXXINSTDMR512, do_ext_inst_512, false)
TRANS_FLAGS2(ISA320, DMXXEXTFDMR256, do_ext_inst_256, true)
TRANS_FLAGS2(ISA320, DMXXINSTDMR256, do_ext_inst_256, false)

static bool trans_DMMR(DisasContext *ctx, arg_DMMR *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA320);
    REQUIRE_DM(ctx);

    TCGv_i128 temp;
    temp = tcg_temp_new_i128();
    for (int row = 0; row < 8; row++) {
        get_dmr_row(temp, a->rb, row);
        set_dmr_row(a->ra, row, temp);
    }
    return true;
}

static bool trans_DMXOR(DisasContext *ctx, arg_DMXOR *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA320);
    REQUIRE_DM(ctx);

    for (int row = 0; row < 8; row++) {
        tcg_gen_gvec_xor(MO_64, dmr_row_offset(a->ra, row),
                         dmr_row_offset(a->ra, row), dmr_row_offset(a->rb, row),
                         16, 16);
    }
    return true;
}

static bool trans_DMXXMFACC(DisasContext *ctx, arg_DMXXMFACC *a)
{
    TCGv_i128 temp;

    REQUIRE_INSNS_FLAGS2(ctx, ISA320);
    REQUIRE_DM(ctx);
    REQUIRE_VSX(ctx);

    temp = tcg_temp_new_i128();
    for (int i = 0; i < 4; i++) {
        get_dmr_row(temp, a->ra, i);
        set_vsr_full((4 * a->ra) + i, temp);
    }
    return true;
}

static bool trans_DMXXMTACC(DisasContext *ctx, arg_DMXXMTACC *a)
{
    TCGv_i128 temp;
    TCGv_i64 hi, lo;

    REQUIRE_INSNS_FLAGS2(ctx, ISA320);
    REQUIRE_DM(ctx);
    REQUIRE_VSX(ctx);

    temp = tcg_temp_new_i128();
    for (int i = 0; i < 4; i++) {
        get_vsr_full(temp, (4 * a->ra) + i);
        set_dmr_row(a->ra, i, temp);
    }

    /*
     * As per ISA 3.2, the last four rows of the DMR corresponding
     * to the target ACC are set to zero.
     */
    hi = tcg_constant_i64(0);
    lo = tcg_constant_i64(0);
    tcg_gen_concat_i64_i128(temp, lo, hi);
    for (int i = 4; i < 8; i++) {
        set_dmr_row(a->ra, i, temp);
    }
    return true;
}

static bool trans_DMSETACCZ(DisasContext *ctx, arg_DMSETACCZ *a)
{
    /*
     * As per ISA 3.2, the last four rows of the DMR corresponding
     * to the target ACC are set to zero.
     * So essentially, all rows are set to zero.
     * This makes this instruction equivalent to DMSETDMRZ.
     */
    return trans_DMSETDMRZ(ctx, a);
}

static bool do_dm_ger(DisasContext *ctx, arg_MMIRR_XX3 *a,
    void (*helper)(TCGv_env, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32))
{
    uint32_t mask;
    TCGv_ptr xt, xa, xb;
    REQUIRE_INSNS_FLAGS2(ctx, ISA320);
    REQUIRE_DM(ctx);
    REQUIRE_VSX(ctx);

    xt = gen_dmr_ptr(a->xt);
    xa = gen_vsr_ptr(a->xa);
    xb = gen_vsr_ptr(a->xb);

    mask = ger_pack_masks(a->pmsk, a->ymsk, a->xmsk);
    helper(tcg_env, xa, xb, xt, tcg_constant_i32(mask));
    return true;
}

TRANS(DMXVI4GER8, do_dm_ger, gen_helper_XVI4GER8)
TRANS(DMXVI4GER8PP, do_dm_ger,  gen_helper_XVI4GER8PP)
TRANS(DMXVI8GER4, do_dm_ger, gen_helper_XVI8GER4)
TRANS(DMXVI8GER4PP, do_dm_ger,  gen_helper_XVI8GER4PP)
TRANS(DMXVI8GER4SPP, do_dm_ger, gen_helper_XVI8GER4SPP)
TRANS(DMXVI16GER2, do_dm_ger, gen_helper_XVI16GER2)
TRANS(DMXVI16GER2PP, do_dm_ger, gen_helper_XVI16GER2PP)
TRANS(DMXVI16GER2S, do_dm_ger, gen_helper_XVI16GER2S)
TRANS(DMXVI16GER2SPP, do_dm_ger, gen_helper_XVI16GER2SPP)

TRANS64(PMDMXVI4GER8, do_dm_ger, gen_helper_XVI4GER8)
TRANS64(PMDMXVI4GER8PP, do_dm_ger, gen_helper_XVI4GER8PP)
TRANS64(PMDMXVI8GER4, do_dm_ger, gen_helper_XVI8GER4)
TRANS64(PMDMXVI8GER4PP, do_dm_ger, gen_helper_XVI8GER4PP)
TRANS64(PMDMXVI8GER4SPP, do_dm_ger, gen_helper_XVI8GER4SPP)
TRANS64(PMDMXVI16GER2, do_dm_ger, gen_helper_XVI16GER2)
TRANS64(PMDMXVI16GER2PP, do_dm_ger, gen_helper_XVI16GER2PP)
TRANS64(PMDMXVI16GER2S, do_dm_ger, gen_helper_XVI16GER2S)
TRANS64(PMDMXVI16GER2SPP, do_dm_ger, gen_helper_XVI16GER2SPP)

TRANS(DMXVBF16GER2, do_dm_ger, gen_helper_XVBF16GER2)
TRANS(DMXVBF16GER2PP, do_dm_ger, gen_helper_XVBF16GER2PP)
TRANS(DMXVBF16GER2PN, do_dm_ger, gen_helper_XVBF16GER2PN)
TRANS(DMXVBF16GER2NP, do_dm_ger, gen_helper_XVBF16GER2NP)
TRANS(DMXVBF16GER2NN, do_dm_ger, gen_helper_XVBF16GER2NN)

TRANS(DMXVF16GER2, do_dm_ger, gen_helper_XVF16GER2)
TRANS(DMXVF16GER2PP, do_dm_ger, gen_helper_XVF16GER2PP)
TRANS(DMXVF16GER2PN, do_dm_ger, gen_helper_XVF16GER2PN)
TRANS(DMXVF16GER2NP, do_dm_ger, gen_helper_XVF16GER2NP)
TRANS(DMXVF16GER2NN, do_dm_ger, gen_helper_XVF16GER2NN)

TRANS(DMXVF32GER, do_dm_ger, gen_helper_XVF32GER)
TRANS(DMXVF32GERPP, do_dm_ger, gen_helper_XVF32GERPP)
TRANS(DMXVF32GERPN, do_dm_ger, gen_helper_XVF32GERPN)
TRANS(DMXVF32GERNP, do_dm_ger, gen_helper_XVF32GERNP)
TRANS(DMXVF32GERNN, do_dm_ger, gen_helper_XVF32GERNN)

TRANS(DMXVF64GER, do_dm_ger, gen_helper_XVF64GER)
TRANS(DMXVF64GERPP, do_dm_ger, gen_helper_XVF64GERPP)
TRANS(DMXVF64GERPN, do_dm_ger, gen_helper_XVF64GERPN)
TRANS(DMXVF64GERNP, do_dm_ger, gen_helper_XVF64GERNP)
TRANS(DMXVF64GERNN, do_dm_ger, gen_helper_XVF64GERNN)

TRANS64(PMDMXVBF16GER2, do_dm_ger, gen_helper_XVBF16GER2)
TRANS64(PMDMXVBF16GER2PP, do_dm_ger, gen_helper_XVBF16GER2PP)
TRANS64(PMDMXVBF16GER2PN, do_dm_ger, gen_helper_XVBF16GER2PN)
TRANS64(PMDMXVBF16GER2NP, do_dm_ger, gen_helper_XVBF16GER2NP)
TRANS64(PMDMXVBF16GER2NN, do_dm_ger, gen_helper_XVBF16GER2NN)

TRANS64(PMDMXVF16GER2, do_dm_ger, gen_helper_XVF16GER2)
TRANS64(PMDMXVF16GER2PP, do_dm_ger, gen_helper_XVF16GER2PP)
TRANS64(PMDMXVF16GER2PN, do_dm_ger, gen_helper_XVF16GER2PN)
TRANS64(PMDMXVF16GER2NP, do_dm_ger, gen_helper_XVF16GER2NP)
TRANS64(PMDMXVF16GER2NN, do_dm_ger, gen_helper_XVF16GER2NN)

TRANS64(PMDMXVF32GER, do_dm_ger, gen_helper_XVF32GER)
TRANS64(PMDMXVF32GERPP, do_dm_ger, gen_helper_XVF32GERPP)
TRANS64(PMDMXVF32GERPN, do_dm_ger, gen_helper_XVF32GERPN)
TRANS64(PMDMXVF32GERNP, do_dm_ger, gen_helper_XVF32GERNP)
TRANS64(PMDMXVF32GERNN, do_dm_ger, gen_helper_XVF32GERNN)

TRANS64(PMDMXVF64GER, do_dm_ger, gen_helper_XVF64GER)
TRANS64(PMDMXVF64GERPP, do_dm_ger, gen_helper_XVF64GERPP)
TRANS64(PMDMXVF64GERPN, do_dm_ger, gen_helper_XVF64GERPN)
TRANS64(PMDMXVF64GERNP, do_dm_ger, gen_helper_XVF64GERNP)
TRANS64(PMDMXVF64GERNN, do_dm_ger, gen_helper_XVF64GERNN)
