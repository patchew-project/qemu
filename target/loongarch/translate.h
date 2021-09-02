/*
 * LoongArch translation routines.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef TARGET_LOONGARCH_TRANSLATE_H
#define TARGET_LOONGARCH_TRANSLATE_H

#include "exec/translator.h"

#define FCMP_LT   0x0001  /* fp0 < fp1 */
#define FCMP_EQ   0x0010  /* fp0 = fp1 */
#define FCMP_GT   0x0100  /* fp1 < fp0 */
#define FCMP_UN   0x1000  /* unordered */

#define TRANS(NAME, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
    { return FUNC(ctx, a, __VA_ARGS__); }

/*
 * If an operation is being performed on less than TARGET_LONG_BITS,
 * it may require the inputs to be sign- or zero-extended; which will
 * depend on the exact operation being performed.
 */
typedef enum {
    EXT_NONE,
    EXT_SIGN,
    EXT_ZERO,
} DisasExtend;

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong page_start;
    uint32_t opcode;
    int mem_idx;
    TCGv zero;
    DisasExtend dst_ext;
    /* Space for 3 operands plus 1 extra for address computation. */
    TCGv temp[4];
    uint8_t ntemp;
} DisasContext;

void generate_exception(DisasContext *ctx, int excp);

extern TCGv cpu_gpr[32], cpu_pc;
extern TCGv_i32 cpu_fscr0;
extern TCGv_i64 cpu_fpr[32];

int ieee_ex_to_loongarch(int xcpt);

#endif
