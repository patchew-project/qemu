/*
 * RISC-V translation routines for the Zvkned Extension.
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
#define GEN_V_UNMASKED_TRANS(NAME, CHECK)                                 \
static bool trans_##NAME(DisasContext *s, arg_##NAME * a)                 \
{                                                                         \
    if (CHECK(s, a)) {                                                    \
        TCGv_ptr rd_v, rs2_v;                                             \
        TCGv_i32 desc;                                                    \
        uint32_t data = 0;                                                \
        TCGLabel *over = gen_new_label();                                 \
        TCGLabel *vl_ok = gen_new_label();                                \
        TCGv_i32 tmp = tcg_temp_new_i32();                                \
                                                                          \
        /* save opcode for unwinding in case we throw an exception */     \
        decode_save_opc(s);                                               \
                                                                          \
        /* check (vl % 4 == 0) */                                         \
        tcg_gen_trunc_tl_i32(tmp, cpu_vl);                                \
        tcg_gen_andi_i32(tmp, tmp, 0b11);                                 \
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, vl_ok);                  \
        gen_helper_restore_cpu_and_raise_exception(cpu_env,               \
            tcg_constant_i32(RISCV_EXCP_ILLEGAL_INST));                   \
        gen_set_label(vl_ok);                                             \
                                                                          \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);        \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                        \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);                    \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);                      \
        data = FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);    \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);                      \
        rd_v = tcg_temp_new_ptr();                                        \
        rs2_v = tcg_temp_new_ptr();                                       \
        desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlen / 8,           \
                                          s->cfg_ptr->vlen / 8, data));   \
        tcg_gen_addi_ptr(rd_v, cpu_env, vreg_ofs(s, a->rd));              \
        tcg_gen_addi_ptr(rs2_v, cpu_env, vreg_ofs(s, a->rs2));            \
        gen_helper_##NAME(rd_v, rs2_v, cpu_env, desc);                    \
        mark_vs_dirty(s);                                                 \
        gen_set_label(over);                                              \
        return true;                                                      \
    }                                                                     \
    return false;                                                         \
}


static bool vaes_check_vv(DisasContext *s, arg_rmr *a)
{
    return s->cfg_ptr->ext_zvkned == true &&
           require_rvv(s) &&
           vext_check_isa_ill(s) &&
           MAXSZ(s) >= (128 / 8) && /* EGW in bytes */
           require_align(a->rd, s->lmul) &&
           require_align(a->rs2, s->lmul) &&
           s->vstart % 4 == 0 && s->sew == MO_32;
}
GEN_V_UNMASKED_TRANS(vaesef_vv, vaes_check_vv)
