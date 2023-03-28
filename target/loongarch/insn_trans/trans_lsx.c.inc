/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LSX translate functions
 * Copyright (c) 2022-2023 Loongson Technology Corporation Limited
 */

#ifndef CONFIG_USER_ONLY
#define CHECK_SXE do { \
    if ((ctx->base.tb->flags & HW_FLAGS_EUEN_SXE) == 0) { \
        generate_exception(ctx, EXCCODE_SXD); \
        return true; \
    } \
} while (0)
#else
#define CHECK_SXE
#endif

static bool gen_vvv(DisasContext *ctx, arg_vvv *a,
                    void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);

    CHECK_SXE;

    func(cpu_env, vd, vj, vk);
    return true;
}

static bool gvec_vvv(DisasContext *ctx, arg_vvv *a, MemOp mop,
                     void (*func)(unsigned, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t))
{
    uint32_t vd_ofs, vj_ofs, vk_ofs;

    CHECK_SXE;

    vd_ofs = vreg_full_offset(a->vd);
    vj_ofs = vreg_full_offset(a->vj);
    vk_ofs = vreg_full_offset(a->vk);

    func(mop, vd_ofs, vj_ofs, vk_ofs, 16, 16);
    return true;
}

static bool gvec_vv(DisasContext *ctx, arg_vv *a, MemOp mop,
                    void (*func)(unsigned, uint32_t, uint32_t,
                                 uint32_t, uint32_t))
{
    uint32_t dofs, jofs;

    CHECK_SXE;

    dofs = vreg_full_offset(a->vd);
    jofs = vreg_full_offset(a->vj);

    func(mop, dofs, jofs, 16, 16);
    return true;
}

static bool gvec_vv_i(DisasContext *ctx, arg_vv_i *a, MemOp mop,
                      void (*func)(unsigned, uint32_t, uint32_t,
                                   int64_t, uint32_t, uint32_t))
{
    uint32_t vd_ofs, vj_ofs;

    CHECK_SXE;

    vd_ofs = vreg_full_offset(a->vd);
    vj_ofs = vreg_full_offset(a->vj);

    func(mop, vd_ofs, vj_ofs, a->imm , 16, 16);
    return true;
}

static bool gvec_subi(DisasContext *ctx, arg_vv_i *a, MemOp mop)
{
    uint32_t vd_ofs, vj_ofs;

    CHECK_SXE;

    vd_ofs = vreg_full_offset(a->vd);
    vj_ofs = vreg_full_offset(a->vj);

    tcg_gen_gvec_addi(mop, vd_ofs, vj_ofs, -(a->imm), 16, 16);
    return true;
}

TRANS(vadd_b, gvec_vvv, MO_8, tcg_gen_gvec_add)
TRANS(vadd_h, gvec_vvv, MO_16, tcg_gen_gvec_add)
TRANS(vadd_w, gvec_vvv, MO_32, tcg_gen_gvec_add)
TRANS(vadd_d, gvec_vvv, MO_64, tcg_gen_gvec_add)
TRANS(vadd_q, gen_vvv, gen_helper_vadd_q)
TRANS(vsub_b, gvec_vvv, MO_8, tcg_gen_gvec_sub)
TRANS(vsub_h, gvec_vvv, MO_16, tcg_gen_gvec_sub)
TRANS(vsub_w, gvec_vvv, MO_32, tcg_gen_gvec_sub)
TRANS(vsub_d, gvec_vvv, MO_64, tcg_gen_gvec_sub)
TRANS(vsub_q, gen_vvv, gen_helper_vsub_q)

TRANS(vaddi_bu, gvec_vv_i, MO_8, tcg_gen_gvec_addi)
TRANS(vaddi_hu, gvec_vv_i, MO_16, tcg_gen_gvec_addi)
TRANS(vaddi_wu, gvec_vv_i, MO_32, tcg_gen_gvec_addi)
TRANS(vaddi_du, gvec_vv_i, MO_64, tcg_gen_gvec_addi)
TRANS(vsubi_bu, gvec_subi, MO_8)
TRANS(vsubi_hu, gvec_subi, MO_16)
TRANS(vsubi_wu, gvec_subi, MO_32)
TRANS(vsubi_du, gvec_subi, MO_64)

TRANS(vneg_b, gvec_vv, MO_8, tcg_gen_gvec_neg)
TRANS(vneg_h, gvec_vv, MO_16, tcg_gen_gvec_neg)
TRANS(vneg_w, gvec_vv, MO_32, tcg_gen_gvec_neg)
TRANS(vneg_d, gvec_vv, MO_64, tcg_gen_gvec_neg)

TRANS(vsadd_b, gvec_vvv, MO_8, tcg_gen_gvec_ssadd)
TRANS(vsadd_h, gvec_vvv, MO_16, tcg_gen_gvec_ssadd)
TRANS(vsadd_w, gvec_vvv, MO_32, tcg_gen_gvec_ssadd)
TRANS(vsadd_d, gvec_vvv, MO_64, tcg_gen_gvec_ssadd)
TRANS(vsadd_bu, gvec_vvv, MO_8, tcg_gen_gvec_usadd)
TRANS(vsadd_hu, gvec_vvv, MO_16, tcg_gen_gvec_usadd)
TRANS(vsadd_wu, gvec_vvv, MO_32, tcg_gen_gvec_usadd)
TRANS(vsadd_du, gvec_vvv, MO_64, tcg_gen_gvec_usadd)
TRANS(vssub_b, gvec_vvv, MO_8, tcg_gen_gvec_sssub)
TRANS(vssub_h, gvec_vvv, MO_16, tcg_gen_gvec_sssub)
TRANS(vssub_w, gvec_vvv, MO_32, tcg_gen_gvec_sssub)
TRANS(vssub_d, gvec_vvv, MO_64, tcg_gen_gvec_sssub)
TRANS(vssub_bu, gvec_vvv, MO_8, tcg_gen_gvec_ussub)
TRANS(vssub_hu, gvec_vvv, MO_16, tcg_gen_gvec_ussub)
TRANS(vssub_wu, gvec_vvv, MO_32, tcg_gen_gvec_ussub)
TRANS(vssub_du, gvec_vvv, MO_64, tcg_gen_gvec_ussub)

TRANS(vhaddw_h_b, gen_vvv, gen_helper_vhaddw_h_b)
TRANS(vhaddw_w_h, gen_vvv, gen_helper_vhaddw_w_h)
TRANS(vhaddw_d_w, gen_vvv, gen_helper_vhaddw_d_w)
TRANS(vhaddw_q_d, gen_vvv, gen_helper_vhaddw_q_d)
TRANS(vhaddw_hu_bu, gen_vvv, gen_helper_vhaddw_hu_bu)
TRANS(vhaddw_wu_hu, gen_vvv, gen_helper_vhaddw_wu_hu)
TRANS(vhaddw_du_wu, gen_vvv, gen_helper_vhaddw_du_wu)
TRANS(vhaddw_qu_du, gen_vvv, gen_helper_vhaddw_qu_du)
TRANS(vhsubw_h_b, gen_vvv, gen_helper_vhsubw_h_b)
TRANS(vhsubw_w_h, gen_vvv, gen_helper_vhsubw_w_h)
TRANS(vhsubw_d_w, gen_vvv, gen_helper_vhsubw_d_w)
TRANS(vhsubw_q_d, gen_vvv, gen_helper_vhsubw_q_d)
TRANS(vhsubw_hu_bu, gen_vvv, gen_helper_vhsubw_hu_bu)
TRANS(vhsubw_wu_hu, gen_vvv, gen_helper_vhsubw_wu_hu)
TRANS(vhsubw_du_wu, gen_vvv, gen_helper_vhsubw_du_wu)
TRANS(vhsubw_qu_du, gen_vvv, gen_helper_vhsubw_qu_du)
