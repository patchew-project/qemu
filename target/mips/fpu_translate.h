/*
 * FPU-related MIPS translation routines.
 *
 *  Copyright (C) 2004-2005  Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef TARGET_MIPS_FPU_TRANSLATE_H
#define TARGET_MIPS_FPU_TRANSLATE_H

#include "exec/translator.h"
#include "translate.h"

extern TCGv_i32 fpu_fcr0, fpu_fcr31;
extern TCGv_i64 fpu_f64[32];

void gen_load_fpr64(DisasContext *ctx, TCGv_i64 t, int reg);
void gen_store_fpr64(DisasContext *ctx, TCGv_i64 t, int reg);

int get_fp_bit(int cc);

void check_cp1_enabled(DisasContext *ctx);

#endif
