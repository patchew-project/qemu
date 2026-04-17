/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RISC-V translation routines for the P Standard Extensions. */
/* Copyright (c) 2026 ISRC ISCAS. */

#define GEN_SIMD_TRANS(NAME)                                \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
   REQUIRE_EXT(ctx, RVP);                                   \
   TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);              \
   TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);              \
   TCGv dest = dest_gpr(ctx, a->rd);                        \
   gen_helper_##NAME(dest, tcg_env, src1, src2);            \
   return true;                                             \
}

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_32(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1, src2);           \
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_32(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    return true;                                            \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_64(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                           \
   REQUIRE_64BIT(ctx);                                      \
   return true;                                             \
}
#else
#define GEN_SIMD_TRANS_64(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a)  \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1, src2);           \
    return true;                                            \
}
#endif

#define GEN_SIMD_TRANS_ACC(NAME)                            \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    TCGv t = tcg_temp_new();                                \
    gen_helper_##NAME(t, tcg_env, src1, src2, dest);        \
    gen_set_gpr(ctx, a->rd, t);                             \
    return true;                                            \
}

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_ACC_32(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    TCGv t = tcg_temp_new();                                \
    gen_helper_##NAME(t, tcg_env, src1, src2, dest);        \
    gen_set_gpr(ctx, a->rd, t);                             \
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_ACC_32(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    return true;                                            \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_ACC_64(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_ACC_64(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    TCGv t = tcg_temp_new();                                \
    gen_helper_##NAME(t, tcg_env, src1, src2, dest);        \
    gen_set_gpr(ctx, a->rd, t);                             \
    return true;                                            \
}
#endif

#define GEN_SIMD_TRANS_R1(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1);                 \
    gen_set_gpr(ctx, a->rd, dest);                          \
    return true;                                            \
}

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_R1_64(NAME)                          \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_R1_64(NAME)                          \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1);                 \
    return true;                                            \
}
#endif

#define GEN_SIMD_TRANS_IMM(NAME)                            \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv imm = tcg_constant_tl(a->imm);                     \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1, imm);            \
    return true;                                            \
}

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_IMM_32(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                   \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv imm = tcg_constant_tl(a->imm);                     \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1, imm);            \
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_IMM_32(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    return true;                                            \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_IMM_64(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_IMM_64(NAME)                         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_64BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                   \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv imm = tcg_constant_tl(a->imm);                     \
    TCGv dest = dest_gpr(ctx, a->rd);                       \
    gen_helper_##NAME(dest, tcg_env, src1, imm);            \
    return true;                                            \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_1(NAME)                     \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv_i32 src1 = get_gpr(ctx, a->rs1, EXT_NONE);         \
    TCGv_i32 src2 = get_gpr(ctx, a->rs2, EXT_NONE);         \
    TCGv_i64 t = tcg_temp_new_i64();                        \
    gen_helper_##NAME(t, tcg_env, src1, src2);              \
    set_pair_regs(ctx, (a->rd) * 2, t);                       \
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_1(NAME)                     \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    return true;                                            \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_2(INSN, HELPER)                \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)    \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    REQUIRE_EXT(ctx, RVP);                                     \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);          \
    TCGv src2_0 = get_gpr(ctx, (a->rs2) * 2, EXT_NONE);          \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                    \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);        \
    TCGv src2_1 = get_gpr(ctx, (a->rs2) * 2 + 1, EXT_NONE);        \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                  \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0, src2_0);      \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1, src2_1);      \
    return true;                                               \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_2(INSN, HELPER)                \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)    \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    return true;                                               \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_3(INSN, HELPER)                \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)    \
{                                                              \
    REQUIRE_EXT(ctx, RVP);                                     \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);          \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                    \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);        \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                  \
    TCGv src2   = get_gpr(ctx, a->rs2, EXT_NONE);              \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0, src2);        \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1, src2);        \
    return true;                                               \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_3(INSN, HELPER)                \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)    \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    return true;                                               \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_DW(INSN, HELPER)               \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)     \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    REQUIRE_EXT(ctx, RVP);                                     \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);          \
    TCGv src2_0 = get_gpr(ctx, (a->rs2) * 2, EXT_NONE);          \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                    \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);        \
    TCGv src2_1 = get_gpr(ctx, (a->rs2) * 2 + 1, EXT_NONE);        \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                  \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0, src2_0);      \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1, src2_1);      \
    return true;                                               \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_DW(INSN, HELPER)               \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)     \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    return true;                                               \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_DW_IMM(INSN, HELPER)           \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)     \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    REQUIRE_EXT(ctx, RVP);                                     \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);          \
    TCGv imm_0 = tcg_constant_tl(a->imm);                      \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                    \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);        \
    TCGv imm_1 = tcg_constant_tl(a->imm);                      \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                  \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0, imm_0);       \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1, imm_1);       \
    return true;                                               \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_DW_IMM(INSN, HELPER)           \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)     \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    return true;                                               \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_DW_IMM_2(INSN, HELPER)         \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)     \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    REQUIRE_EXT(ctx, RVP);                                     \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);          \
    TCGv imm_0 = tcg_constant_tl(a->imm);                      \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                    \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);        \
    TCGv imm_1 = tcg_constant_tl(a->imm);                      \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                  \
    gen_helper_##HELPER##_32(dest_0, tcg_env, src1_0, imm_0);  \
    gen_helper_##HELPER##_32(dest_1, tcg_env, src1_1, imm_1);  \
    return true;                                               \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_DW_IMM_2(INSN, HELPER)         \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)     \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    return true;                                               \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_5(INSN, HELPER)                \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)    \
{                                                              \
    REQUIRE_EXT(ctx, RVP);                                     \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);          \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                    \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);        \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                  \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0);              \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1);              \
    gen_set_gpr(ctx, (a->rd) * 2, dest_0);                       \
    gen_set_gpr(ctx, (a->rd) * 2 + 1, dest_1);                     \
    return true;                                               \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_5(INSN, HELPER)                \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)    \
{                                                              \
    REQUIRE_32BIT(ctx);                                        \
    return true;                                               \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_IMM(NAME)                   \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);             \
    TCGv_i32 imm = tcg_constant_i32(a->imm);                \
    TCGv_i64 t = tcg_temp_new_i64();                        \
    gen_helper_##NAME(t, tcg_env, src1, imm);               \
    set_pair_regs(ctx, (a->rd) * 2, t);                       \
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_IMM(NAME)                   \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    return true;                                            \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_IMM_2(INSN, HELPER)          \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)  \
{                                                            \
    REQUIRE_32BIT(ctx);                                      \
    REQUIRE_EXT(ctx, RVP);                                   \
    TCGv src1_0 = get_gpr(ctx, (a->rs1) * 2, EXT_NONE);        \
    TCGv imm_0 = tcg_constant_tl(a->imm);                    \
    TCGv dest_0 = dest_gpr(ctx, (a->rd) * 2);                  \
    TCGv src1_1 = get_gpr(ctx, (a->rs1) * 2 + 1, EXT_NONE);      \
    TCGv imm_1 = tcg_constant_tl(a->imm);                    \
    TCGv dest_1 = dest_gpr(ctx, (a->rd) * 2 + 1);                \
    gen_helper_##HELPER(dest_0, tcg_env, src1_0, imm_0);     \
    gen_helper_##HELPER(dest_1, tcg_env, src1_1, imm_1);     \
    return true;                                             \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_IMM_2(INSN, HELPER)          \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a)  \
{                                                            \
    REQUIRE_32BIT(ctx);                                      \
    return true;                                             \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_ACC_REG_PAIR_1(NAME)                 \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
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
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_ACC_REG_PAIR_1(NAME)                 \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    return true;                                            \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_REG_PAIR_PREDSUM(NAME)               \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    REQUIRE_EXT(ctx, RVP);                                  \
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
    return true;                                            \
}
#else
#define GEN_SIMD_TRANS_REG_PAIR_PREDSUM(NAME)               \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                           \
    REQUIRE_32BIT(ctx);                                     \
    return true;                                            \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_PN_OP_IMM(NAME)                     \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                          \
    REQUIRE_32BIT(ctx);                                    \
    REQUIRE_EXT(ctx, RVP);                                 \
    TCGv_i64 s1 = tcg_temp_new_i64();                      \
    if (a->rs1 == 0) {                                     \
        tcg_gen_mov_i64(s1, 0);                            \
    } else {                                               \
        get_pair_regs(ctx, s1, a->rs1 * 2);                \
    }                                                      \
    TCGv shamt = tcg_constant_tl(a->imm);                  \
    TCGv_i32 dest = dest_gpr(ctx, a->rd);                  \
    gen_helper_##NAME(dest, tcg_env, s1, shamt);           \
    return true;                                           \
}
#else
#define GEN_SIMD_TRANS_PN_OP_IMM(NAME)                     \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                          \
    REQUIRE_32BIT(ctx);                                    \
    return true;                                           \
}
#endif

#if defined(TARGET_RISCV32)
#define GEN_SIMD_TRANS_PN_OP_REG(NAME)                     \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                          \
    REQUIRE_32BIT(ctx);                                    \
    REQUIRE_EXT(ctx, RVP);                                 \
    TCGv_i64 s1 = tcg_temp_new_i64();                      \
    if (a->rs1 == 0) {                                     \
        tcg_gen_mov_i64(s1, 0);                            \
    } else {                                               \
        get_pair_regs(ctx, s1, a->rs1 * 2);                \
    }                                                      \
    TCGv_i32 rs2 = get_gpr(ctx, a->rs2, EXT_NONE);         \
    TCGv_i32 dest = dest_gpr(ctx, a->rd);                  \
    gen_helper_##NAME(dest, tcg_env, s1, rs2);             \
    return true;                                           \
}
#else
#define GEN_SIMD_TRANS_PN_OP_REG(NAME)                     \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
{                                                          \
    REQUIRE_32BIT(ctx);                                    \
    return true;                                           \
}
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
GEN_SIMD_TRANS(pssh1sadd_h)
GEN_SIMD_TRANS_64(pssh1sadd_w)
GEN_SIMD_TRANS_32(ssh1sadd)
GEN_SIMD_TRANS(psadd_b)
GEN_SIMD_TRANS(psadd_h)
GEN_SIMD_TRANS_64(psadd_w)
GEN_SIMD_TRANS(psaddu_b)
GEN_SIMD_TRANS(psaddu_h)
GEN_SIMD_TRANS_64(psaddu_w)
GEN_SIMD_TRANS_32(sadd)
GEN_SIMD_TRANS_32(saddu)
GEN_SIMD_TRANS(pssub_b)
GEN_SIMD_TRANS(pssub_h)
GEN_SIMD_TRANS_64(pssub_w)
GEN_SIMD_TRANS(pssubu_b)
GEN_SIMD_TRANS(pssubu_h)
GEN_SIMD_TRANS_64(pssubu_w)
GEN_SIMD_TRANS_32(ssub)
GEN_SIMD_TRANS_32(ssubu)
GEN_SIMD_TRANS_IMM(psati_h)
GEN_SIMD_TRANS_IMM(pusati_h)
GEN_SIMD_TRANS_IMM_64(psati_w)
GEN_SIMD_TRANS_IMM_64(pusati_w)
GEN_SIMD_TRANS_IMM_32(sati_32)
GEN_SIMD_TRANS_IMM_32(usati_32)
GEN_SIMD_TRANS_IMM_64(sati_64)
GEN_SIMD_TRANS_IMM_64(usati_64)

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
GEN_SIMD_TRANS_R1(psabs_b)
GEN_SIMD_TRANS_R1(psabs_h)
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

