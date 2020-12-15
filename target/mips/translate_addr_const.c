/*
 * Address Computation and Large Constant Instructions
 */
#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "translate.h"

bool gen_LSA(DisasContext *ctx, int rd, int rt, int rs, int sa)
{
    TCGv t0;
    TCGv t1;

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);
    tcg_gen_shli_tl(t0, t0, sa + 1);
    tcg_gen_add_tl(cpu_gpr[rd], t0, t1);
    tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);

    tcg_temp_free(t1);
    tcg_temp_free(t0);

    return true;
}

bool gen_DLSA(DisasContext *ctx, int rd, int rt, int rs, int sa)
{
    TCGv t0;
    TCGv t1;

    check_mips_64(ctx);

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);
    tcg_gen_shli_tl(t0, t0, sa + 1);
    tcg_gen_add_tl(cpu_gpr[rd], t0, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);

    return true;
}
