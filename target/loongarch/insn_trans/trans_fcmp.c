/*
 * LoongArch translate functions
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

static bool trans_fcmp_cond_s(DisasContext *ctx, arg_fcmp_cond_s *a)
{
    TCGv var = tcg_temp_new();
    TCGv_i32 flags = NULL;

    switch (a->fcond) {
    /* caf */
    case  0:
        flags = tcg_constant_i32(0);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* saf */
    case 1:
        flags = tcg_constant_i32(0);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* clt */
    case 2:
        flags = tcg_constant_i32(FCMP_LT);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* slt */
    case 3:
        flags = tcg_constant_i32(FCMP_LT);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* ceq */
    case 4:
        flags = tcg_constant_i32(FCMP_EQ);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* seq */
    case 5:
        flags = tcg_constant_i32(FCMP_EQ);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* cle */
    case 6:
        flags = tcg_constant_i32(FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* sle */
    case 7:
        flags = tcg_constant_i32(FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* cun */
    case 8:
        flags = tcg_constant_i32(FCMP_UN);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* sun */
    case 9:
        flags = tcg_constant_i32(FCMP_UN);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* cult */
    case 10:
        flags = tcg_constant_i32(FCMP_UN | FCMP_LT);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* sult */
    case 11:
        flags = tcg_constant_i32(FCMP_UN | FCMP_LT);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* cueq */
    case 12:
        flags = tcg_constant_i32(FCMP_UN | FCMP_EQ);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* sueq */
    case 13:
        flags = tcg_constant_i32(FCMP_UN | FCMP_EQ);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* cule */
    case 14:
        flags = tcg_constant_i32(FCMP_UN | FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* sule */
    case 15:
        flags = tcg_constant_i32(FCMP_UN | FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* cne */
    case 16:
        flags = tcg_constant_i32(FCMP_GT | FCMP_LT);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* sne */
    case 17:
        flags = tcg_constant_i32(FCMP_GT | FCMP_LT);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* cor */
    case 20:
        flags = tcg_constant_i32(FCMP_GT | FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* sor */
    case 21:
        flags = tcg_constant_i32(FCMP_GT | FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* cune */
    case 24:
        flags = tcg_constant_i32(FCMP_UN | FCMP_GT | FCMP_LT);
        gen_helper_fcmp_c_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    /* sune */
    case 25:
        flags = tcg_constant_i32(FCMP_UN | FCMP_GT | FCMP_LT);
        gen_helper_fcmp_s_s(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    default:
        abort();
    }

    tcg_gen_st8_tl(var, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7]));
    tcg_temp_free(var);
    return true;
}

static bool trans_fcmp_cond_d(DisasContext *ctx, arg_fcmp_cond_d *a)
{
    TCGv var = tcg_temp_new();
    TCGv_i32 flags = NULL;

    switch (a->fcond) {
    case 0:
        flags = tcg_constant_i32(0);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 1:
        flags = tcg_constant_i32(0);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 2:
        flags = tcg_constant_i32(FCMP_LT);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 3:
        flags = tcg_constant_i32(FCMP_LT);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 4:
        flags = tcg_constant_i32(FCMP_EQ);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 5:
        flags = tcg_constant_i32(FCMP_EQ);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 6:
        flags = tcg_constant_i32(FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 7:
        flags = tcg_constant_i32(FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 8:
        flags = tcg_constant_i32(FCMP_UN);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 9:
        flags = tcg_constant_i32(FCMP_UN);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 10:
        flags = tcg_constant_i32(FCMP_UN | FCMP_LT);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 11:
        flags = tcg_constant_i32(FCMP_UN | FCMP_LT);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 12:
        flags = tcg_constant_i32(FCMP_UN | FCMP_EQ);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 13:
        flags = tcg_constant_i32(FCMP_UN | FCMP_EQ);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 14:
        flags = tcg_constant_i32(FCMP_UN | FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 15:
        flags = tcg_constant_i32(FCMP_UN | FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 16:
        flags = tcg_constant_i32(FCMP_GT | FCMP_LT);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 17:
        flags = tcg_constant_i32(FCMP_GT | FCMP_LT);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 20:
        flags = tcg_constant_i32(FCMP_GT | FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 21:
        flags = tcg_constant_i32(FCMP_GT | FCMP_LT | FCMP_EQ);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 24:
        flags = tcg_constant_i32(FCMP_UN | FCMP_GT | FCMP_LT);
        gen_helper_fcmp_c_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    case 25:
        flags = tcg_constant_i32(FCMP_UN | FCMP_GT | FCMP_LT);
        gen_helper_fcmp_s_d(var, cpu_env, cpu_fpr[a->fj],
                            cpu_fpr[a->fk], flags);
        break;
    default:
        abort();
    }

    tcg_gen_st8_tl(var, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7]));
    tcg_temp_free(var);
    return true;
}
