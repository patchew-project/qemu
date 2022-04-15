/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * LoongArch translation routines for the privileged instructions.
 */

#include "cpu-csr.h"

static void gen_disas_exit(DisasContext *ctx)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next + 4);
    ctx->base.is_jmp = DISAS_EXIT;
}

static bool check_plv(DisasContext *ctx)
{
    if (ctx->base.tb->flags == MMU_USER_IDX) {
        generate_exception(ctx, EXCCODE_IPE);
        return true;
    }
    return false;
}

static bool ro_csr(int csr_num)
{
    /*
     * For now qemu does not support any features of the MISC
     * bits yet treat as a RO CSR.
     */
    if ((csr_num == LOONGARCH_CSR_BADI) ||
        (csr_num == LOONGARCH_CSR_CPUID) ||
        (csr_num == LOONGARCH_CSR_PRCFG1) ||
        (csr_num == LOONGARCH_CSR_PRCFG2) ||
        (csr_num == LOONGARCH_CSR_PRCFG3) ||
        (csr_num == LOONGARCH_CSR_PGD) ||
        (csr_num == LOONGARCH_CSR_TVAL) ||
        (csr_num == LOONGARCH_CSR_MISC)) {
        return true;
    }

    return false;
}

static bool trans_csrrd(DisasContext *ctx, arg_csrrd *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }

    unsigned csr_offset = cpu_csr_offset(a->csr);
    if (csr_offset == 0) {
        /* CSR is undefined: read as 0 */
        dest = tcg_constant_tl(0);
        return true;
    }

    switch (a->csr) {
    case LOONGARCH_CSR_PGD:
        gen_helper_csrrd_pgd(dest, cpu_env);
        break;
    case LOONGARCH_CSR_TVAL:
        gen_helper_csrrd_tval(dest, cpu_env);
        break;
    default:
        tcg_gen_ld_tl(dest, cpu_env, csr_offset);
    }
    return true;
}

static bool trans_csrwr(DisasContext *ctx, arg_csrwr *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rd, EXT_NONE);

    if (check_plv(ctx) || ro_csr(a->csr)) {
        return false;
    }

    unsigned csr_offset = cpu_csr_offset(a->csr);
    if (csr_offset == 0) {
        /* CSR is undefined: write ignored. */
        return true;
    }

    switch (a->csr) {
    case LOONGARCH_CSR_ESTAT:
        gen_helper_csrwr_estat(dest, cpu_env, src1);
        break;
    case LOONGARCH_CSR_ASID:
        gen_helper_csrwr_asid(dest, cpu_env, src1);
        gen_disas_exit(ctx);
        break;
    case LOONGARCH_CSR_TCFG:
        gen_helper_csrwr_tcfg(dest, cpu_env, src1);
        break;
    case LOONGARCH_CSR_TICLR:
        gen_helper_csrwr_ticlr(dest, cpu_env, src1);
        break;
    default:
        {
            TCGv temp = tcg_temp_new();
            tcg_gen_ld_tl(temp, cpu_env, csr_offset);
            tcg_gen_st_tl(src1, cpu_env, csr_offset);
            tcg_gen_mov_tl(dest, temp);
            tcg_temp_free(temp);

            /* Cpu state may be changed, need exit */
            if ((a->csr == LOONGARCH_CSR_CRMD) ||
                (a->csr == LOONGARCH_CSR_EUEN)) {
                gen_disas_exit(ctx);
            }
        }
    }
    return true;
}

static bool trans_csrxchg(DisasContext *ctx, arg_csrxchg *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv mask = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx) || ro_csr(a->csr)) {
        return false;
    }

    unsigned csr_offset = cpu_csr_offset(a->csr);
    if (csr_offset == 0) {
        /* CSR is undefined: write ignored */
        return true;
    }

    TCGv old_val = tcg_temp_new();
    TCGv new_val = tcg_temp_new();
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    tcg_gen_ld_tl(old_val, cpu_env, csr_offset);
    tcg_gen_and_tl(t0, src1, mask);
    tcg_gen_not_tl(t1, mask);
    tcg_gen_and_tl(t1, old_val, t1);
    tcg_gen_or_tl(new_val, t1, t0);

    switch (a->csr) {
    case LOONGARCH_CSR_ESTAT:
        gen_helper_csrwr_estat(dest, cpu_env, new_val);
        break;
    case LOONGARCH_CSR_ASID:
        gen_helper_csrwr_asid(dest, cpu_env, new_val);
        break;
    case LOONGARCH_CSR_TCFG:
        gen_helper_csrwr_tcfg(dest, cpu_env, new_val);
        break;
    case LOONGARCH_CSR_TICLR:
        gen_helper_csrwr_ticlr(dest, cpu_env, new_val);
        break;
    default:
        tcg_gen_mov_tl(dest, old_val);
    }

    gen_helper_csr_update(cpu_env, new_val, tcg_constant_tl(csr_offset));

    if ((a->csr == LOONGARCH_CSR_ASID) || (a->csr == LOONGARCH_CSR_CRMD) ||
        (a->csr == LOONGARCH_CSR_EUEN)) {
        gen_disas_exit(ctx);
    }

    tcg_temp_free(t1);
    tcg_temp_free(t0);
    tcg_temp_free(new_val);
    tcg_temp_free(old_val);

    return true;
}

static bool trans_iocsrrd_b(DisasContext *ctx, arg_iocsrrd_b *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_iocsr_read(dest, cpu_env, src1, tcg_constant_i32(1));
    return true;
}

static bool trans_iocsrrd_h(DisasContext *ctx, arg_iocsrrd_h *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_iocsr_read(dest, cpu_env, src1, tcg_constant_i32(2));
    return true;
}

static bool trans_iocsrrd_w(DisasContext *ctx, arg_iocsrrd_w *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_iocsr_read(dest, cpu_env, src1, tcg_constant_i32(4));
    return true;
}

static bool trans_iocsrrd_d(DisasContext *ctx, arg_iocsrrd_d *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_iocsr_read(dest, cpu_env, src1, tcg_constant_i32(8));
    return true;
}

static bool trans_iocsrwr_b(DisasContext *ctx, arg_iocsrwr_b *a)
{
    TCGv val = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_iocsr_write(cpu_env, addr, val, tcg_constant_i32(1));
    return true;
}

static bool trans_iocsrwr_h(DisasContext *ctx, arg_iocsrwr_h *a)
{
    TCGv val = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_iocsr_write(cpu_env, addr, val, tcg_constant_i32(2));
    return true;
}

static bool trans_iocsrwr_w(DisasContext *ctx, arg_iocsrwr_w *a)
{
    TCGv val = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_iocsr_write(cpu_env, addr, val, tcg_constant_i32(4));
    return true;
}

static bool trans_iocsrwr_d(DisasContext *ctx, arg_iocsrwr_d *a)
{
    TCGv val = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_iocsr_write(cpu_env, addr, val, tcg_constant_i32(8));
    return true;
}
