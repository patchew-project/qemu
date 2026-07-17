/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RISC-V translation routines for the P Standard Extensions. */
/* Copyright (c) 2026 ISRC ISCAS. */

/* Save a 64 bit data in src to dst and dst + 1 */

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

