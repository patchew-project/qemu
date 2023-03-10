/*
 * RISC-V translation routines for the Zvknh Extension.
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

#define GEN_VV_UNMASKED_TRANS(NAME, CHECK, VL_MULTIPLE)                \
static bool trans_##NAME(DisasContext *s, arg_rmrr * a)                \
{                                                                      \
    if (CHECK(s, a)) {                                                 \
        uint32_t data = 0;                                             \
        TCGLabel *over = gen_new_label();                              \
        TCGLabel *vl_ok = gen_new_label();                             \
        TCGv_i32 tmp = tcg_temp_new_i32();                             \
                                                                       \
        /* save opcode for unwinding in case we throw an exception */  \
        decode_save_opc(s);                                            \
                                                                       \
        /* check (vl % VL_MULTIPLE == 0) assuming it's power of 2 */   \
        tcg_gen_trunc_tl_i32(tmp, cpu_vl);                             \
        tcg_gen_andi_i32(tmp, tmp, VL_MULTIPLE - 1);                   \
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, vl_ok);               \
        gen_helper_restore_cpu_and_raise_exception(cpu_env,            \
            tcg_constant_i32(RISCV_EXCP_ILLEGAL_INST));                \
        gen_set_label(vl_ok);                                          \
                                                                       \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);     \
                                                                       \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                     \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);                 \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);                   \
        data = FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s); \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);                   \
                                                                       \
        tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd),                         \
                           vreg_ofs(s, a->rs1),                        \
                           vreg_ofs(s, a->rs2), cpu_env,               \
                           s->cfg_ptr->vlen / 8,                       \
                           s->cfg_ptr->vlen / 8, data,                 \
                           gen_helper_##NAME);                         \
                                                                       \
        mark_vs_dirty(s);                                              \
        gen_set_label(over);                                           \
        return true;                                                   \
    }                                                                  \
    return false;                                                      \
}

static bool vsha_check_sew(DisasContext *s)
{
    return (s->cfg_ptr->ext_zvknha == true && s->sew == MO_32) ||
           (s->cfg_ptr->ext_zvknhb == true &&
               (s->sew == MO_32 || s->sew == MO_64));
}

static bool vsha_check(DisasContext *s, arg_rmrr *a)
{
    int egw_bytes = 4 << s->sew;
    int mult = 1 << MAX(s->lmul, 0);
    return opivv_check(s, a) &&
           vsha_check_sew(s) &&
           MAXSZ(s) >= egw_bytes &&
           !is_overlapped(a->rd, mult, a->rs1, mult) &&
           !is_overlapped(a->rd, mult, a->rs2, mult) &&
           s->vstart % 4 == 0 &&
           s->lmul >= 0;
}

GEN_VV_UNMASKED_TRANS(vsha2ms_vv, vsha_check, 4)
GEN_VV_UNMASKED_TRANS(vsha2cl_vv, vsha_check, 4)
GEN_VV_UNMASKED_TRANS(vsha2ch_vv, vsha_check, 4)
