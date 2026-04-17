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
GEN_SIMD_TRANS_IMM(psslai_h)
GEN_SIMD_TRANS_IMM_64(psslai_w)
GEN_SIMD_TRANS_IMM_32(sslai)
GEN_SIMD_TRANS_IMM(psrari_h)
GEN_SIMD_TRANS_IMM_64(psrari_w)
GEN_SIMD_TRANS_IMM_32(srari_32)
GEN_SIMD_TRANS_IMM_64(srari_64)
GEN_SIMD_TRANS(pssha_hs)
GEN_SIMD_TRANS_64(pssha_ws)
GEN_SIMD_TRANS(psshar_hs)
GEN_SIMD_TRANS_64(psshar_ws)
GEN_SIMD_TRANS_32(ssha)
GEN_SIMD_TRANS_32(sshar)
GEN_SIMD_TRANS_64(sha)
GEN_SIMD_TRANS_64(shar)

/* Packed SIMD - Exchange Operations */
GEN_SIMD_TRANS(pas_hx)
GEN_SIMD_TRANS(psa_hx)
GEN_SIMD_TRANS(psas_hx)
GEN_SIMD_TRANS(pssa_hx)
GEN_SIMD_TRANS(paas_hx)
GEN_SIMD_TRANS(pasa_hx)
GEN_SIMD_TRANS_64(pas_wx)
GEN_SIMD_TRANS_64(psa_wx)
GEN_SIMD_TRANS_64(psas_wx)
GEN_SIMD_TRANS_64(pssa_wx)
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
GEN_SIMD_TRANS(pm2sadd_h)
GEN_SIMD_TRANS(pm2sadd_hx)
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
GEN_SIMD_TRANS(pmulq_h)
GEN_SIMD_TRANS(pmulqr_h)
GEN_SIMD_TRANS_64(pmulq_w)
GEN_SIMD_TRANS_64(pmulqr_w)
GEN_SIMD_TRANS_32(mulq)
GEN_SIMD_TRANS_32(mulqr)

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

static bool trans_pli_b(DisasContext *ctx, arg_pli_b * a)
{
    REQUIRE_EXT(ctx, RVP);
    int i = 1;
    target_long imm = a->imm;
    while (i < TARGET_LONG_SIZE) {
        imm = ((imm << 8) + a->imm);
        i++;
    }
    gen_set_gpri(ctx, a->rd, imm);
    return true;
}

static bool trans_pli_h(DisasContext *ctx, arg_pli_h * a)
{
    REQUIRE_EXT(ctx, RVP);
    int i = 1;
    target_long imm = a->imm;
    while (i < TARGET_LONG_SIZE / 2) {
        imm = (imm << 16) + (a->imm & 0xFFFF);
        i++;
    }
    gen_set_gpri(ctx, a->rd, imm);
    return true;
}

static bool trans_plui_h(DisasContext *ctx, arg_plui_h * a)
{
    REQUIRE_EXT(ctx, RVP);
    int i = 1;
    target_long imm = a->imm;
    while (i < TARGET_LONG_SIZE / 2) {
        imm = (imm << 16) + (a->imm & 0xFFFF);
        i++;
    }
    gen_set_gpri(ctx, a->rd, imm);
    return true;
}

static bool trans_pli_w(DisasContext *ctx, arg_pli_w * a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVP);
    int i = 1;
    int64_t imm = a->imm;
    while (i < TARGET_LONG_SIZE / 4) {
        imm = (imm << 32) + (a->imm & 0xFFFFFFFF);
        i++;
    }
    gen_set_gpri(ctx, a->rd, imm);
    return true;
}

static bool trans_plui_w(DisasContext *ctx, arg_plui_w * a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVP);
    int i = 1;
    int64_t imm = a->imm;
    while (i < TARGET_LONG_SIZE / 4) {
        imm = (imm << 32) + (a->imm & 0xFFFFFFFF);
        i++;
    }
    gen_set_gpri(ctx, a->rd, imm);
    return true;
}
