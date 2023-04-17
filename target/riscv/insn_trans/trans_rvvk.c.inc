/*
 * RISC-V translation routines for the vector crypto extension.
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Written by Codethink Ltd and SiFive.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Zvbc
 */

#define GEN_VV_MASKED_TRANS(NAME, CHECK)                     \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)   \
    {                                                        \
        if (CHECK(s, a)) {                                   \
            return opivv_trans(a->rd, a->rs1, a->rs2, a->vm, \
                               gen_helper_##NAME, s);        \
        }                                                    \
        return false;                                        \
    }

static bool vclmul_vv_check(DisasContext *s, arg_rmrr *a)
{
    return opivv_check(s, a) &&
           s->cfg_ptr->ext_zvbc == true &&
           s->sew == MO_64;
}

GEN_VV_MASKED_TRANS(vclmul_vv, vclmul_vv_check)
GEN_VV_MASKED_TRANS(vclmulh_vv, vclmul_vv_check)

#define GEN_VX_MASKED_TRANS(NAME, CHECK)                                      \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                    \
    {                                                                         \
        if (CHECK(s, a)) {                                                    \
            TCGv_ptr rd_v, v0_v, rs2_v;                                       \
            TCGv rs1;                                                         \
            TCGv_i32 desc;                                                    \
            uint32_t data = 0;                                                \
                                                                              \
            TCGLabel *over = gen_new_label();                                 \
            tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);        \
                                                                              \
            data = FIELD_DP32(data, VDATA, VM, a->vm);                        \
            data = FIELD_DP32(data, VDATA, LMUL, s->lmul);                    \
            data = FIELD_DP32(data, VDATA, VTA, s->vta);                      \
            data = FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);    \
            data = FIELD_DP32(data, VDATA, VMA, s->vma);                      \
                                                                              \
            rd_v = tcg_temp_new_ptr();                                        \
            v0_v = tcg_temp_new_ptr();                                        \
            rs1 = get_gpr(s, a->rs1, EXT_ZERO);                               \
            rs2_v = tcg_temp_new_ptr();                                       \
            desc = tcg_constant_i32(                                          \
                simd_desc(s->cfg_ptr->vlen / 8, s->cfg_ptr->vlen / 8, data)); \
            tcg_gen_addi_ptr(rd_v, cpu_env, vreg_ofs(s, a->rd));              \
            tcg_gen_addi_ptr(v0_v, cpu_env, vreg_ofs(s, 0));                  \
            tcg_gen_addi_ptr(rs2_v, cpu_env, vreg_ofs(s, a->rs2));            \
            gen_helper_##NAME(rd_v, v0_v, rs1, rs2_v, cpu_env, desc);         \
                                                                              \
            mark_vs_dirty(s);                                                 \
            gen_set_label(over);                                              \
            return true;                                                      \
        }                                                                     \
        return false;                                                         \
    }

static bool vclmul_vx_check(DisasContext *s, arg_rmrr *a)
{
    return opivx_check(s, a) &&
           s->cfg_ptr->ext_zvbc == true &&
           s->sew == MO_64;
}

GEN_VX_MASKED_TRANS(vclmul_vx, vclmul_vx_check)
GEN_VX_MASKED_TRANS(vclmulh_vx, vclmul_vx_check)
