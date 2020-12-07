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

#define OPC_CP1 (0x11 << 26)

/* Coprocessor 1 (rs field) */
#define MASK_CP1(op)                (MASK_OP_MAJOR(op) | (op & (0x1F << 21)))

/* Values for the fmt field in FP instructions */
enum {
    /* 0 - 15 are reserved */
    FMT_S = 16,          /* single fp */
    FMT_D = 17,          /* double fp */
    FMT_E = 18,          /* extended fp */
    FMT_Q = 19,          /* quad fp */
    FMT_W = 20,          /* 32-bit fixed */
    FMT_L = 21,          /* 64-bit fixed */
    FMT_PS = 22,         /* paired single fp */
    /* 23 - 31 are reserved */
};

enum {
    OPC_MFC1     = (0x00 << 21) | OPC_CP1,
    OPC_DMFC1    = (0x01 << 21) | OPC_CP1,
    OPC_CFC1     = (0x02 << 21) | OPC_CP1,
    OPC_MFHC1    = (0x03 << 21) | OPC_CP1,
    OPC_MTC1     = (0x04 << 21) | OPC_CP1,
    OPC_DMTC1    = (0x05 << 21) | OPC_CP1,
    OPC_CTC1     = (0x06 << 21) | OPC_CP1,
    OPC_MTHC1    = (0x07 << 21) | OPC_CP1,
    OPC_BC1      = (0x08 << 21) | OPC_CP1, /* bc */
    OPC_BC1ANY2  = (0x09 << 21) | OPC_CP1,
    OPC_BC1ANY4  = (0x0A << 21) | OPC_CP1,
    OPC_BZ_V     = (0x0B << 21) | OPC_CP1,
    OPC_BNZ_V    = (0x0F << 21) | OPC_CP1,
    OPC_S_FMT    = (FMT_S << 21) | OPC_CP1,
    OPC_D_FMT    = (FMT_D << 21) | OPC_CP1,
    OPC_E_FMT    = (FMT_E << 21) | OPC_CP1,
    OPC_Q_FMT    = (FMT_Q << 21) | OPC_CP1,
    OPC_W_FMT    = (FMT_W << 21) | OPC_CP1,
    OPC_L_FMT    = (FMT_L << 21) | OPC_CP1,
    OPC_PS_FMT   = (FMT_PS << 21) | OPC_CP1,
    OPC_BC1EQZ   = (0x09 << 21) | OPC_CP1,
    OPC_BC1NEZ   = (0x0D << 21) | OPC_CP1,
    OPC_BZ_B     = (0x18 << 21) | OPC_CP1,
    OPC_BZ_H     = (0x19 << 21) | OPC_CP1,
    OPC_BZ_W     = (0x1A << 21) | OPC_CP1,
    OPC_BZ_D     = (0x1B << 21) | OPC_CP1,
    OPC_BNZ_B    = (0x1C << 21) | OPC_CP1,
    OPC_BNZ_H    = (0x1D << 21) | OPC_CP1,
    OPC_BNZ_W    = (0x1E << 21) | OPC_CP1,
    OPC_BNZ_D    = (0x1F << 21) | OPC_CP1,
};

#define MASK_CP1_FUNC(op)           (MASK_CP1(op) | (op & 0x3F))
#define MASK_BC1(op)                (MASK_CP1(op) | (op & (0x3 << 16)))

enum {
    OPC_BC1F     = (0x00 << 16) | OPC_BC1,
    OPC_BC1T     = (0x01 << 16) | OPC_BC1,
    OPC_BC1FL    = (0x02 << 16) | OPC_BC1,
    OPC_BC1TL    = (0x03 << 16) | OPC_BC1,
};

enum {
    OPC_BC1FANY2     = (0x00 << 16) | OPC_BC1ANY2,
    OPC_BC1TANY2     = (0x01 << 16) | OPC_BC1ANY2,
};

enum {
    OPC_BC1FANY4     = (0x00 << 16) | OPC_BC1ANY4,
    OPC_BC1TANY4     = (0x01 << 16) | OPC_BC1ANY4,
};

extern TCGv_i32 fpu_fcr0, fpu_fcr31;
extern TCGv_i64 fpu_f64[32];

void gen_load_fpr64(DisasContext *ctx, TCGv_i64 t, int reg);
void gen_store_fpr64(DisasContext *ctx, TCGv_i64 t, int reg);

int get_fp_bit(int cc);

void check_cp1_enabled(DisasContext *ctx);

#endif
