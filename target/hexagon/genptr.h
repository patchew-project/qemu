/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_GENPTR_H
#define HEXAGON_GENPTR_H

#include "insn.h"
#include "tcg/tcg.h"
#include "translate.h"

extern const SemanticInsn opcode_genptr[];

TCGv gen_read_reg(TCGv result, int num);
TCGv gen_read_preg(TCGv pred, uint8_t num);
void gen_log_reg_write(int rnum, TCGv val);
void gen_log_pred_write(DisasContext *ctx, int pnum, TCGv val);
void gen_store32(DisasContext *ctx, TCGv vaddr, TCGv src, tcg_target_long width,
                 unsigned slot);
void gen_store1(TCGv_env cpu_env, TCGv vaddr, TCGv src, DisasContext *ctx,
                unsigned slot);
void gen_store2(TCGv_env cpu_env, TCGv vaddr, TCGv src, DisasContext *ctx,
                unsigned slot);
void gen_store4(TCGv_env cpu_env, TCGv vaddr, TCGv src, DisasContext *ctx,
                unsigned slot);
void gen_store8(TCGv_env cpu_env, TCGv vaddr, TCGv_i64 src, DisasContext *ctx,
                unsigned slot);
void gen_write_new_pc(TCGv addr);
void gen_set_usr_field(int field, TCGv val);
void gen_set_usr_fieldi(int field, int x);
void gen_sat_i32(TCGv dest, TCGv source, int width);
void gen_sat_i32_ext(TCGv ovfl, TCGv dest, TCGv source, int width);
void gen_satu_i32(TCGv dest, TCGv source, int width);
void gen_satu_i32_ext(TCGv ovfl, TCGv dest, TCGv source, int width);
void gen_sat_i64(TCGv_i64 dest, TCGv_i64 source, int width);
void gen_sat_i64_ext(TCGv ovfl, TCGv_i64 dest, TCGv_i64 source, int width);
void gen_satu_i64(TCGv_i64 dest, TCGv_i64 source, int width);
void gen_satu_i64_ext(TCGv ovfl, TCGv_i64 dest, TCGv_i64 source, int width);
void gen_fbrev(TCGv result, TCGv src);

#endif
