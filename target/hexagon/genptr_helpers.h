/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GENPTR_HELPERS_H
#define GENPTR_HELPERS_H

static inline TCGv gen_read_reg(TCGv result, int num)
{
    tcg_gen_mov_tl(result, hex_gpr[num]);
    return result;
}

static inline TCGv gen_read_preg(TCGv pred, uint8_t num)
{
    tcg_gen_mov_tl(pred, hex_pred[num]);
    return pred;
}

static inline TCGv gen_newreg_st(TCGv result, TCGv_env cpu_env, TCGv rnum)
{
    gen_helper_new_value(result, cpu_env, rnum);
    return result;
}

static inline bool is_preloaded(DisasContext *ctx, int num)
{
    int i;
    for (i = 0; i < ctx->ctx_reg_log_idx; i++) {
        if (ctx->ctx_reg_log[i] == num) {
            return true;
        }
    }
    return false;
}

static inline void gen_log_reg_write(int rnum, TCGv val, int slot,
                                     int is_predicated)
{
    if (is_predicated) {
        TCGv one = tcg_const_tl(1);
        TCGv zero = tcg_const_tl(0);
        TCGv slot_mask = tcg_temp_new();

        tcg_gen_andi_tl(slot_mask, hex_slot_cancelled, 1 << slot);
        tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum], slot_mask, zero,
                           val, hex_new_value[rnum]);

        tcg_temp_free(one);
        tcg_temp_free(zero);
        tcg_temp_free(slot_mask);
    } else {
        tcg_gen_mov_tl(hex_new_value[rnum], val);
    }
}

static inline void gen_log_reg_write_pair(int rnum, TCGv_i64 val, int slot,
                                          int is_predicated)
{
    TCGv val32 = tcg_temp_new();

    if (is_predicated) {
        TCGv one = tcg_const_tl(1);
        TCGv zero = tcg_const_tl(0);
        TCGv slot_mask = tcg_temp_new();

        tcg_gen_andi_tl(slot_mask, hex_slot_cancelled, 1 << slot);
        /* Low word */
        tcg_gen_extrl_i64_i32(val32, val);
        tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum], slot_mask, zero,
                           val32, hex_new_value[rnum]);
        /* High word */
        tcg_gen_extrh_i64_i32(val32, val);
        tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum + 1],
                           slot_mask, zero,
                           val32, hex_new_value[rnum + 1]);

        tcg_temp_free(one);
        tcg_temp_free(zero);
        tcg_temp_free(slot_mask);
    } else {
        /* Low word */
        tcg_gen_extrl_i64_i32(val32, val);
        tcg_gen_mov_tl(hex_new_value[rnum], val32);
        /* High word */
        tcg_gen_extrh_i64_i32(val32, val);
        tcg_gen_mov_tl(hex_new_value[rnum + 1], val32);
    }

    tcg_temp_free(val32);
}

static inline void gen_log_pred_write(int pnum, TCGv val)
{
    TCGv zero = tcg_const_tl(0);
    TCGv base_val = tcg_temp_local_new();
    TCGv and_val = tcg_temp_local_new();

    /* Multiple writes to the same preg are and'ed together */
    tcg_gen_andi_tl(base_val, val, 0xff);
    tcg_gen_and_tl(and_val, base_val, hex_new_pred_value[pnum]);
    tcg_gen_movcond_tl(TCG_COND_NE, hex_new_pred_value[pnum],
                       hex_pred_written[pnum], zero,
                       and_val, base_val);
    tcg_gen_movi_tl(hex_pred_written[pnum], 1);

    tcg_temp_free(zero);
    tcg_temp_free(base_val);
    tcg_temp_free(and_val);
}

static inline void gen_read_p3_0(TCGv control_reg)
{
    TCGv pval = tcg_temp_new();
    int i;
    tcg_gen_movi_tl(control_reg, 0);
    for (i = NUM_PREGS - 1; i >= 0; i--) {
        tcg_gen_shli_tl(control_reg, control_reg, 8);
        tcg_gen_andi_tl(pval, hex_pred[i], 0xff);
        tcg_gen_or_tl(control_reg, control_reg, pval);
    }
    tcg_temp_free(pval);
}

static inline void gen_write_p3_0(TCGv tmp)
{
    TCGv control_reg = tcg_temp_new();
    TCGv pred_val = tcg_temp_new();
    int i;

    tcg_gen_mov_tl(control_reg, tmp);
    for (i = 0; i < NUM_PREGS; i++) {
        tcg_gen_andi_tl(pred_val, control_reg, 0xff);
        tcg_gen_mov_tl(hex_pred[i], pred_val);
        tcg_gen_shri_tl(control_reg, control_reg, 8);
    }
    tcg_temp_free(control_reg);
    tcg_temp_free(pred_val);
}

static inline TCGv gen_get_byte(TCGv result, int N, TCGv src, bool sign)
{
    TCGv shift = tcg_const_tl(8 * N);
    TCGv mask = tcg_const_tl(0xff);

    tcg_gen_shr_tl(result, src, shift);
    tcg_gen_and_tl(result, result, mask);
    if (sign) {
        tcg_gen_ext8s_tl(result, result);
    } else {
        tcg_gen_ext8u_tl(result, result);
    }
    tcg_temp_free(mask);
    tcg_temp_free(shift);

    return result;
}

static inline TCGv gen_get_byte_i64(TCGv result, int N, TCGv_i64 src, bool sign)
{
    TCGv_i64 result_i64 = tcg_temp_new_i64();
    TCGv_i64 shift = tcg_const_i64(8 * N);
    TCGv_i64 mask = tcg_const_i64(0xff);
    tcg_gen_shr_i64(result_i64, src, shift);
    tcg_gen_and_i64(result_i64, result_i64, mask);
    tcg_gen_extrl_i64_i32(result, result_i64);
    if (sign) {
        tcg_gen_ext8s_tl(result, result);
    } else {
        tcg_gen_ext8u_tl(result, result);
    }
    tcg_temp_free_i64(result_i64);
    tcg_temp_free_i64(shift);
    tcg_temp_free_i64(mask);

    return result;

}
static inline TCGv gen_get_half(TCGv result, int N, TCGv src, bool sign)
{
    TCGv shift = tcg_const_tl(16 * N);
    TCGv mask = tcg_const_tl(0xffff);

    tcg_gen_shr_tl(result, src, shift);
    tcg_gen_and_tl(result, result, mask);
    if (sign) {
        tcg_gen_ext16s_tl(result, result);
    } else {
        tcg_gen_ext16u_tl(result, result);
    }
    tcg_temp_free(mask);
    tcg_temp_free(shift);

    return result;
}

static inline void gen_set_half(int N, TCGv result, TCGv src)
{
    TCGv mask1 = tcg_const_tl(~(0xffff << (N * 16)));
    TCGv mask2 = tcg_const_tl(0xffff);
    TCGv shift = tcg_const_tl(N * 16);
    TCGv tmp = tcg_temp_new();

    tcg_gen_and_tl(result, result, mask1);
    tcg_gen_and_tl(tmp, src, mask2);
    tcg_gen_shli_tl(tmp, tmp, N * 16);
    tcg_gen_or_tl(result, result, tmp);

    tcg_temp_free(mask1);
    tcg_temp_free(mask2);
    tcg_temp_free(shift);
    tcg_temp_free(tmp);
}

static inline void gen_set_half_i64(int N, TCGv_i64 result, TCGv src)
{
    TCGv_i64 mask1 = tcg_const_i64(~(0xffffLL << (N * 16)));
    TCGv_i64 mask2 = tcg_const_i64(0xffffLL);
    TCGv_i64 shift = tcg_const_i64(N * 16);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_and_i64(result, result, mask1);
    tcg_gen_concat_i32_i64(tmp, src, src);
    tcg_gen_and_i64(tmp, tmp, mask2);
    tcg_gen_shli_i64(tmp, tmp, N * 16);
    tcg_gen_or_i64(result, result, tmp);

    tcg_temp_free_i64(mask1);
    tcg_temp_free_i64(mask2);
    tcg_temp_free_i64(shift);
    tcg_temp_free_i64(tmp);
}

static inline void gen_set_byte(int N, TCGv result, TCGv src)
{
    TCGv mask1 = tcg_const_tl(~(0xff << (N * 8)));
    TCGv mask2 = tcg_const_tl(0xff);
    TCGv shift = tcg_const_tl(N * 8);
    TCGv tmp = tcg_temp_new();

    tcg_gen_and_tl(result, result, mask1);
    tcg_gen_and_tl(tmp, src, mask2);
    tcg_gen_shli_tl(tmp, tmp, N * 8);
    tcg_gen_or_tl(result, result, tmp);

    tcg_temp_free(mask1);
    tcg_temp_free(mask2);
    tcg_temp_free(shift);
    tcg_temp_free(tmp);
}

static inline void gen_set_byte_i64(int N, TCGv_i64 result, TCGv src)
{
    TCGv_i64 mask1 = tcg_const_i64(~(0xffLL << (N * 8)));
    TCGv_i64 mask2 = tcg_const_i64(0xffLL);
    TCGv_i64 shift = tcg_const_i64(N * 8);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_and_i64(result, result, mask1);
    tcg_gen_concat_i32_i64(tmp, src, src);
    tcg_gen_and_i64(tmp, tmp, mask2);
    tcg_gen_shli_i64(tmp, tmp, N * 8);
    tcg_gen_or_i64(result, result, tmp);

    tcg_temp_free_i64(mask1);
    tcg_temp_free_i64(mask2);
    tcg_temp_free_i64(shift);
    tcg_temp_free_i64(tmp);
}

static inline TCGv gen_get_word(TCGv result, int N, TCGv_i64 src, bool sign)
{
    if (N == 0) {
        tcg_gen_extrl_i64_i32(result, src);
    } else if (N == 1) {
        tcg_gen_extrh_i64_i32(result, src);
    } else {
      g_assert_not_reached();
    }
    return result;
}

static inline TCGv_i64 gen_get_word_i64(TCGv_i64 result, int N, TCGv_i64 src,
                                        bool sign)
{
    TCGv word = tcg_temp_new();
    gen_get_word(word, N, src, sign);
    if (sign) {
        tcg_gen_ext_i32_i64(result, word);
    } else {
        tcg_gen_extu_i32_i64(result, word);
    }
    tcg_temp_free(word);
    return result;
}

static inline TCGv gen_set_bit(int i, TCGv result, TCGv src)
{
    TCGv mask = tcg_const_tl(~(1 << i));
    TCGv bit = tcg_temp_new();
    tcg_gen_shli_tl(bit, src, i);
    tcg_gen_and_tl(result, result, mask);
    tcg_gen_or_tl(result, result, bit);
    tcg_temp_free(mask);
    tcg_temp_free(bit);

    return result;
}

static inline void gen_load_locked4u(TCGv dest, TCGv vaddr, int mem_index)
{
    tcg_gen_qemu_ld32u(dest, vaddr, mem_index);
    tcg_gen_mov_tl(llsc_addr, vaddr);
    tcg_gen_mov_tl(llsc_val, dest);
}

static inline void gen_load_locked8u(TCGv_i64 dest, TCGv vaddr, int mem_index)
{
    tcg_gen_qemu_ld64(dest, vaddr, mem_index);
    tcg_gen_mov_tl(llsc_addr, vaddr);
    tcg_gen_mov_i64(llsc_val_i64, dest);
}

static inline void gen_store_conditional4(CPUHexagonState *env,
                                          DisasContext *ctx, int prednum,
                                          TCGv pred, TCGv vaddr, TCGv src)
{
    TCGv tmp = tcg_temp_new();
    TCGLabel *fail = gen_new_label();

    tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPUHexagonState, llsc_addr));
    tcg_gen_brcond_tl(TCG_COND_NE, vaddr, tmp, fail);
    tcg_gen_movi_tl(tmp, prednum);
    tcg_gen_st_tl(tmp, cpu_env, offsetof(CPUHexagonState, llsc_reg));
    tcg_gen_st_tl(src, cpu_env, offsetof(CPUHexagonState, llsc_newval));
    gen_exception(HEX_EXCP_SC4);

    gen_set_label(fail);
    tcg_gen_movi_tl(pred, 0);
    tcg_temp_free(tmp);
}

static inline void gen_store_conditional8(CPUHexagonState *env,
                                          DisasContext *ctx, int prednum,
                                          TCGv pred, TCGv vaddr, TCGv_i64 src)
{
    TCGv tmp = tcg_temp_new();
    TCGLabel *fail = gen_new_label();

    tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPUHexagonState, llsc_addr));
    tcg_gen_brcond_tl(TCG_COND_NE, vaddr, tmp, fail);
    tcg_gen_movi_tl(tmp, prednum);
    tcg_gen_st_tl(tmp, cpu_env, offsetof(CPUHexagonState, llsc_reg));
    tcg_gen_st_i64(src, cpu_env, offsetof(CPUHexagonState, llsc_newval_i64));
    gen_exception(HEX_EXCP_SC8);

    gen_set_label(fail);
    tcg_gen_movi_tl(pred, 0);
    tcg_temp_free(tmp);
}

#endif
