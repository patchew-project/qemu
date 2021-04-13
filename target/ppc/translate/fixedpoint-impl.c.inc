static bool trans_paddi(DisasContext *ctx, arg_paddi *a)
{
    if (a->r == 0) {
        if (a->ra == 0) {
            /* li case */
            tcg_gen_movi_tl(cpu_gpr[a->rt], a->si);
        } else {
            tcg_gen_addi_tl(cpu_gpr[a->rt],
                            cpu_gpr[a->ra], a->si);
        }
    } else {
        if (a->ra == 0) {
            tcg_gen_addi_tl(cpu_gpr[a->rt], cpu_nip, a->si);
        } else {
            /* invalid form */
            gen_invalid(ctx);
        }
    }

    return true;
}

static bool trans_addi(DisasContext *ctx, arg_addi *a)
{
    return trans_paddi(ctx, a);
}
