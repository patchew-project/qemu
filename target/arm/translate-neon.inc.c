/*
 *  ARM translation: AArch32 Neon instructions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Copyright (c) 2007 OpenedHand, Ltd.
 *  Copyright (c) 2020 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file is intended to be included from translate.c; it uses
 * some macros and definitions provided by that file.
 * It might be possible to convert it to a standalone .c file eventually.
 */

static inline int plus1(DisasContext *s, int x)
{
    return x + 1;
}

/* Include the generated Neon decoder */
#include "decode-neon-dp.inc.c"
#include "decode-neon-ls.inc.c"
#include "decode-neon-shared.inc.c"

static bool trans_VCMLA(DisasContext *s, arg_VCMLA *a)
{
    int opr_sz;
    TCGv_ptr fpst;
    gen_helper_gvec_3_ptr *fn_gvec_ptr;

    if (!dc_isar_feature(aa32_vcma, s)
        || (!a->size && !dc_isar_feature(aa32_fp16_arith, s))) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    fpst = get_fpstatus_ptr(1);
    fn_gvec_ptr = a->size ? gen_helper_gvec_fcmlas : gen_helper_gvec_fcmlah;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       fpst, opr_sz, opr_sz, a->rot,
                       fn_gvec_ptr);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCADD(DisasContext *s, arg_VCADD *a)
{
    int opr_sz;
    TCGv_ptr fpst;
    gen_helper_gvec_3_ptr *fn_gvec_ptr;

    if (!dc_isar_feature(aa32_vcma, s)
        || (!a->size && !dc_isar_feature(aa32_fp16_arith, s))) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    fpst = get_fpstatus_ptr(1);
    fn_gvec_ptr = a->size ? gen_helper_gvec_fcadds : gen_helper_gvec_fcaddh;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       fpst, opr_sz, opr_sz, a->rot,
                       fn_gvec_ptr);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VDOT(DisasContext *s, arg_VDOT *a)
{
    int opr_sz;
    gen_helper_gvec_3 *fn_gvec;

    if (!dc_isar_feature(aa32_dp, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    fn_gvec = a->u ? gen_helper_gvec_udot_b : gen_helper_gvec_sdot_b;
    tcg_gen_gvec_3_ool(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       opr_sz, opr_sz, 0, fn_gvec);
    return true;
}

static bool trans_VFML(DisasContext *s, arg_VFML *a)
{
    int opr_sz;

    if (!dc_isar_feature(aa32_fhm, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        (a->vd & 0x10)) {
        return false;
    }

    if (a->vd & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(a->q, a->vn),
                       vfp_reg_offset(a->q, a->vm),
                       cpu_env, opr_sz, opr_sz, a->s, /* is_2 == 0 */
                       gen_helper_gvec_fmlal_a32);
    return true;
}

static bool trans_VCMLA_scalar(DisasContext *s, arg_VCMLA_scalar *a)
{
    gen_helper_gvec_3_ptr *fn_gvec_ptr;
    int opr_sz;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_vcma, s)) {
        return false;
    }
    if (a->size == 0 && !dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vd | a->vn) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fn_gvec_ptr = (a->size ? gen_helper_gvec_fcmlas_idx
                   : gen_helper_gvec_fcmlah_idx);
    opr_sz = (1 + a->q) * 8;
    fpst = get_fpstatus_ptr(1);
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       fpst, opr_sz, opr_sz,
                       (a->index << 2) | a->rot, fn_gvec_ptr);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VDOT_scalar(DisasContext *s, arg_VDOT_scalar *a)
{
    gen_helper_gvec_3 *fn_gvec;
    int opr_sz;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_dp, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn) & 0x10)) {
        return false;
    }

    if ((a->vd | a->vn) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fn_gvec = a->u ? gen_helper_gvec_udot_idx_b : gen_helper_gvec_sdot_idx_b;
    opr_sz = (1 + a->q) * 8;
    fpst = get_fpstatus_ptr(1);
    tcg_gen_gvec_3_ool(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->rm),
                       opr_sz, opr_sz, a->index, fn_gvec);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VFML_scalar(DisasContext *s, arg_VFML_scalar *a)
{
    int opr_sz;

    if (!dc_isar_feature(aa32_fhm, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd & 0x10) || (a->q && (a->vn & 0x10)))) {
        return false;
    }

    if (a->vd & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(a->q, a->vn),
                       vfp_reg_offset(a->q, a->rm),
                       cpu_env, opr_sz, opr_sz,
                       (a->index << 2) | a->s, /* is_2 == 0 */
                       gen_helper_gvec_fmlal_idx_a32);
    return true;
}

static struct {
    int nregs;
    int interleave;
    int spacing;
} const neon_ls_element_type[11] = {
    {1, 4, 1},
    {1, 4, 2},
    {4, 1, 1},
    {2, 2, 2},
    {1, 3, 1},
    {1, 3, 2},
    {3, 1, 1},
    {1, 1, 1},
    {1, 2, 1},
    {1, 2, 2},
    {2, 1, 1}
};

static void gen_neon_ldst_base_update(DisasContext *s, int rm, int rn,
                                      int stride)
{
    if (rm != 15) {
        TCGv_i32 base;

        base = load_reg(s, rn);
        if (rm == 13) {
            tcg_gen_addi_i32(base, base, stride);
        } else {
            TCGv_i32 index;
            index = load_reg(s, rm);
            tcg_gen_add_i32(base, base, index);
            tcg_temp_free_i32(index);
        }
        store_reg(s, rn, base);
    }
}

static bool trans_VLDST_multiple(DisasContext *s, arg_VLDST_multiple *a)
{
    /* Neon load/store multiple structures */
    int nregs, interleave, spacing, reg, n;
    MemOp endian = s->be_data;
    int mmu_idx = get_mem_index(s);
    int size = a->size;
    TCGv_i64 tmp64;
    TCGv_i32 addr, tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }
    if (a->itype > 10) {
        return false;
    }
    /* Catch UNDEF cases for bad values of align field */
    switch (a->itype & 0xc) {
    case 4:
        if (a->align >= 2) {
            return false;
        }
        break;
    case 8:
        if (a->align == 3) {
            return false;
        }
        break;
    default:
        break;
    }
    nregs = neon_ls_element_type[a->itype].nregs;
    interleave = neon_ls_element_type[a->itype].interleave;
    spacing = neon_ls_element_type[a->itype].spacing;
    if (size == 3 && (interleave | spacing) != 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /* For our purposes, bytes are always little-endian.  */
    if (size == 0) {
        endian = MO_LE;
    }
    /*
     * Consecutive little-endian elements from a single register
     * can be promoted to a larger little-endian operation.
     */
    if (interleave == 1 && endian == MO_LE) {
        size = 3;
    }
    tmp64 = tcg_temp_new_i64();
    addr = tcg_temp_new_i32();
    tmp = tcg_const_i32(1 << size);
    load_reg_var(s, addr, a->rn);
    for (reg = 0; reg < nregs; reg++) {
        for (n = 0; n < 8 >> size; n++) {
            int xs;
            for (xs = 0; xs < interleave; xs++) {
                int tt = a->vd + reg + spacing * xs;

                if (a->l) {
                    gen_aa32_ld_i64(s, tmp64, addr, mmu_idx, endian | size);
                    neon_store_element64(tt, n, size, tmp64);
                } else {
                    neon_load_element64(tmp64, tt, n, size);
                    gen_aa32_st_i64(s, tmp64, addr, mmu_idx, endian | size);
                }
                tcg_gen_add_i32(addr, addr, tmp);
            }
        }
    }
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i64(tmp64);

    gen_neon_ldst_base_update(s, a->rm, a->rn, nregs * interleave * 8);
    return true;
}

static bool trans_VLD_all_lanes(DisasContext *s, arg_VLD_all_lanes *a)
{
    /* Neon load single structure to all lanes */
    int reg, stride, vec_size;
    int vd = a->vd;
    int size = a->size;
    int nregs = a->n + 1;
    TCGv_i32 addr, tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    if (size == 3) {
        if (nregs != 4 || a->a == 0) {
            return false;
        }
        /* For VLD4 size == 3 a == 1 means 32 bits at 16 byte alignment */
        size = 2;
    }
    if (nregs == 1 && a->a == 1 && size == 0) {
        return false;
    }
    if (nregs == 3 && a->a == 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /*
     * VLD1 to all lanes: T bit indicates how many Dregs to write.
     * VLD2/3/4 to all lanes: T bit indicates register stride.
     */
    stride = a->t ? 2 : 1;
    vec_size = nregs == 1 ? stride * 8 : 8;

    tmp = tcg_temp_new_i32();
    addr = tcg_temp_new_i32();
    load_reg_var(s, addr, a->rn);
    for (reg = 0; reg < nregs; reg++) {
        gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s),
                        s->be_data | size);
        if ((vd & 1) && vec_size == 16) {
            /*
             * We cannot write 16 bytes at once because the
             * destination is unaligned.
             */
            tcg_gen_gvec_dup_i32(size, neon_reg_offset(vd, 0),
                                 8, 8, tmp);
            tcg_gen_gvec_mov(0, neon_reg_offset(vd + 1, 0),
                             neon_reg_offset(vd, 0), 8, 8);
        } else {
            tcg_gen_gvec_dup_i32(size, neon_reg_offset(vd, 0),
                                 vec_size, vec_size, tmp);
        }
        tcg_gen_addi_i32(addr, addr, 1 << size);
        vd += stride;
    }
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(addr);

    gen_neon_ldst_base_update(s, a->rm, a->rn, (1 << size) * nregs);

    return true;
}

static bool trans_VLDST_single(DisasContext *s, arg_VLDST_single *a)
{
    /* Neon load/store single structure to one lane */
    int reg;
    int nregs = a->n + 1;
    int vd = a->vd;
    TCGv_i32 addr, tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    /* Catch the UNDEF cases. This is unavoidably a bit messy. */
    switch (nregs) {
    case 1:
        if (((a->align & (1 << a->size)) != 0) ||
            (a->size == 2 && ((a->align & 3) == 1 || (a->align & 3) == 2))) {
            return false;
        }
        break;
    case 3:
        if ((a->align & 1) != 0) {
            return false;
        }
        /* fall through */
    case 2:
        if (a->size == 2 && (a->align & 2) != 0) {
            return false;
        }
        break;
    case 4:
        if ((a->size == 2) && ((a->align & 3) == 3)) {
            return false;
        }
        break;
    default:
        abort();
    }
    if ((vd + a->stride * (nregs - 1)) > 31) {
        /*
         * Attempts to write off the end of the register file are
         * UNPREDICTABLE; we choose to UNDEF because otherwise we would
         * access off the end of the array that holds the register data.
         */
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    addr = tcg_temp_new_i32();
    load_reg_var(s, addr, a->rn);
    /*
     * TODO: if we implemented alignment exceptions, we should check
     * addr against the alignment encoded in a->align here.
     */
    for (reg = 0; reg < nregs; reg++) {
        if (a->l) {
            gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s),
                            s->be_data | a->size);
            neon_store_element(vd, a->reg_idx, a->size, tmp);
        } else { /* Store */
            neon_load_element(tmp, vd, a->reg_idx, a->size);
            gen_aa32_st_i32(s, tmp, addr, get_mem_index(s),
                            s->be_data | a->size);
        }
        vd += a->stride;
        tcg_gen_addi_i32(addr, addr, 1 << a->size);
    }
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(tmp);

    gen_neon_ldst_base_update(s, a->rm, a->rn, (1 << a->size) * nregs);

    return true;
}

static bool do_3same(DisasContext *s, arg_3same *a, GVecGen3Fn fn)
{
    int vec_size = a->q ? 16 : 8;
    int rd_ofs = neon_reg_offset(a->vd, 0);
    int rn_ofs = neon_reg_offset(a->vn, 0);
    int rm_ofs = neon_reg_offset(a->vm, 0);

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fn(a->size, rd_ofs, rn_ofs, rm_ofs, vec_size, vec_size);
    return true;
}

#define DO_3SAME(INSN, FUNC)                                            \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        return do_3same(s, a, FUNC);                                    \
    }

DO_3SAME(VADD, tcg_gen_gvec_add)
DO_3SAME(VSUB, tcg_gen_gvec_sub)
DO_3SAME(VAND, tcg_gen_gvec_and)
DO_3SAME(VBIC, tcg_gen_gvec_andc)
DO_3SAME(VORR, tcg_gen_gvec_or)
DO_3SAME(VORN, tcg_gen_gvec_orc)
DO_3SAME(VEOR, tcg_gen_gvec_xor)

/* These insns are all gvec_bitsel but with the inputs in various orders. */
#define DO_3SAME_BITSEL(INSN, O1, O2, O3)                               \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        tcg_gen_gvec_bitsel(vece, rd_ofs, O1, O2, O3, oprsz, maxsz);    \
    }                                                                   \
    DO_3SAME(INSN, gen_##INSN##_3s)

DO_3SAME_BITSEL(VBSL, rd_ofs, rn_ofs, rm_ofs)
DO_3SAME_BITSEL(VBIT, rm_ofs, rn_ofs, rd_ofs)
DO_3SAME_BITSEL(VBIF, rm_ofs, rd_ofs, rn_ofs)

#define DO_3SAME_NO_SZ_3(INSN, FUNC)                                    \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        if (a->size == 3) {                                             \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, FUNC);                                    \
    }

DO_3SAME_NO_SZ_3(VMAX_S, tcg_gen_gvec_smax)
DO_3SAME_NO_SZ_3(VMAX_U, tcg_gen_gvec_umax)
DO_3SAME_NO_SZ_3(VMIN_S, tcg_gen_gvec_smin)
DO_3SAME_NO_SZ_3(VMIN_U, tcg_gen_gvec_umin)
DO_3SAME_NO_SZ_3(VMUL, tcg_gen_gvec_mul)

#define DO_3SAME_CMP(INSN, COND)                                        \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        tcg_gen_gvec_cmp(COND, vece, rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz); \
    }                                                                   \
    DO_3SAME_NO_SZ_3(INSN, gen_##INSN##_3s)

DO_3SAME_CMP(VCGT_S, TCG_COND_GT)
DO_3SAME_CMP(VCGT_U, TCG_COND_GTU)
DO_3SAME_CMP(VCGE_S, TCG_COND_GE)
DO_3SAME_CMP(VCGE_U, TCG_COND_GEU)
DO_3SAME_CMP(VCEQ, TCG_COND_EQ)

static void gen_VTST_3s(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                         uint32_t rm_ofs, uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, &cmtst_op[vece]);
}
DO_3SAME_NO_SZ_3(VTST, gen_VTST_3s)

#define DO_3SAME_GVEC4(INSN, OPARRAY)                                   \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),           \
                       rn_ofs, rm_ofs, oprsz, maxsz, &OPARRAY[vece]);   \
    }                                                                   \
    DO_3SAME(INSN, gen_##INSN##_3s)

DO_3SAME_GVEC4(VQADD_S, sqadd_op)
DO_3SAME_GVEC4(VQADD_U, uqadd_op)
DO_3SAME_GVEC4(VQSUB_S, sqsub_op)
DO_3SAME_GVEC4(VQSUB_U, uqsub_op)

static void gen_VMUL_p_3s(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                           uint32_t rm_ofs, uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz,
                       0, gen_helper_gvec_pmul_b);
}

static bool trans_VMUL_p_3s(DisasContext *s, arg_3same *a)
{
    if (a->size != 0) {
        return false;
    }
    return do_3same(s, a, gen_VMUL_p_3s);
}

#define DO_3SAME_GVEC3_NO_SZ_3(INSN, OPARRAY)                           \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs,                          \
                       oprsz, maxsz, &OPARRAY[vece]);                   \
    }                                                                   \
    DO_3SAME_NO_SZ_3(INSN, gen_##INSN##_3s)


DO_3SAME_GVEC3_NO_SZ_3(VMLA, mla_op)
DO_3SAME_GVEC3_NO_SZ_3(VMLS, mls_op)

#define DO_3SAME_GVEC3_SHIFT(INSN, OPARRAY)                             \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        /* Note the operation is vshl vd,vm,vn */                       \
        tcg_gen_gvec_3(rd_ofs, rm_ofs, rn_ofs,                          \
                       oprsz, maxsz, &OPARRAY[vece]);                   \
    }                                                                   \
    DO_3SAME(INSN, gen_##INSN##_3s)

DO_3SAME_GVEC3_SHIFT(VSHL_S, sshl_op)
DO_3SAME_GVEC3_SHIFT(VSHL_U, ushl_op)

static bool do_vqrdmlah(DisasContext *s, arg_3same *a,
                        gen_helper_gvec_3_ptr *fn)
{
    int vec_size = a->q ? 16 : 8;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !dc_isar_feature(aa32_rdm, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!fn) {
        return false; /* bad size */
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       cpu_env, vec_size, vec_size, 0, fn);
    return true;
}

static bool trans_VQRDMLAH_3s(DisasContext *s, arg_3same *a)
{
    static gen_helper_gvec_3_ptr * const fns[] = {
        NULL,
        gen_helper_gvec_qrdmlah_s16,
        gen_helper_gvec_qrdmlah_s32,
        NULL,
    };
    return do_vqrdmlah(s, a, fns[a->size]);
}

static bool trans_VQRDMLSH_3s(DisasContext *s, arg_3same *a)
{
    static gen_helper_gvec_3_ptr * const fns[] = {
        NULL,
        gen_helper_gvec_qrdmlsh_s16,
        gen_helper_gvec_qrdmlsh_s32,
        NULL,
    };
    return do_vqrdmlah(s, a, fns[a->size]);
}

static bool trans_SHA1_3s(DisasContext *s, arg_SHA1_3s *a)
{
    TCGv_ptr ptr1, ptr2, ptr3;
    TCGv_i32 tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !dc_isar_feature(aa32_sha1, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    ptr1 = vfp_reg_ptr(true, a->vd);
    ptr2 = vfp_reg_ptr(true, a->vn);
    ptr3 = vfp_reg_ptr(true, a->vm);
    tmp = tcg_const_i32(a->optype);
    gen_helper_crypto_sha1_3reg(ptr1, ptr2, ptr3, tmp);
    tcg_temp_free_i32(tmp);
    tcg_temp_free_ptr(ptr1);
    tcg_temp_free_ptr(ptr2);
    tcg_temp_free_ptr(ptr3);

    return true;
}

static bool trans_SHA256H_3s(DisasContext *s, arg_SHA256H_3s *a)
{
    TCGv_ptr ptr1, ptr2, ptr3;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !dc_isar_feature(aa32_sha2, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    ptr1 = vfp_reg_ptr(true, a->vd);
    ptr2 = vfp_reg_ptr(true, a->vn);
    ptr3 = vfp_reg_ptr(true, a->vm);
    gen_helper_crypto_sha256h(ptr1, ptr2, ptr3);
    tcg_temp_free_ptr(ptr1);
    tcg_temp_free_ptr(ptr2);
    tcg_temp_free_ptr(ptr3);

    return true;
}

static bool trans_SHA256H2_3s(DisasContext *s, arg_SHA256H2_3s *a)
{
    TCGv_ptr ptr1, ptr2, ptr3;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !dc_isar_feature(aa32_sha2, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    ptr1 = vfp_reg_ptr(true, a->vd);
    ptr2 = vfp_reg_ptr(true, a->vn);
    ptr3 = vfp_reg_ptr(true, a->vm);
    gen_helper_crypto_sha256h2(ptr1, ptr2, ptr3);
    tcg_temp_free_ptr(ptr1);
    tcg_temp_free_ptr(ptr2);
    tcg_temp_free_ptr(ptr3);

    return true;
}

static bool trans_SHA256SU1_3s(DisasContext *s, arg_SHA256SU1_3s *a)
{
    TCGv_ptr ptr1, ptr2, ptr3;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !dc_isar_feature(aa32_sha2, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    ptr1 = vfp_reg_ptr(true, a->vd);
    ptr2 = vfp_reg_ptr(true, a->vn);
    ptr3 = vfp_reg_ptr(true, a->vm);
    gen_helper_crypto_sha256su1(ptr1, ptr2, ptr3);
    tcg_temp_free_ptr(ptr1);
    tcg_temp_free_ptr(ptr2);
    tcg_temp_free_ptr(ptr3);

    return true;
}

static bool do_3same_64(DisasContext *s, arg_3same *a, NeonGenTwo64OpFn *fn)
{
    /* Handle 3-reg-same operations to be performed 64 bits at a time */
    TCGv_i64 rn, rm, rd;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    rn = tcg_temp_new_i64();
    rm = tcg_temp_new_i64();
    rd = tcg_temp_new_i64();

    for (pass = 0; pass < (a->q ? 2 : 1); pass++) {
        neon_load_reg64(rn, a->vn + pass);
        neon_load_reg64(rm, a->vm + pass);
        fn(rd, rm, rn);
        neon_store_reg64(rd, a->vd + pass);
    }

    tcg_temp_free_i64(rn);
    tcg_temp_free_i64(rm);
    tcg_temp_free_i64(rd);

    return true;
}

#define DO_3SAME_64(INSN, FUNC)                                         \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        return do_3same_64(s, a, FUNC);                                 \
    }

#define DO_3SAME_64_ENV(INSN, FUNC)                                     \
    static void gen_##INSN##_3s(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)     \
    {                                                                   \
        FUNC(d, cpu_env, n, m);                                         \
    }                                                                   \
    DO_3SAME_64(INSN, gen_##INSN##_3s)

DO_3SAME_64(VRSHL_S64, gen_helper_neon_rshl_s64)
DO_3SAME_64(VRSHL_U64, gen_helper_neon_rshl_u64)
DO_3SAME_64_ENV(VQSHL_S64, gen_helper_neon_qshl_s64)
DO_3SAME_64_ENV(VQSHL_U64, gen_helper_neon_qshl_u64)
DO_3SAME_64_ENV(VQRSHL_S64, gen_helper_neon_qrshl_s64)
DO_3SAME_64_ENV(VQRSHL_U64, gen_helper_neon_qrshl_u64)

static bool do_3same_32(DisasContext *s, arg_3same *a, NeonGenTwoOpFn *fn)
{
    /* Operations handled elementwise 32 bits at a time */
    TCGv_i32 tmp, tmp2;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        tmp = neon_load_reg(a->vn, pass);
        tmp2 = neon_load_reg(a->vm, pass);
        fn(tmp, tmp, tmp2);
        tcg_temp_free_i32(tmp2);
        neon_store_reg(a->vd, pass, tmp);
    }
    return true;
}

#define DO_3SAME_32(INSN, func)                                         \
    static bool trans_##INSN##_S_3s(DisasContext *s, arg_3same *a)      \
    {                                                                   \
        static NeonGenTwoOpFn * const fns[] = {                         \
            gen_helper_neon_##func##_s8,                                \
            gen_helper_neon_##func##_s16,                               \
            gen_helper_neon_##func##_s32,                               \
        };                                                              \
        if (a->size > 2) {                                              \
            return false;                                               \
        }                                                               \
        return do_3same_32(s, a, fns[a->size]);                         \
    }                                                                   \
    static bool trans_##INSN##_U_3s(DisasContext *s, arg_3same *a)      \
    {                                                                   \
        static NeonGenTwoOpFn * const fns[] = {                         \
            gen_helper_neon_##func##_u8,                                \
            gen_helper_neon_##func##_u16,                               \
            gen_helper_neon_##func##_u32,                               \
        };                                                              \
        if (a->size > 2) {                                              \
            return false;                                               \
        }                                                               \
        return do_3same_32(s, a, fns[a->size]);                         \
    }

DO_3SAME_32(VHADD, hadd)
DO_3SAME_32(VHSUB, hsub)
DO_3SAME_32(VRHADD, rhadd)
DO_3SAME_32(VABD, abd)

static bool do_3same_qs32(DisasContext *s, arg_3same *a, NeonGenTwoOpEnvFn *fn)
{
    /*
     * Saturating shift operations handled elementwise 32 bits at a
     * time which need to pass cpu_env to the helper and where the rn
     * and rm operands are reversed from the usual do_3same() order.
     */
    TCGv_i32 tmp, tmp2;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (a->size == 3) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        /* Note reversal of operand order */
        tmp = neon_load_reg(a->vm, pass);
        tmp2 = neon_load_reg(a->vn, pass);
        fn(tmp, cpu_env, tmp, tmp2);
        tcg_temp_free_i32(tmp2);
        neon_store_reg(a->vd, pass, tmp);
    }
    return true;
}

/*
 * Handling for shifts with sizes 8/16/32 bits. 64-bit shifts are
 * covered by the *_S64_3s and *_U64_3s patterns and the grouping in
 * the decode file means those functions are called first for
 * size==0b11. Note that we must 'return false' here for the
 * size==0b11 case rather than asserting, because where the 64-bit
 * function has an UNDEF case and returns false the decoder will fall
 * through to trying these functions.
 */
#define DO_3SAME_QS32(INSN, func)                                       \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        static NeonGenTwoOpEnvFn * const fns[] = {                      \
            gen_helper_neon_##func##8,                                  \
            gen_helper_neon_##func##16,                                 \
            gen_helper_neon_##func##32,                                 \
        };                                                              \
        if (a->size > 2) {                                              \
            return false;                                               \
        }                                                               \
        return do_3same_qs32(s, a, fns[a->size]);                       \
    }

DO_3SAME_QS32(VQSHL_S,qshl_s)
DO_3SAME_QS32(VQSHL_U,qshl_u)
DO_3SAME_QS32(VQRSHL_S,qrshl_s)
DO_3SAME_QS32(VQRSHL_U,qrshl_u)

#define DO_3SAME_SHIFT32(INSN, func) \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        static NeonGenTwoOpFn * const fns[] = {                         \
            gen_helper_neon_##func##8,                                  \
            gen_helper_neon_##func##16,                                 \
            gen_helper_neon_##func##32,                                 \
        };                                                              \
        int rtmp;                                                       \
        if (a->size > 2) {                                              \
            return false;                                               \
        }                                                               \
        /* Shift operand order is reversed */                           \
        rtmp = a->vn;                                                   \
        a->vn = a->vm;                                                  \
        a->vm = rtmp;                                                   \
        return do_3same_32(s, a, fns[a->size]);                         \
    }

DO_3SAME_SHIFT32(VRSHL_S, rshl_s)
DO_3SAME_SHIFT32(VRSHL_U, rshl_u)

static bool do_vaba(DisasContext *s, arg_3same *a,
                    NeonGenTwoOpFn *abd_fn, NeonGenTwoOpFn *add_fn)
{
    /* VABA: handled elementwise 32 bits at a time, accumulating */
    TCGv_i32 tmp, tmp2;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        tmp = neon_load_reg(a->vn, pass);
        tmp2 = neon_load_reg(a->vm, pass);
        abd_fn(tmp, tmp, tmp2);
        tcg_temp_free_i32(tmp2);
        tmp2 = neon_load_reg(a->vd, pass);
        add_fn(tmp, tmp, tmp2);
        tcg_temp_free_i32(tmp2);
        neon_store_reg(a->vd, pass, tmp);
    }
    return true;
}

static bool trans_VABA_S_3s(DisasContext *s, arg_3same *a)
{
    static NeonGenTwoOpFn * const abd_fns[] = {
        gen_helper_neon_abd_s8,
        gen_helper_neon_abd_s16,
        gen_helper_neon_abd_s32,
    };
    static NeonGenTwoOpFn * const add_fns[] = {
        gen_helper_neon_add_u8,
        gen_helper_neon_add_u16,
        tcg_gen_add_i32,
    };

    if (a->size > 2) {
        return false;
    }
    return do_vaba(s, a, abd_fns[a->size], add_fns[a->size]);
}

static bool trans_VABA_U_3s(DisasContext *s, arg_3same *a)
{
    static NeonGenTwoOpFn * const abd_fns[] = {
        gen_helper_neon_abd_u8,
        gen_helper_neon_abd_u16,
        gen_helper_neon_abd_u32,
    };
    static NeonGenTwoOpFn * const add_fns[] = {
        gen_helper_neon_add_u8,
        gen_helper_neon_add_u16,
        tcg_gen_add_i32,
    };

    if (a->size > 2) {
        return false;
    }
    return do_vaba(s, a, abd_fns[a->size], add_fns[a->size]);
}

static bool do_3same_pair(DisasContext *s, arg_3same *a, NeonGenTwoOpFn *fn)
{
    /* Operations handled pairwise 32 bits at a time */
    TCGv_i32 tmp, tmp2, tmp3;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (a->size == 3) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    assert(a->q == 0); /* enforced by decode patterns */

    /*
     * Note that we have to be careful not to clobber the source operands
     * in the "vm == vd" case by storing the result of the first pass too
     * early. Since Q is 0 there are always just two passes, so instead
     * of a complicated loop over each pass we just unroll.
     */
    tmp = neon_load_reg(a->vn, 0);
    tmp2 = neon_load_reg(a->vn, 1);
    fn(tmp, tmp, tmp2);
    tcg_temp_free_i32(tmp2);

    tmp3 = neon_load_reg(a->vm, 0);
    tmp2 = neon_load_reg(a->vm, 1);
    fn(tmp3, tmp3, tmp2);
    tcg_temp_free_i32(tmp2);

    neon_store_reg(a->vd, 0, tmp);
    neon_store_reg(a->vd, 1, tmp3);
    return true;
}

#define DO_3SAME_PAIR(INSN, func)                                       \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        static NeonGenTwoOpFn * const fns[] = {                         \
            gen_helper_neon_##func##8,                                  \
            gen_helper_neon_##func##16,                                 \
            gen_helper_neon_##func##32,                                 \
        };                                                              \
        if (a->size > 2) {                                              \
            return false;                                               \
        }                                                               \
        return do_3same_pair(s, a, fns[a->size]);                       \
    }

/* 32-bit pairwise ops end up the same as the elementwise versions.  */
#define gen_helper_neon_pmax_s32  tcg_gen_smax_i32
#define gen_helper_neon_pmax_u32  tcg_gen_umax_i32
#define gen_helper_neon_pmin_s32  tcg_gen_smin_i32
#define gen_helper_neon_pmin_u32  tcg_gen_umin_i32
#define gen_helper_neon_padd_u32  tcg_gen_add_i32

DO_3SAME_PAIR(VPMAX_S, pmax_s)
DO_3SAME_PAIR(VPMIN_S, pmin_s)
DO_3SAME_PAIR(VPMAX_U, pmax_u)
DO_3SAME_PAIR(VPMIN_U, pmin_u)
DO_3SAME_PAIR(VPADD, padd_u)

static void gen_VQDMULH_s16(TCGv_i32 rd, TCGv_i32 rn, TCGv_i32 rm)
{
    gen_helper_neon_qdmulh_s16(rd, cpu_env, rn, rm);
}

static void gen_VQDMULH_s32(TCGv_i32 rd, TCGv_i32 rn, TCGv_i32 rm)
{
    gen_helper_neon_qdmulh_s32(rd, cpu_env, rn, rm);
}

static bool trans_VQDMULH_3s(DisasContext *s, arg_3same *a)
{
    static NeonGenTwoOpFn * const fns[] = {
        gen_VQDMULH_s16, gen_VQDMULH_s32,
    };

    if (a->size != 1 && a->size != 2) {
        return false;
    }
    return do_3same_32(s, a, fns[a->size - 1]);
}

static void gen_VQRDMULH_s16(TCGv_i32 rd, TCGv_i32 rn, TCGv_i32 rm)
{
    gen_helper_neon_qrdmulh_s16(rd, cpu_env, rn, rm);
}

static void gen_VQRDMULH_s32(TCGv_i32 rd, TCGv_i32 rn, TCGv_i32 rm)
{
    gen_helper_neon_qrdmulh_s32(rd, cpu_env, rn, rm);
}

static bool trans_VQRDMULH_3s(DisasContext *s, arg_3same *a)
{
    static NeonGenTwoOpFn * const fns[] = {
        gen_VQRDMULH_s16, gen_VQRDMULH_s32,
    };

    if (a->size != 1 && a->size != 2) {
        return false;
    }
    return do_3same_32(s, a, fns[a->size - 1]);
}

static bool do_3same_fp(DisasContext *s, arg_3same *a, VFPGen3OpSPFn *fn,
                        bool reads_vd)
{
    /*
     * FP operations handled elementwise 32 bits at a time.
     * If reads_vd is true then the old value of Vd will be
     * loaded before calling the callback function. This is
     * used for multiply-accumulate type operations.
     */
    TCGv_i32 tmp, tmp2;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    TCGv_ptr fpstatus = get_fpstatus_ptr(1);
    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        tmp = neon_load_reg(a->vn, pass);
        tmp2 = neon_load_reg(a->vm, pass);
        if (reads_vd) {
            TCGv_i32 tmp_rd = neon_load_reg(a->vd, pass);
            fn(tmp_rd, tmp, tmp2, fpstatus);
            neon_store_reg(a->vd, pass, tmp_rd);
            tcg_temp_free_i32(tmp);
        } else {
            fn(tmp, tmp, tmp2, fpstatus);
            neon_store_reg(a->vd, pass, tmp);
        }
        tcg_temp_free_i32(tmp2);
    }
    tcg_temp_free_ptr(fpstatus);
    return true;
}

/*
 * For all the functions using this macro, size == 1 means fp16,
 * which is an architecture extension we don't implement yet.
 */
#define DO_3S_FP(INSN,FUNC,READS_VD)                                \
    static bool trans_##INSN##_fp_3s(DisasContext *s, arg_3same *a) \
    {                                                               \
        if (a->size != 0) {                                         \
            /* TODO fp16 support */                                 \
            return false;                                           \
        }                                                           \
        return do_3same_fp(s, a, FUNC, READS_VD);                   \
    }

DO_3S_FP(VADD, gen_helper_vfp_adds, false)
DO_3S_FP(VSUB, gen_helper_vfp_subs, false)
DO_3S_FP(VABD, gen_helper_neon_abd_f32, false)
DO_3S_FP(VMUL, gen_helper_vfp_muls, false)
DO_3S_FP(VCEQ, gen_helper_neon_ceq_f32, false)
DO_3S_FP(VCGE, gen_helper_neon_cge_f32, false)
DO_3S_FP(VCGT, gen_helper_neon_cgt_f32, false)
DO_3S_FP(VACGE, gen_helper_neon_acge_f32, false)
DO_3S_FP(VACGT, gen_helper_neon_acgt_f32, false)
DO_3S_FP(VMAX, gen_helper_vfp_maxs, false)
DO_3S_FP(VMIN, gen_helper_vfp_mins, false)

static void gen_VMLA_fp_3s(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm,
                            TCGv_ptr fpstatus)
{
    gen_helper_vfp_muls(vn, vn, vm, fpstatus);
    gen_helper_vfp_adds(vd, vd, vn, fpstatus);
}

static void gen_VMLS_fp_3s(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm,
                            TCGv_ptr fpstatus)
{
    gen_helper_vfp_muls(vn, vn, vm, fpstatus);
    gen_helper_vfp_subs(vd, vd, vn, fpstatus);
}

DO_3S_FP(VMLA, gen_VMLA_fp_3s, true)
DO_3S_FP(VMLS, gen_VMLS_fp_3s, true)

static bool trans_VMAXNM_fp_3s(DisasContext *s, arg_3same *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8)) {
            return false;
    }

    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same_fp(s, a, gen_helper_vfp_maxnums, false);
}

static bool trans_VMINNM_fp_3s(DisasContext *s, arg_3same *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8)) {
            return false;
    }

    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same_fp(s, a, gen_helper_vfp_minnums, false);
}

static void gen_VRECPS_fp_3s(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm)
{
    gen_helper_recps_f32(vd, vn, vm, cpu_env);
}

static bool trans_VRECPS_fp_3s(DisasContext *s, arg_3same *a)
{
    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same_32(s, a, gen_VRECPS_fp_3s);
}

static void gen_VRSQRTS_fp_3s(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm)
{
    gen_helper_rsqrts_f32(vd, vn, vm, cpu_env);
}

static bool trans_VRSQRTS_fp_3s(DisasContext *s, arg_3same *a)
{
    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same_32(s, a, gen_VRSQRTS_fp_3s);
}

static bool do_3same_fp_pair(DisasContext *s, arg_3same *a, VFPGen3OpSPFn *fn)
{
    /* FP operations handled pairwise 32 bits at a time */
    TCGv_i32 tmp, tmp2, tmp3;
    TCGv_ptr fpstatus;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    assert(a->q == 0); /* enforced by decode patterns */

    /*
     * Note that we have to be careful not to clobber the source operands
     * in the "vm == vd" case by storing the result of the first pass too
     * early. Since Q is 0 there are always just two passes, so instead
     * of a complicated loop over each pass we just unroll.
     */
    fpstatus = get_fpstatus_ptr(1);
    tmp = neon_load_reg(a->vn, 0);
    tmp2 = neon_load_reg(a->vn, 1);
    fn(tmp, tmp, tmp2, fpstatus);
    tcg_temp_free_i32(tmp2);

    tmp3 = neon_load_reg(a->vm, 0);
    tmp2 = neon_load_reg(a->vm, 1);
    fn(tmp3, tmp3, tmp2, fpstatus);
    tcg_temp_free_i32(tmp2);
    tcg_temp_free_ptr(fpstatus);

    neon_store_reg(a->vd, 0, tmp);
    neon_store_reg(a->vd, 1, tmp3);
    return true;
}

/*
 * For all the functions using this macro, size == 1 means fp16,
 * which is an architecture extension we don't implement yet.
 */
#define DO_3S_FP_PAIR(INSN,FUNC)                                    \
    static bool trans_##INSN##_fp_3s(DisasContext *s, arg_3same *a) \
    {                                                               \
        if (a->size != 0) {                                         \
            /* TODO fp16 support */                                 \
            return false;                                           \
        }                                                           \
        return do_3same_fp_pair(s, a, FUNC);                        \
    }

DO_3S_FP_PAIR(VPADD, gen_helper_vfp_adds)
DO_3S_FP_PAIR(VPMAX, gen_helper_vfp_maxs)
DO_3S_FP_PAIR(VPMIN, gen_helper_vfp_mins)
