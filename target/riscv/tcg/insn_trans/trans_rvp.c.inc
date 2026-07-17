/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RISC-V translation routines for the P Standard Extensions. */
/* Copyright (c) 2026 ISRC ISCAS. */

/* Save a 64 bit data in src to dst and dst + 1 */
static void set_pair_regs(DisasContext *ctx, int dst, TCGv_i64 src)
{
#if defined(TARGET_RISCV32)
    TCGv_i64 tl_64 = tcg_temp_new_i64();
    TCGv_i64 th_64 = tcg_temp_new_i64();
    TCGv_i32 tl_32 = tcg_temp_new_i32();
    TCGv_i32 th_32 = tcg_temp_new_i32();
    tcg_gen_extract_i64(tl_64, src, 0, 32);
    tcg_gen_extract_i64(th_64, src, 32, 32);
    tcg_gen_trunc_i64_tl(tl_32, tl_64);
    tcg_gen_trunc_i64_tl(th_32, th_64);
    gen_set_gpr(ctx, dst, tl_32);
    gen_set_gpr(ctx, dst + 1, th_32);
# else
    gen_set_gpr(ctx, dst, src);
#endif
}

/* Concat two 32 bit data in src and src + 1 to dst */
static void get_pair_regs(DisasContext *ctx, TCGv_i64 dst, int src)
{
#if defined(TARGET_RISCV32)
    TCGv t1 = get_gpr(ctx, src, EXT_NONE);
    TCGv t2 = get_gpr(ctx, src + 1, EXT_NONE);
    tcg_gen_concat_i32_i64(dst, t1, t2);
#else
    TCGv t1 = get_gpr(ctx, src, EXT_NONE);
    tcg_gen_mov_tl(dst, t1);
#endif
}

static bool require_rvp(DisasContext *ctx)
{
    uint32_t opcode = ctx->opcode & 0x7f;

    if (!has_ext(ctx, RVP)) {
        return false;
    }

    /*
     * P_VXSAT_EXCP is encoded in the TB flags only when P is implemented
     * without Zve*, where stateen0.VXSAT controls the whole OP-32 and
     * OP-IMM-32 P instruction spaces.  When Zve* is present, this field
     * remains NONE and vxsat access is checked by the VS path instead.
     */
    if ((opcode == 0x3b || opcode == 0x1b) &&
        ctx->p_vxsat_excp != P_VXSAT_EXCP_NONE) {
        ctx->virt_inst_excp =
            ctx->p_vxsat_excp == P_VXSAT_EXCP_VIRTUAL;
        return false;
    }

    return true;
}

static bool prepare_rvp_vxsat(DisasContext *ctx)
{
    if (!ctx->cfg_ptr->ext_zve32x) {
        return true;
    }

    if (ctx->mstatus_vs == EXT_STATUS_DISABLED) {
        return false;
    }

    mark_vs_dirty(ctx);
    return true;
}

#define REQUIRE_RVP(ctx) do {             \
    if (!require_rvp(ctx)) {              \
        return false;                     \
    }                                     \
} while (0)

#define GEN_SIMD_TRANS_BODY(NAME, VXSAT)                    \
do {                                                        \
    REQUIRE_RVP(ctx);                                       \
    if (VXSAT && !prepare_rvp_vxsat(ctx)) {                 \
        return false;                                       \
    }                                                       \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1, src2);           \
    gen_set_gpr(ctx, a->rd, dest);                          \
    return true;                                            \
} while (0)

#define GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)                 \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    return true;                                           \
}

#define GEN_SIMD_TRANS_UNAVAILABLE_64(NAME)                 \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    return true;                                           \
}

#define GEN_SIMD_TRANS(NAME)                                \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    GEN_SIMD_TRANS_BODY(NAME, false);                       \
}

#define GEN_SIMD_TRANS_VXSAT(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    GEN_SIMD_TRANS_BODY(NAME, true);                        \
}

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_32(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    GEN_SIMD_TRANS_BODY(NAME, false);                       \
}
#define GEN_SIMD_TRANS_32_VXSAT(NAME)                      \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    GEN_SIMD_TRANS_BODY(NAME, true);                        \
}
#else
#define GEN_SIMD_TRANS_32(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#define GEN_SIMD_TRANS_32_VXSAT(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_64(NAME) GEN_SIMD_TRANS_UNAVAILABLE_64(NAME)
#define GEN_SIMD_TRANS_64_VXSAT(NAME) GEN_SIMD_TRANS_UNAVAILABLE_64(NAME)
#else
#define GEN_SIMD_TRANS_64(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    GEN_SIMD_TRANS_BODY(NAME, false);                       \
}
#define GEN_SIMD_TRANS_64_VXSAT(NAME)                      \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    GEN_SIMD_TRANS_BODY(NAME, true);                        \
}
#endif

#define GEN_SIMD_TRANS_ACC_BODY(NAME, VXSAT)                \
do {                                                        \
    REQUIRE_RVP(ctx);                                       \
    if (VXSAT && !prepare_rvp_vxsat(ctx)) {                 \
        return false;                                       \
    }                                                       \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    TCGv t = tcg_temp_new();                                \
    gen_helper_##NAME(t, tcg_env, src1, src2, dest);        \
    gen_set_gpr(ctx, a->rd, t);                             \
    return true;                                            \
} while (0)

#define GEN_SIMD_TRANS_ACC(NAME)                            \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    GEN_SIMD_TRANS_ACC_BODY(NAME, false);                   \
}

#define GEN_SIMD_TRANS_ACC_VXSAT(NAME)                     \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    GEN_SIMD_TRANS_ACC_BODY(NAME, true);                    \
}

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_ACC_32(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    GEN_SIMD_TRANS_ACC_BODY(NAME, false);                   \
}
#else
#define GEN_SIMD_TRANS_ACC_32(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_ACC_64(NAME) GEN_SIMD_TRANS_UNAVAILABLE_64(NAME)
#else
#define GEN_SIMD_TRANS_ACC_64(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    GEN_SIMD_TRANS_ACC_BODY(NAME, false);                   \
}
#endif

#define GEN_SIMD_TRANS_R1_BODY(NAME, VXSAT)                 \
do {                                                        \
    REQUIRE_RVP(ctx);                                       \
    if (VXSAT && !prepare_rvp_vxsat(ctx)) {                 \
        return false;                                       \
    }                                                       \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1);                 \
    gen_set_gpr(ctx, a->rd, dest);                          \
    return true;                                            \
} while (0)

#define GEN_SIMD_TRANS_R1(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    GEN_SIMD_TRANS_R1_BODY(NAME, false);                    \
}

#define GEN_SIMD_TRANS_R1_VXSAT(NAME)                      \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    GEN_SIMD_TRANS_R1_BODY(NAME, true);                     \
}

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_R1_64(NAME) GEN_SIMD_TRANS_UNAVAILABLE_64(NAME)
#define GEN_SIMD_TRANS_R1_64_VXSAT(NAME) GEN_SIMD_TRANS_UNAVAILABLE_64(NAME)
#else
#define GEN_SIMD_TRANS_R1_64(NAME)                          \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    GEN_SIMD_TRANS_R1_BODY(NAME, false);                    \
}
#define GEN_SIMD_TRANS_R1_64_VXSAT(NAME)                   \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    GEN_SIMD_TRANS_R1_BODY(NAME, true);                     \
}
#endif

#define GEN_SIMD_TRANS_IMM_BODY(NAME, VXSAT)                \
do {                                                        \
    REQUIRE_RVP(ctx);                                       \
    if (VXSAT && !prepare_rvp_vxsat(ctx)) {                 \
        return false;                                       \
    }                                                       \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv imm = tcg_constant_tl(a->imm);                     \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1, imm);            \
    gen_set_gpr(ctx, a->rd, dest);                          \
    return true;                                            \
} while (0)

#define GEN_SIMD_TRANS_IMM(NAME)                            \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    GEN_SIMD_TRANS_IMM_BODY(NAME, false);                   \
}

#define GEN_SIMD_TRANS_IMM_VXSAT(NAME)                     \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    GEN_SIMD_TRANS_IMM_BODY(NAME, true);                    \
}

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_IMM_32(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    GEN_SIMD_TRANS_IMM_BODY(NAME, false);                   \
}
#define GEN_SIMD_TRANS_IMM_32_VXSAT(NAME)                  \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    GEN_SIMD_TRANS_IMM_BODY(NAME, true);                    \
}
#else
#define GEN_SIMD_TRANS_IMM_32(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#define GEN_SIMD_TRANS_IMM_32_VXSAT(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_IMM_64(NAME) GEN_SIMD_TRANS_UNAVAILABLE_64(NAME)
#define GEN_SIMD_TRANS_IMM_64_VXSAT(NAME) GEN_SIMD_TRANS_UNAVAILABLE_64(NAME)
#else
#define GEN_SIMD_TRANS_IMM_64(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    GEN_SIMD_TRANS_IMM_BODY(NAME, false);                   \
}
#define GEN_SIMD_TRANS_IMM_64_VXSAT(NAME)                  \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    GEN_SIMD_TRANS_IMM_BODY(NAME, true);                    \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_SCALAR_OP(NAME, SRC2)       \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    TCGv_i32 src1 = get_gpr(ctx, a->rs1, EXT_NONE);         \
    TCGv_i32 src2 = SRC2;                                   \
    TCGv_i64 t = tcg_temp_new_i64();                        \
    gen_helper_##NAME(t, tcg_env, src1, src2);              \
    set_pair_regs(ctx, (a->rd) * 2, t);                     \
    return true;                                            \
}

#define GEN_SIMD_TRANS_REG_PAIR_SCALAR(NAME) \
    GEN_SIMD_TRANS_REG_PAIR_SCALAR_OP(NAME, get_gpr(ctx, a->rs2, EXT_NONE))

#define GEN_SIMD_TRANS_REG_PAIR_SCALAR_IMM(NAME) \
    GEN_SIMD_TRANS_REG_PAIR_SCALAR_OP(NAME, tcg_constant_i32(a->imm))
#else
#define GEN_SIMD_TRANS_REG_PAIR_SCALAR(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#define GEN_SIMD_TRANS_REG_PAIR_SCALAR_IMM(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_OP(INSN, HELPER, SRC2_0, SRC2_1, VXSAT) \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)       \
{                                                                 \
    REQUIRE_32BIT(ctx);                                           \
    REQUIRE_RVP(ctx);                                             \
    if (VXSAT && !prepare_rvp_vxsat(ctx)) {                       \
        return false;                                             \
    }                                                             \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);           \
    TCGv src2_0 = SRC2_0;                                         \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                     \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);       \
    TCGv src2_1 = SRC2_1;                                         \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                 \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0, src2_0);         \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1, src2_1);         \
    gen_set_gpr(ctx, (a->rd) * 2, dest_0);                        \
    gen_set_gpr(ctx, (a->rd) * 2 + 1, dest_1);                    \
    return true;                                                  \
}

#define GEN_SIMD_TRANS_REG_PAIR_LANE(INSN, HELPER)                   \
    GEN_SIMD_TRANS_REG_PAIR_OP(INSN, HELPER,                       \
        get_gpr(ctx, (a->rs2) * 2, EXT_NONE),                     \
        get_gpr(ctx, (a->rs2) * 2 + 1, EXT_NONE), false)

#define GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(INSN, HELPER)            \
    GEN_SIMD_TRANS_REG_PAIR_OP(INSN, HELPER,                       \
        get_gpr(ctx, (a->rs2) * 2, EXT_NONE),                     \
        get_gpr(ctx, (a->rs2) * 2 + 1, EXT_NONE), true)

#define GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(INSN, HELPER)               \
    GEN_SIMD_TRANS_REG_PAIR_OP(INSN, HELPER,                       \
        tcg_constant_tl(a->imm), tcg_constant_tl(a->imm), false)

#define GEN_SIMD_TRANS_REG_PAIR_LANE_IMM_VXSAT(INSN, HELPER)        \
    GEN_SIMD_TRANS_REG_PAIR_OP(INSN, HELPER,                       \
        tcg_constant_tl(a->imm), tcg_constant_tl(a->imm), true)
#else
#define GEN_SIMD_TRANS_REG_PAIR_LANE(INSN, HELPER) \
    GEN_SIMD_TRANS_UNAVAILABLE_32(INSN)
#define GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(INSN, HELPER) \
    GEN_SIMD_TRANS_UNAVAILABLE_32(INSN)
#define GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(INSN, HELPER) \
    GEN_SIMD_TRANS_UNAVAILABLE_32(INSN)
#define GEN_SIMD_TRANS_REG_PAIR_LANE_IMM_VXSAT(INSN, HELPER) \
    GEN_SIMD_TRANS_UNAVAILABLE_32(INSN)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_OP(INSN, HELPER, VXSAT)     \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)    \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    REQUIRE_RVP(ctx);                                          \
    if (VXSAT && !prepare_rvp_vxsat(ctx)) {                    \
        return false;                                          \
    }                                                          \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);        \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                    \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);        \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                  \
    TCGv src2   = get_gpr(ctx, a->rs2, EXT_NONE);              \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0, src2);        \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1, src2);        \
    gen_set_gpr(ctx, (a->rd) * 2, dest_0);                     \
    gen_set_gpr(ctx, (a->rd) * 2 + 1, dest_1);                 \
    return true;                                              \
}

#define GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(INSN, HELPER)              \
    GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_OP(INSN, HELPER, false)

#define GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(INSN, HELPER)       \
    GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_OP(INSN, HELPER, true)
#else
#define GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(INSN, HELPER) \
    GEN_SIMD_TRANS_UNAVAILABLE_32(INSN)
#define GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(INSN, HELPER) \
    GEN_SIMD_TRANS_UNAVAILABLE_32(INSN)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY_OP(INSN, HELPER, VXSAT)     \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)    \
{                                                              \
    REQUIRE_RVP(ctx);                                          \
    if (VXSAT && !prepare_rvp_vxsat(ctx)) {                    \
        return false;                                          \
    }                                                          \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);        \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                  \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);    \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);              \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0);              \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1);              \
    gen_set_gpr(ctx, (a->rd) * 2, dest_0);                     \
    gen_set_gpr(ctx, (a->rd) * 2 + 1, dest_1);                 \
    return true;                                               \
}

#define GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY(INSN, HELPER)               \
    GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY_OP(INSN, HELPER, false)

#define GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY_VXSAT(INSN, HELPER)        \
    GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY_OP(INSN, HELPER, true)
#else
#define GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY(INSN, HELPER) \
    GEN_SIMD_TRANS_UNAVAILABLE_32(INSN)
#define GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY_VXSAT(INSN, HELPER) \
    GEN_SIMD_TRANS_UNAVAILABLE_32(INSN)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_ACC(NAME)                   \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);             \
    TCGv_i64 t = tcg_temp_new_i64();                        \
    if (a->rd == 0) {                                         \
        tcg_gen_movi_i64(t, 0);                             \
    } else {                                                  \
        get_pair_regs(ctx, t, (a->rd) * 2);                   \
    }                                                       \
    gen_helper_##NAME(t, tcg_env, src1, src2, t);           \
    set_pair_regs(ctx, (a->rd) * 2, t);                       \
    return true;                                           \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_ACC(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_PREDSUM(NAME)               \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    TCGv_i32 src1_l;                                        \
    TCGv_i32 src1_h;                                        \
    TCGv_i32 src2 = get_gpr(ctx, a->rs2, EXT_NONE);         \
    TCGv_i32 dest = dest_gpr(ctx, a->rd);                   \
    if (a->rs1 == 0) {                                        \
        src1_l = tcg_temp_new_i32();                        \
        src1_h = tcg_temp_new_i32();                        \
        tcg_gen_movi_i32(src1_l, 0);                        \
        tcg_gen_movi_i32(src1_h, 0);                        \
    } else {                                                  \
        src1_l = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);        \
        src1_h = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);      \
    }                                                       \
    gen_helper_##NAME(dest, tcg_env, src1_l, src1_h, src2); \
    gen_set_gpr(ctx, a->rd, dest);                          \
    return true;                                           \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_PREDSUM(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_PN_OP(NAME, SRC2, VXSAT)            \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    if (VXSAT && !prepare_rvp_vxsat(ctx)) {                 \
        return false;                                       \
    }                                                       \
    TCGv_i64 s1 = tcg_temp_new_i64();                       \
    if (a->rs1 == 0) {                                      \
        tcg_gen_mov_i64(s1, 0);                             \
    } else {                                                \
        get_pair_regs(ctx, s1, a->rs1 * 2);                 \
    }                                                       \
    TCGv src2 = SRC2;                                       \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, s1, src2);             \
    gen_set_gpr(ctx, a->rd, dest);                          \
    return true;                                            \
}

#define GEN_SIMD_TRANS_PN_OP_IMM(NAME) \
    GEN_SIMD_TRANS_PN_OP(NAME, tcg_constant_tl(a->imm), false)

#define GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(NAME) \
    GEN_SIMD_TRANS_PN_OP(NAME, tcg_constant_tl(a->imm), true)

#define GEN_SIMD_TRANS_PN_OP_REG(NAME) \
    GEN_SIMD_TRANS_PN_OP(NAME, get_gpr(ctx, a->rs2, EXT_NONE), false)

#define GEN_SIMD_TRANS_PN_OP_REG_VXSAT(NAME) \
    GEN_SIMD_TRANS_PN_OP(NAME, get_gpr(ctx, a->rs2, EXT_NONE), true)
#else
#define GEN_SIMD_TRANS_PN_OP_IMM(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#define GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#define GEN_SIMD_TRANS_PN_OP_REG(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#define GEN_SIMD_TRANS_PN_OP_REG_VXSAT(NAME) GEN_SIMD_TRANS_UNAVAILABLE_32(NAME)
#endif

/* Packed SIMD - Arithmetic Operations(Non-Saturating and Saturating) */
GEN_SIMD_TRANS(padd_b)
GEN_SIMD_TRANS(padd_h)
GEN_SIMD_TRANS_64(padd_w)
GEN_SIMD_TRANS(padd_bs)
GEN_SIMD_TRANS(padd_hs)
GEN_SIMD_TRANS_64(padd_ws)
GEN_SIMD_TRANS(psub_b)
GEN_SIMD_TRANS(psub_h)
GEN_SIMD_TRANS_64(psub_w)
GEN_SIMD_TRANS(psh1add_h)
GEN_SIMD_TRANS_64(psh1add_w)
GEN_SIMD_TRANS_VXSAT(pssh1sadd_h)
GEN_SIMD_TRANS_64_VXSAT(pssh1sadd_w)
GEN_SIMD_TRANS_32_VXSAT(ssh1sadd)
GEN_SIMD_TRANS_VXSAT(psadd_b)
GEN_SIMD_TRANS_VXSAT(psadd_h)
GEN_SIMD_TRANS_64_VXSAT(psadd_w)
GEN_SIMD_TRANS_VXSAT(psaddu_b)
GEN_SIMD_TRANS_VXSAT(psaddu_h)
GEN_SIMD_TRANS_64_VXSAT(psaddu_w)
GEN_SIMD_TRANS_32_VXSAT(sadd)
GEN_SIMD_TRANS_32_VXSAT(saddu)
GEN_SIMD_TRANS_VXSAT(pssub_b)
GEN_SIMD_TRANS_VXSAT(pssub_h)
GEN_SIMD_TRANS_64_VXSAT(pssub_w)
GEN_SIMD_TRANS_VXSAT(pssubu_b)
GEN_SIMD_TRANS_VXSAT(pssubu_h)
GEN_SIMD_TRANS_64_VXSAT(pssubu_w)
GEN_SIMD_TRANS_32_VXSAT(ssub)
GEN_SIMD_TRANS_32_VXSAT(ssubu)
GEN_SIMD_TRANS_IMM_VXSAT(psati_h)
GEN_SIMD_TRANS_IMM_VXSAT(pusati_h)
GEN_SIMD_TRANS_IMM_64_VXSAT(psati_w)
GEN_SIMD_TRANS_IMM_64_VXSAT(pusati_w)
GEN_SIMD_TRANS_IMM_32_VXSAT(sati_32)
GEN_SIMD_TRANS_IMM_32_VXSAT(usati_32)
GEN_SIMD_TRANS_IMM_64_VXSAT(sati_64)
GEN_SIMD_TRANS_IMM_64_VXSAT(usati_64)

/* Packed SIMD - Averaging and Rounding Operations */
GEN_SIMD_TRANS(paadd_b)
GEN_SIMD_TRANS(paadd_h)
GEN_SIMD_TRANS_64(paadd_w)
GEN_SIMD_TRANS(paaddu_b)
GEN_SIMD_TRANS(paaddu_h)
GEN_SIMD_TRANS_64(paaddu_w)
GEN_SIMD_TRANS_32(aadd)
GEN_SIMD_TRANS_32(aaddu)
GEN_SIMD_TRANS(pasub_b)
GEN_SIMD_TRANS(pasub_h)
GEN_SIMD_TRANS_64(pasub_w)
GEN_SIMD_TRANS(pasubu_b)
GEN_SIMD_TRANS(pasubu_h)
GEN_SIMD_TRANS_64(pasubu_w)
GEN_SIMD_TRANS_32(asub)
GEN_SIMD_TRANS_32(asubu)

/* Packed SIMD - Absolute Value and Difference Operations */
GEN_SIMD_TRANS_R1_VXSAT(psabs_b)
GEN_SIMD_TRANS_R1_VXSAT(psabs_h)
GEN_SIMD_TRANS_R1(abs)
GEN_SIMD_TRANS_R1_64(absw)
GEN_SIMD_TRANS(pabd_b)
GEN_SIMD_TRANS(pabdu_b)
GEN_SIMD_TRANS(pabd_h)
GEN_SIMD_TRANS(pabdu_h)
GEN_SIMD_TRANS(pabdsumu_b)
GEN_SIMD_TRANS_ACC(pabdsumau_b)

/* Packed SIMD - Comparison and Mask Generation Operations */
GEN_SIMD_TRANS(pmseq_b)
GEN_SIMD_TRANS(pmslt_b)
GEN_SIMD_TRANS(pmsltu_b)
GEN_SIMD_TRANS(pmin_b)
GEN_SIMD_TRANS(pminu_b)
GEN_SIMD_TRANS(pmax_b)
GEN_SIMD_TRANS(pmaxu_b)
GEN_SIMD_TRANS(pmseq_h)
GEN_SIMD_TRANS(pmslt_h)
GEN_SIMD_TRANS(pmsltu_h)
GEN_SIMD_TRANS(pmin_h)
GEN_SIMD_TRANS(pminu_h)
GEN_SIMD_TRANS(pmax_h)
GEN_SIMD_TRANS(pmaxu_h)
GEN_SIMD_TRANS_64(pmseq_w)
GEN_SIMD_TRANS_64(pmslt_w)
GEN_SIMD_TRANS_64(pmsltu_w)
GEN_SIMD_TRANS_64(pmin_w)
GEN_SIMD_TRANS_64(pminu_w)
GEN_SIMD_TRANS_64(pmax_w)
GEN_SIMD_TRANS_64(pmaxu_w)
GEN_SIMD_TRANS_32(mseq)
GEN_SIMD_TRANS_32(mslt)
GEN_SIMD_TRANS_32(msltu)

/* Packed SIMD - Shift Operations */
GEN_SIMD_TRANS_IMM(pslli_b)
GEN_SIMD_TRANS(psll_bs)
GEN_SIMD_TRANS_IMM(pslli_h)
GEN_SIMD_TRANS(psll_hs)
GEN_SIMD_TRANS_IMM_64(pslli_w)
GEN_SIMD_TRANS_64(psll_ws)
GEN_SIMD_TRANS_IMM(psrli_b)
GEN_SIMD_TRANS(psrl_bs)
GEN_SIMD_TRANS_IMM(psrli_h)
GEN_SIMD_TRANS(psrl_hs)
GEN_SIMD_TRANS_IMM_64(psrli_w)
GEN_SIMD_TRANS_64(psrl_ws)
GEN_SIMD_TRANS_IMM(psrai_b)
GEN_SIMD_TRANS(psra_bs)
GEN_SIMD_TRANS_IMM(psrai_h)
GEN_SIMD_TRANS(psra_hs)
GEN_SIMD_TRANS_IMM_64(psrai_w)
GEN_SIMD_TRANS_64(psra_ws)
GEN_SIMD_TRANS_IMM_VXSAT(psslai_h)
GEN_SIMD_TRANS_IMM_64_VXSAT(psslai_w)
GEN_SIMD_TRANS_IMM_32_VXSAT(sslai)
GEN_SIMD_TRANS_IMM(psrari_h)
GEN_SIMD_TRANS_IMM_64(psrari_w)
GEN_SIMD_TRANS_IMM_32(srari_32)
GEN_SIMD_TRANS_IMM_64(srari_64)
GEN_SIMD_TRANS_VXSAT(pssha_hs)
GEN_SIMD_TRANS_64_VXSAT(pssha_ws)
GEN_SIMD_TRANS_VXSAT(psshar_hs)
GEN_SIMD_TRANS_64_VXSAT(psshar_ws)
GEN_SIMD_TRANS_32_VXSAT(ssha)
GEN_SIMD_TRANS_32_VXSAT(sshar)
GEN_SIMD_TRANS_64(sha)
GEN_SIMD_TRANS_64(shar)
GEN_SIMD_TRANS_VXSAT(psshl_hs)
GEN_SIMD_TRANS_VXSAT(psshlr_hs)
GEN_SIMD_TRANS_64_VXSAT(psshl_ws)
GEN_SIMD_TRANS_64_VXSAT(psshlr_ws)
GEN_SIMD_TRANS_32_VXSAT(sshl)
GEN_SIMD_TRANS_32_VXSAT(sshlr)
GEN_SIMD_TRANS_64(shl)
GEN_SIMD_TRANS_64(shlr)

/* Packed SIMD - Exchange Operations */
GEN_SIMD_TRANS(pas_hx)
GEN_SIMD_TRANS(psa_hx)
GEN_SIMD_TRANS_VXSAT(psas_hx)
GEN_SIMD_TRANS_VXSAT(pssa_hx)
GEN_SIMD_TRANS(paas_hx)
GEN_SIMD_TRANS(pasa_hx)
GEN_SIMD_TRANS_64(pas_wx)
GEN_SIMD_TRANS_64(psa_wx)
GEN_SIMD_TRANS_64_VXSAT(psas_wx)
GEN_SIMD_TRANS_64_VXSAT(pssa_wx)
GEN_SIMD_TRANS_64(paas_wx)
GEN_SIMD_TRANS_64(pasa_wx)

/* Packed SIMD - Horizontal Reduction Operations */
GEN_SIMD_TRANS(predsum_bs)
GEN_SIMD_TRANS(predsumu_bs)
GEN_SIMD_TRANS(predsum_hs)
GEN_SIMD_TRANS(predsumu_hs)
GEN_SIMD_TRANS_64(predsum_ws)
GEN_SIMD_TRANS_64(predsumu_ws)

/* Packed SIMD - Pack, Unpack, and Merge Operations */
GEN_SIMD_TRANS(ppaire_b)
GEN_SIMD_TRANS(ppaireo_b)
GEN_SIMD_TRANS(ppairoe_b)
GEN_SIMD_TRANS(ppairo_b)
GEN_SIMD_TRANS_64(ppaire_h)
GEN_SIMD_TRANS(ppaireo_h)
GEN_SIMD_TRANS(ppairoe_h)
GEN_SIMD_TRANS(ppairo_h)
GEN_SIMD_TRANS_64(ppaireo_w)
GEN_SIMD_TRANS_64(ppairoe_w)
GEN_SIMD_TRANS_64(ppairo_w)
GEN_SIMD_TRANS_R1(psext_h_b)
GEN_SIMD_TRANS_R1_64(psext_w_b)
GEN_SIMD_TRANS_R1_64(psext_w_h)
GEN_SIMD_TRANS_R1(rev)
GEN_SIMD_TRANS_R1_64(rev16)
GEN_SIMD_TRANS_64(zip8p)
GEN_SIMD_TRANS_64(zip8hp)
GEN_SIMD_TRANS_64(unzip8p)
GEN_SIMD_TRANS_64(unzip8hp)
GEN_SIMD_TRANS_64(zip16p)
GEN_SIMD_TRANS_64(zip16hp)
GEN_SIMD_TRANS_64(unzip16p)
GEN_SIMD_TRANS_64(unzip16hp)
GEN_SIMD_TRANS_ACC(slx)
GEN_SIMD_TRANS_ACC(srx)
GEN_SIMD_TRANS_ACC(mvm)
GEN_SIMD_TRANS_ACC(mvmn)
GEN_SIMD_TRANS_ACC(merge)
GEN_SIMD_TRANS_64_VXSAT(pnclipp_b)
GEN_SIMD_TRANS_64_VXSAT(pnclipup_b)
GEN_SIMD_TRANS_64_VXSAT(pnclipp_h)
GEN_SIMD_TRANS_64_VXSAT(pnclipup_h)
GEN_SIMD_TRANS_64_VXSAT(pnclipp_w)
GEN_SIMD_TRANS_64_VXSAT(pnclipup_w)

/* Packed SIMD - Count Leading Operations */
GEN_SIMD_TRANS_R1(cls)
GEN_SIMD_TRANS_R1_64(clsw)

/* Packed SIMD - Pure Multiplication Operations */
GEN_SIMD_TRANS(pmulh_h)
GEN_SIMD_TRANS(pmulhsu_h)
GEN_SIMD_TRANS(pmulhu_h)
GEN_SIMD_TRANS(pmulhr_h)
GEN_SIMD_TRANS(pmulhrsu_h)
GEN_SIMD_TRANS(pmulhru_h)
GEN_SIMD_TRANS_64(pmulh_w)
GEN_SIMD_TRANS_64(pmulhr_w)
GEN_SIMD_TRANS_64(pmulhsu_w)
GEN_SIMD_TRANS_64(pmulhrsu_w)
GEN_SIMD_TRANS_64(pmulhu_w)
GEN_SIMD_TRANS_64(pmulhru_w)
GEN_SIMD_TRANS_32(mulhr)
GEN_SIMD_TRANS_32(mulhrsu)
GEN_SIMD_TRANS_32(mulhru)
GEN_SIMD_TRANS(pmulh_h_b0)
GEN_SIMD_TRANS(pmulh_h_b1)
GEN_SIMD_TRANS(pmulhsu_h_b0)
GEN_SIMD_TRANS(pmulhsu_h_b1)
GEN_SIMD_TRANS_32(mulh_h0)
GEN_SIMD_TRANS_32(mulh_h1)
GEN_SIMD_TRANS_32(mulhsu_h0)
GEN_SIMD_TRANS_32(mulhsu_h1)
GEN_SIMD_TRANS_64(pmulh_w_h0)
GEN_SIMD_TRANS_64(pmulh_w_h1)
GEN_SIMD_TRANS_64(pmulhsu_w_h0)
GEN_SIMD_TRANS_64(pmulhsu_w_h1)
GEN_SIMD_TRANS(pmul_h_b00)
GEN_SIMD_TRANS(pmul_h_b01)
GEN_SIMD_TRANS(pmul_h_b11)
GEN_SIMD_TRANS(pmulsu_h_b00)
GEN_SIMD_TRANS(pmulsu_h_b11)
GEN_SIMD_TRANS(pmulu_h_b00)
GEN_SIMD_TRANS(pmulu_h_b01)
GEN_SIMD_TRANS(pmulu_h_b11)
GEN_SIMD_TRANS_64(pmul_w_h00)
GEN_SIMD_TRANS_64(pmul_w_h01)
GEN_SIMD_TRANS_64(pmul_w_h11)
GEN_SIMD_TRANS_64(pmulsu_w_h00)
GEN_SIMD_TRANS_64(pmulsu_w_h11)
GEN_SIMD_TRANS_64(pmulu_w_h00)
GEN_SIMD_TRANS_64(pmulu_w_h01)
GEN_SIMD_TRANS_64(pmulu_w_h11)
GEN_SIMD_TRANS_VXSAT(pm2sadd_h)
GEN_SIMD_TRANS_VXSAT(pm2sadd_hx)
GEN_SIMD_TRANS_32(mul_h00)
GEN_SIMD_TRANS_32(mul_h01)
GEN_SIMD_TRANS_32(mul_h11)
GEN_SIMD_TRANS_32(mulsu_h00)
GEN_SIMD_TRANS_32(mulsu_h11)
GEN_SIMD_TRANS_32(mulu_h00)
GEN_SIMD_TRANS_32(mulu_h01)
GEN_SIMD_TRANS_32(mulu_h11)
GEN_SIMD_TRANS_64(mul_w00)
GEN_SIMD_TRANS_64(mul_w01)
GEN_SIMD_TRANS_64(mul_w11)
GEN_SIMD_TRANS_64(mulsu_w00)
GEN_SIMD_TRANS_64(mulsu_w11)
GEN_SIMD_TRANS_64(mulu_w00)
GEN_SIMD_TRANS_64(mulu_w01)
GEN_SIMD_TRANS_64(mulu_w11)

/* Packed SIMD - Multiply-Accumulate Operations */
GEN_SIMD_TRANS_ACC(pmhacc_h)
GEN_SIMD_TRANS_ACC(pmhaccsu_h)
GEN_SIMD_TRANS_ACC(pmhaccu_h)
GEN_SIMD_TRANS_ACC(pmhracc_h)
GEN_SIMD_TRANS_ACC(pmhraccsu_h)
GEN_SIMD_TRANS_ACC(pmhraccu_h)
GEN_SIMD_TRANS_ACC_64(pmhacc_w)
GEN_SIMD_TRANS_ACC_64(pmhracc_w)
GEN_SIMD_TRANS_ACC_64(pmhaccsu_w)
GEN_SIMD_TRANS_ACC_64(pmhraccsu_w)
GEN_SIMD_TRANS_ACC_64(pmhaccu_w)
GEN_SIMD_TRANS_ACC_64(pmhraccu_w)
GEN_SIMD_TRANS_ACC_32(mhacc)
GEN_SIMD_TRANS_ACC_32(mhracc)
GEN_SIMD_TRANS_ACC_32(mhaccsu)
GEN_SIMD_TRANS_ACC_32(mhraccsu)
GEN_SIMD_TRANS_ACC_32(mhaccu)
GEN_SIMD_TRANS_ACC_32(mhraccu)
GEN_SIMD_TRANS_ACC(pmhacc_h_b0)
GEN_SIMD_TRANS_ACC(pmhacc_h_b1)
GEN_SIMD_TRANS_ACC(pmhaccsu_h_b0)
GEN_SIMD_TRANS_ACC(pmhaccsu_h_b1)
GEN_SIMD_TRANS_ACC_32(mhacc_h0)
GEN_SIMD_TRANS_ACC_32(mhacc_h1)
GEN_SIMD_TRANS_ACC_32(mhaccsu_h0)
GEN_SIMD_TRANS_ACC_32(mhaccsu_h1)
GEN_SIMD_TRANS_ACC_64(pmhacc_w_h0)
GEN_SIMD_TRANS_ACC_64(pmhacc_w_h1)
GEN_SIMD_TRANS_ACC_64(pmhaccsu_w_h0)
GEN_SIMD_TRANS_ACC_64(pmhaccsu_w_h1)
GEN_SIMD_TRANS_ACC_64(pmacc_w_h00)
GEN_SIMD_TRANS_ACC_64(pmacc_w_h01)
GEN_SIMD_TRANS_ACC_64(pmacc_w_h11)
GEN_SIMD_TRANS_ACC_64(pmaccsu_w_h00)
GEN_SIMD_TRANS_ACC_64(pmaccsu_w_h11)
GEN_SIMD_TRANS_ACC_64(pmaccu_w_h00)
GEN_SIMD_TRANS_ACC_64(pmaccu_w_h01)
GEN_SIMD_TRANS_ACC_64(pmaccu_w_h11)
GEN_SIMD_TRANS_ACC_32(macc_h00)
GEN_SIMD_TRANS_ACC_32(macc_h01)
GEN_SIMD_TRANS_ACC_32(macc_h11)
GEN_SIMD_TRANS_ACC_32(maccsu_h00)
GEN_SIMD_TRANS_ACC_32(maccsu_h11)
GEN_SIMD_TRANS_ACC_32(maccu_h00)
GEN_SIMD_TRANS_ACC_32(maccu_h01)
GEN_SIMD_TRANS_ACC_32(maccu_h11)
GEN_SIMD_TRANS_ACC_64(macc_w00)
GEN_SIMD_TRANS_ACC_64(macc_w01)
GEN_SIMD_TRANS_ACC_64(macc_w11)
GEN_SIMD_TRANS_ACC_64(maccsu_w00)
GEN_SIMD_TRANS_ACC_64(maccsu_w11)
GEN_SIMD_TRANS_ACC_64(maccu_w00)
GEN_SIMD_TRANS_ACC_64(maccu_w01)
GEN_SIMD_TRANS_ACC_64(maccu_w11)

/* Packed SIMD - Q-Format Multiplication Operations */
GEN_SIMD_TRANS_VXSAT(pmulq_h)
GEN_SIMD_TRANS_VXSAT(pmulqr_h)
GEN_SIMD_TRANS_64_VXSAT(pmulq_w)
GEN_SIMD_TRANS_64_VXSAT(pmulqr_w)
GEN_SIMD_TRANS_32_VXSAT(mulq)
GEN_SIMD_TRANS_32_VXSAT(mulqr)

/* Packed SIMD - Q-Format Multiply-Accumulate Operations */
GEN_SIMD_TRANS_ACC_32(mqacc_h00)
GEN_SIMD_TRANS_ACC_32(mqacc_h01)
GEN_SIMD_TRANS_ACC_32(mqacc_h11)
GEN_SIMD_TRANS_ACC_32(mqracc_h00)
GEN_SIMD_TRANS_ACC_32(mqracc_h01)
GEN_SIMD_TRANS_ACC_32(mqracc_h11)
GEN_SIMD_TRANS_ACC_64(mqacc_w00)
GEN_SIMD_TRANS_ACC_64(mqacc_w01)
GEN_SIMD_TRANS_ACC_64(mqacc_w11)
GEN_SIMD_TRANS_ACC_64(mqracc_w00)
GEN_SIMD_TRANS_ACC_64(mqracc_w01)
GEN_SIMD_TRANS_ACC_64(mqracc_w11)
GEN_SIMD_TRANS_ACC_64(pmqacc_w_h00)
GEN_SIMD_TRANS_ACC_64(pmqacc_w_h01)
GEN_SIMD_TRANS_ACC_64(pmqacc_w_h11)
GEN_SIMD_TRANS_ACC_64(pmqracc_w_h00)
GEN_SIMD_TRANS_ACC_64(pmqracc_w_h01)
GEN_SIMD_TRANS_ACC_64(pmqracc_w_h11)

/* Packed SIMD - Two-Way Multiply and Accumulate Operations */
GEN_SIMD_TRANS(pmq2add_h)
GEN_SIMD_TRANS(pmqr2add_h)
GEN_SIMD_TRANS_ACC(pmq2adda_h)
GEN_SIMD_TRANS_ACC(pmqr2adda_h)
GEN_SIMD_TRANS_64(pmq2add_w)
GEN_SIMD_TRANS_64(pmqr2add_w)
GEN_SIMD_TRANS_ACC_64(pmq2adda_w)
GEN_SIMD_TRANS_ACC_64(pmqr2adda_w)
GEN_SIMD_TRANS(pm2add_h)
GEN_SIMD_TRANS(pm2addsu_h)
GEN_SIMD_TRANS(pm2addu_h)
GEN_SIMD_TRANS(pm2add_hx)
GEN_SIMD_TRANS(pm2sub_h)
GEN_SIMD_TRANS(pm2sub_hx)
GEN_SIMD_TRANS_ACC(pm2adda_h)
GEN_SIMD_TRANS_ACC(pm2addasu_h)
GEN_SIMD_TRANS_ACC(pm2addau_h)
GEN_SIMD_TRANS_ACC(pm2adda_hx)
GEN_SIMD_TRANS_ACC(pm2suba_h)
GEN_SIMD_TRANS_ACC(pm2suba_hx)
GEN_SIMD_TRANS_64(pm2add_w)
GEN_SIMD_TRANS_64(pm2addsu_w)
GEN_SIMD_TRANS_64(pm2addu_w)
GEN_SIMD_TRANS_64(pm2add_wx)
GEN_SIMD_TRANS_64(pm2sub_w)
GEN_SIMD_TRANS_64(pm2sub_wx)
GEN_SIMD_TRANS_ACC_64(pm2adda_w)
GEN_SIMD_TRANS_ACC_64(pm2addasu_w)
GEN_SIMD_TRANS_ACC_64(pm2addau_w)
GEN_SIMD_TRANS_ACC_64(pm2adda_wx)
GEN_SIMD_TRANS_ACC_64(pm2suba_w)
GEN_SIMD_TRANS_ACC_64(pm2suba_wx)

/* Packed SIMD - Four-Way Multiply and Accumulate Operations */
GEN_SIMD_TRANS(pm4add_b)
GEN_SIMD_TRANS(pm4addsu_b)
GEN_SIMD_TRANS(pm4addu_b)
GEN_SIMD_TRANS_ACC(pm4adda_b)
GEN_SIMD_TRANS_ACC(pm4addasu_b)
GEN_SIMD_TRANS_ACC(pm4addau_b)
GEN_SIMD_TRANS_64(pm4add_h)
GEN_SIMD_TRANS_64(pm4addsu_h)
GEN_SIMD_TRANS_64(pm4addu_h)
GEN_SIMD_TRANS_ACC_64(pm4adda_h)
GEN_SIMD_TRANS_ACC_64(pm4addasu_h)
GEN_SIMD_TRANS_ACC_64(pm4addau_h)

/* Packed SIMD - Double-Width Operations (RV32 only, register pairs) */
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwadd_b)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwadda_b)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwaddu_b)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwaddau_b)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwsub_b)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwsuba_b)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwsubu_b)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwsubau_b)
GEN_SIMD_TRANS_REG_PAIR_SCALAR_IMM(pwslli_b)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwsll_bs)
GEN_SIMD_TRANS_REG_PAIR_SCALAR_IMM(pwslai_b)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwsla_bs)

GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwadd_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwadda_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwaddu_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwaddau_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwsub_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwsuba_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwsubu_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwsubau_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR_IMM(pwslli_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwsll_hs)
GEN_SIMD_TRANS_REG_PAIR_SCALAR_IMM(pwslai_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwsla_hs)

GEN_SIMD_TRANS_REG_PAIR_SCALAR(wadd)
GEN_SIMD_TRANS_REG_PAIR_ACC(wadda)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(waddu)
GEN_SIMD_TRANS_REG_PAIR_ACC(waddau)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(wsub)
GEN_SIMD_TRANS_REG_PAIR_ACC(wsuba)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(wsubu)
GEN_SIMD_TRANS_REG_PAIR_ACC(wsubau)

GEN_SIMD_TRANS_REG_PAIR_SCALAR_IMM(wslli)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(wsll)
GEN_SIMD_TRANS_REG_PAIR_SCALAR_IMM(wslai)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(wsla)

GEN_SIMD_TRANS_REG_PAIR_LANE(padd_db, padd_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(psub_db, psub_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(psadd_db, psadd_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(psaddu_db, psaddu_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssub_db, pssub_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssubu_db, pssubu_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(paadd_db, paadd_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(paaddu_db, paaddu_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pasub_db, pasub_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pasubu_db, pasubu_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pabd_db, pabd_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pabdu_db, pabdu_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY_VXSAT(psabs_db, psabs_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(padd_dh, padd_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(psub_dh, psub_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(psadd_dh, psadd_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(psaddu_dh, psaddu_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssub_dh, pssub_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssubu_dh, pssubu_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(paadd_dh, paadd_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(paaddu_dh, paaddu_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pasub_dh, pasub_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pasubu_dh, pasubu_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(psh1add_dh, psh1add_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssh1sadd_dh, pssh1sadd_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pas_dhx, pas_hx)
GEN_SIMD_TRANS_REG_PAIR_LANE(psa_dhx, psa_hx)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(psas_dhx, psas_hx)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssa_dhx, pssa_hx)
GEN_SIMD_TRANS_REG_PAIR_LANE(paas_dhx, paas_hx)
GEN_SIMD_TRANS_REG_PAIR_LANE(pasa_dhx, pasa_hx)
GEN_SIMD_TRANS_REG_PAIR_LANE(pabd_dh, pabd_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pabdu_dh, pabdu_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY_VXSAT(psabs_dh, psabs_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(psadd_dw, sadd)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(psaddu_dw, saddu)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssub_dw, ssub)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssubu_dw, ssubu)
GEN_SIMD_TRANS_REG_PAIR_LANE(paadd_dw, aadd)
GEN_SIMD_TRANS_REG_PAIR_LANE(paaddu_dw, aaddu)
GEN_SIMD_TRANS_REG_PAIR_LANE(pasub_dw, asub)
GEN_SIMD_TRANS_REG_PAIR_LANE(pasubu_dw, asubu)
GEN_SIMD_TRANS_REG_PAIR_LANE_VXSAT(pssh1sadd_dw, ssh1sadd)

GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(pslli_db, pslli_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(psrli_db, psrli_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(psrai_db, psrai_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmin_db, pmin_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pminu_db, pminu_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmax_db, pmax_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmaxu_db, pmaxu_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmseq_db, pmseq_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmslt_db, pmslt_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmsltu_db, pmsltu_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_UNARY(psext_dh_b, psext_h_b)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM_VXSAT(psati_dh, psati_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM_VXSAT(pusati_dh, pusati_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(pslli_dh, pslli_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(psrli_dh, psrli_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(psrai_dh, psrai_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM_VXSAT(psslai_dh, psslai_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(psrari_dh, psrari_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmin_dh, pmin_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pminu_dh, pminu_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmax_dh, pmax_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmaxu_dh, pmaxu_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmseq_dh, pmseq_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmslt_dh, pmslt_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmsltu_dh, pmsltu_h)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM_VXSAT(psati_dw, sati_32)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM_VXSAT(pusati_dw, usati_32)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM_VXSAT(psslai_dw, sslai)
GEN_SIMD_TRANS_REG_PAIR_LANE_IMM(psrari_dw, srari_32)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmseq_dw, mseq)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmslt_dw, mslt)
GEN_SIMD_TRANS_REG_PAIR_LANE(pmsltu_dw, msltu)

GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(padd_dbs, padd_bs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(psll_dbs, psll_bs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(psrl_dbs, psrl_bs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(psra_dbs, psra_bs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(padd_dhs, padd_hs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(psll_dhs, psll_hs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(psrl_dhs, psrl_hs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR(psra_dhs, psra_hs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(pssha_dhs, pssha_hs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(psshar_dhs, psshar_hs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(psshl_dhs, psshl_hs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(psshlr_dhs, psshlr_hs)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(pssha_dws, ssha)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(psshar_dws, sshar)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(psshl_dws, sshl)
GEN_SIMD_TRANS_REG_PAIR_LANE_SCALAR_VXSAT(psshlr_dws, sshlr)

GEN_SIMD_TRANS_REG_PAIR_LANE(ppairo_db, ppairo_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(ppairo_dh, ppairo_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(ppaire_db, ppaire_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(ppaireo_db, ppaireo_b)
GEN_SIMD_TRANS_REG_PAIR_LANE(ppaireo_dh, ppaireo_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(ppairoe_dh, ppairoe_h)
GEN_SIMD_TRANS_REG_PAIR_LANE(ppairoe_db, ppairoe_b)

GEN_SIMD_TRANS_REG_PAIR_PREDSUM(predsum_dbs)
GEN_SIMD_TRANS_REG_PAIR_PREDSUM(predsumu_dbs)
GEN_SIMD_TRANS_REG_PAIR_PREDSUM(predsum_dhs)
GEN_SIMD_TRANS_REG_PAIR_PREDSUM(predsumu_dhs)

GEN_SIMD_TRANS_PN_OP_IMM(pnsrli_b)
GEN_SIMD_TRANS_PN_OP_IMM(pnsrai_b)
GEN_SIMD_TRANS_PN_OP_IMM(pnsrari_b)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(pnclipi_b)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(pnclipri_b)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(pnclipiu_b)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(pnclipriu_b)

GEN_SIMD_TRANS_PN_OP_IMM(pnsrli_h)
GEN_SIMD_TRANS_PN_OP_IMM(pnsrai_h)
GEN_SIMD_TRANS_PN_OP_IMM(pnsrari_h)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(pnclipi_h)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(pnclipri_h)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(pnclipiu_h)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(pnclipriu_h)

GEN_SIMD_TRANS_PN_OP_IMM(nsrli)
GEN_SIMD_TRANS_PN_OP_IMM(nsrai)
GEN_SIMD_TRANS_PN_OP_IMM(nsrari)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(nclipi)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(nclipri)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(nclipiu)
GEN_SIMD_TRANS_PN_OP_IMM_VXSAT(nclipriu)

GEN_SIMD_TRANS_PN_OP_REG(pnsrl_bs)
GEN_SIMD_TRANS_PN_OP_REG(pnsra_bs)
GEN_SIMD_TRANS_PN_OP_REG(pnsrar_bs)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(pnclip_bs)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(pnclipr_bs)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(pnclipu_bs)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(pnclipru_bs)

GEN_SIMD_TRANS_PN_OP_REG(pnsrl_hs)
GEN_SIMD_TRANS_PN_OP_REG(pnsra_hs)
GEN_SIMD_TRANS_PN_OP_REG(pnsrar_hs)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(pnclip_hs)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(pnclipr_hs)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(pnclipu_hs)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(pnclipru_hs)

GEN_SIMD_TRANS_PN_OP_REG(nsrl)
GEN_SIMD_TRANS_PN_OP_REG(nsra)
GEN_SIMD_TRANS_PN_OP_REG(nsrar)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(nclip)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(nclipr)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(nclipu)
GEN_SIMD_TRANS_PN_OP_REG_VXSAT(nclipru)

GEN_SIMD_TRANS_REG_PAIR_ACC(pmqwacc_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pmqrwacc_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(mqwacc)
GEN_SIMD_TRANS_REG_PAIR_ACC(mqrwacc)

GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwmul_b)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwmulsu_b)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwmulu_b)

GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwmul_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwmulsu_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pwmulu_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwmacc_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwmaccsu_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pwmaccu_h)

GEN_SIMD_TRANS_REG_PAIR_SCALAR(wmul)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(wmulsu)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(wmulu)

GEN_SIMD_TRANS_REG_PAIR_ACC(wmacc)
GEN_SIMD_TRANS_REG_PAIR_ACC(wmaccsu)
GEN_SIMD_TRANS_REG_PAIR_ACC(wmaccu)

GEN_SIMD_TRANS_REG_PAIR_SCALAR(pm2wadd_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pm2waddsu_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pm2waddu_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pm2wadd_hx)

GEN_SIMD_TRANS_REG_PAIR_ACC(pm2wadda_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pm2waddasu_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pm2waddau_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pm2wadda_hx)

GEN_SIMD_TRANS_REG_PAIR_SCALAR(pm2wsub_h)
GEN_SIMD_TRANS_REG_PAIR_SCALAR(pm2wsub_hx)
GEN_SIMD_TRANS_REG_PAIR_ACC(pm2wsuba_h)
GEN_SIMD_TRANS_REG_PAIR_ACC(pm2wsuba_hx)

#define GEN_PLI(NAME, PRE_REQ, IMM_TYPE, LIMIT, SHIFT, ADDEND) \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)    \
{                                                              \
    PRE_REQ                                                    \
    REQUIRE_RVP(ctx);                                          \
    int i = 1;                                                 \
    IMM_TYPE imm = a->imm;                                     \
    while (i < (LIMIT)) {                                      \
        imm = (imm << (SHIFT)) + (ADDEND);                     \
        i++;                                                   \
    }                                                          \
    gen_set_gpri(ctx, a->rd, imm);                             \
    return true;                                               \
}

#define GEN_PLI_D(NAME, IMM_TYPE, LIMIT, SHIFT, ADDEND)         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)    \
{                                                              \
    REQUIRE_RVP(ctx);                                          \
    int i = 1;                                                 \
    IMM_TYPE imm = a->imm;                                     \
    while (i < (LIMIT)) {                                      \
        imm = (imm << (SHIFT)) + (ADDEND);                     \
        i++;                                                   \
    }                                                          \
    gen_set_gpri(ctx, (a->rd) * 2, imm);                       \
    gen_set_gpri(ctx, (a->rd) * 2 + 1, imm);                   \
    return true;                                               \
}

GEN_PLI(pli_b, , target_long, TARGET_LONG_SIZE, 8, a->imm)
GEN_PLI(pli_h, , target_long, TARGET_LONG_SIZE / 2, 16,
        a->imm & 0xFFFF)
GEN_PLI(plui_h, , target_long, TARGET_LONG_SIZE / 2, 16,
        a->imm & 0xFFFF)
GEN_PLI(pli_w, REQUIRE_64BIT(ctx); , int64_t, TARGET_LONG_SIZE / 4, 32,
        a->imm & 0xFFFFFFFF)
GEN_PLI(plui_w, REQUIRE_64BIT(ctx); , int64_t, TARGET_LONG_SIZE / 4, 32,
        a->imm & 0xFFFFFFFF)
GEN_PLI_D(pli_db, target_long, TARGET_LONG_SIZE, 8, a->imm)
GEN_PLI_D(pli_dh, target_long, TARGET_LONG_SIZE / 2, 16,
          a->imm & 0xFFFF)
GEN_PLI_D(plui_dh, target_long, TARGET_LONG_SIZE / 2, 16,
          a->imm & 0xFFFF)

#define GEN_DW_LANE_BINOP(NAME, OP)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);     \
    TCGv src2_0 = get_gpr(ctx, (a->rs2) * 2, EXT_NONE);     \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);               \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE); \
    TCGv src2_1 = get_gpr(ctx, (a->rs2) * 2 + 1, EXT_NONE); \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);           \
    OP(dest_0, src1_0, src2_0);                             \
    OP(dest_1, src1_1, src2_1);                             \
    gen_set_gpr(ctx, (a->rd) * 2, dest_0);                  \
    gen_set_gpr(ctx, (a->rd) * 2 + 1, dest_1);              \
    return true;                                            \
}

GEN_DW_LANE_BINOP(padd_dw, tcg_gen_add_tl)
GEN_DW_LANE_BINOP(psub_dw, tcg_gen_sub_tl)
GEN_DW_LANE_BINOP(psh1add_dw, gen_sh1add)

/* Verify rd is not zero register for wzip8p and wzip16p. */
#if defined(TARGET_RISCV32)
#define GEN_WZIP_P(NAME)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                            \
    REQUIRE_32BIT(ctx);                                      \
    REQUIRE_RVP(ctx);                                        \
    TCGv_i32 src1 = get_gpr(ctx, a->rs1, EXT_NONE);          \
    TCGv_i32 src2 = get_gpr(ctx, a->rs2, EXT_NONE);          \
    TCGv_i64 t = tcg_temp_new_i64();                         \
    if (a->rd == 0) {                                        \
        return true;                                         \
    } else {                                                 \
        get_pair_regs(ctx, t, (a->rd) * 2);                  \
    }                                                        \
    gen_helper_##NAME(t, tcg_env, src1, src2);               \
    set_pair_regs(ctx, (a->rd) * 2, t);                      \
    return true;                                             \
}
#else
#define GEN_WZIP_P(NAME)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                            \
    REQUIRE_32BIT(ctx);                                      \
    return true;                                             \
}
#endif

GEN_WZIP_P(wzip8p)
GEN_WZIP_P(wzip16p)

#define GEN_PAIR_I64_BINOP(NAME, OP)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                            \
    REQUIRE_32BIT(ctx);                                      \
    REQUIRE_RVP(ctx);                                        \
    TCGv_i64 src1 = tcg_temp_new_i64();                      \
    TCGv_i64 src2 = tcg_temp_new_i64();                      \
    TCGv_i64 dest = tcg_temp_new_i64();                      \
    get_pair_regs(ctx, src1, (a->rs1) * 2);                  \
    get_pair_regs(ctx, src2, (a->rs2) * 2);                  \
    get_pair_regs(ctx, dest, (a->rd) * 2);                   \
    OP(dest, src1, src2);                                    \
    set_pair_regs(ctx, (a->rd) * 2, dest);                   \
    return true;                                             \
}

GEN_PAIR_I64_BINOP(addd_p, tcg_gen_add_i64)
GEN_PAIR_I64_BINOP(subd_p, tcg_gen_sub_i64)

#define GEN_DW_EXT(NAME, OP)                                \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);               \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);     \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);           \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE); \
    OP(dest_0, src1_0);                                     \
    gen_set_gpr(ctx, (a->rd) * 2, dest_0);                  \
    OP(dest_1, src1_1);                                     \
    gen_set_gpr(ctx, (a->rd) * 2 + 1, dest_1);              \
    return true;                                            \
}

GEN_DW_EXT(psext_dw_b, tcg_gen_ext8s_tl)
GEN_DW_EXT(psext_dw_h, tcg_gen_ext16s_tl)

#define GEN_DW_SHIFT_IMM(NAME, OP)                          \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    arg_shift a0, a1;                                       \
    a0.rd = (a->rd) * 2;                                    \
    a0.rs1 = (a->rs1) * 2;                                  \
    a0.shamt = a->imm;                                      \
    a1.rd = (a->rd) * 2 + 1;                                \
    a1.rs1 = (a->rs1) * 2 + 1;                              \
    a1.shamt = a->imm;                                      \
    gen_shift_imm_fn(ctx, &a0, EXT_NONE, OP, NULL);         \
    gen_shift_imm_fn(ctx, &a1, EXT_NONE, OP, NULL);         \
    return true;                                            \
}

#define GEN_DW_SHIFT_IMM_PER_OL(NAME, OP, OP_OL)                  \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)       \
{                                                                 \
    REQUIRE_32BIT(ctx);                                           \
    REQUIRE_RVP(ctx);                                             \
    arg_shift a0, a1;                                             \
    a0.rd = (a->rd) * 2;                                          \
    a0.rs1 = (a->rs1) * 2;                                        \
    a0.shamt = a->imm;                                            \
    a1.rd = (a->rd) * 2 + 1;                                      \
    a1.rs1 = (a->rs1) * 2 + 1;                                    \
    a1.shamt = a->imm;                                            \
    gen_shift_imm_fn_per_ol(ctx, &a0, EXT_NONE, OP, OP_OL, NULL); \
    gen_shift_imm_fn_per_ol(ctx, &a1, EXT_NONE, OP, OP_OL, NULL); \
    return true;                                                  \
}

GEN_DW_SHIFT_IMM(pslli_dw, tcg_gen_shli_tl)
GEN_DW_SHIFT_IMM_PER_OL(psrli_dw, tcg_gen_shri_tl, gen_srliw)
GEN_DW_SHIFT_IMM_PER_OL(psrai_dw, tcg_gen_sari_tl, gen_sraiw)

#define GEN_DW_PAIR_ARITH(NAME, EXTRA_REQ, EXT, OP)         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    EXTRA_REQ                                               \
    arg_r a0, a1;                                           \
    a0.rd = (a->rd) * 2;                                    \
    a0.rs1 = (a->rs1) * 2;                                  \
    a0.rs2 = (a->rs2) * 2;                                  \
    a1.rd = (a->rd) * 2 + 1;                                \
    a1.rs1 = (a->rs1) * 2 + 1;                              \
    a1.rs2 = (a->rs2) * 2 + 1;                              \
    gen_arith(ctx, &a0, EXT, OP, NULL);                     \
    gen_arith(ctx, &a1, EXT, OP, NULL);                     \
    return true;                                            \
}

GEN_DW_PAIR_ARITH(pmin_dw, REQUIRE_ZBB(ctx); , EXT_SIGN, tcg_gen_smin_tl)
GEN_DW_PAIR_ARITH(pminu_dw, REQUIRE_ZBB(ctx); , EXT_SIGN, tcg_gen_umin_tl)
GEN_DW_PAIR_ARITH(pmax_dw, REQUIRE_ZBB(ctx); , EXT_SIGN, tcg_gen_smax_tl)
GEN_DW_PAIR_ARITH(pmaxu_dw, REQUIRE_ZBB(ctx); , EXT_SIGN, tcg_gen_umax_tl)

#define GEN_DWS_REG_OP(NAME, GEN, EXT, OP)                  \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_RVP(ctx);                                       \
    arg_r a0, a1;                                           \
    a0.rd = (a->rd) * 2;                                    \
    a0.rs1 = (a->rs1) * 2;                                  \
    a0.rs2 = a->rs2;                                        \
    a1.rd = (a->rd) * 2 + 1;                                \
    a1.rs1 = (a->rs1) * 2 + 1;                              \
    a1.rs2 = a->rs2;                                        \
    GEN(ctx, &a0, EXT, OP, NULL);                           \
    GEN(ctx, &a1, EXT, OP, NULL);                           \
    return true;                                            \
}

GEN_DWS_REG_OP(padd_dws, gen_arith, EXT_NONE, tcg_gen_add_tl)
GEN_DWS_REG_OP(psll_dws, gen_shift, EXT_NONE, tcg_gen_shl_tl)
GEN_DWS_REG_OP(psrl_dws, gen_shift, EXT_ZERO, tcg_gen_shr_tl)
GEN_DWS_REG_OP(psra_dws, gen_shift, EXT_SIGN, tcg_gen_sar_tl)

GEN_DW_PAIR_ARITH(ppaire_dh, , EXT_NONE, gen_pack)
