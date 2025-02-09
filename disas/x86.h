/*
 * Copyright (c) 2024-2025 Michael Clark
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <stddef.h>
#include <string.h>

#include "qemu/bswap.h"

/*
 * types
 */

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned long long ullong;
typedef long long llong;

typedef char i8;
typedef short i16;
typedef int i32;
typedef llong i64;

typedef uchar u8;
typedef ushort u16;
typedef uint u32;
typedef ullong u64;

/*
 * x86 forward declarations
 */

typedef struct x86_rex x86_rex;
typedef struct x86_rex2 x86_rex2;
typedef struct x86_vex2 x86_vex2;
typedef struct x86_vex3 x86_vex3;
typedef struct x86_evex x86_evex;
typedef struct x86_modrm x86_modrm;
typedef struct x86_sib x86_sib;

typedef struct x86_mem x86_mem;
typedef struct x86_codec x86_codec;
typedef struct x86_modeb x86_modeb;
typedef struct x86_opc_data x86_opc_data;
typedef struct x86_opr_data x86_opr_data;
typedef struct x86_ord_data x86_ord_data;
typedef struct x86_table_idx x86_table_idx;
typedef struct x86_acc_idx x86_acc_idx;
typedef struct x86_acc_entry x86_acc_entry;
typedef struct x86_buffer x86_buffer;
typedef struct x86_ctx x86_ctx;

/*
 * simple debug macros
 */

#define VA_ARGS(...) , ##__VA_ARGS__
#define x86_debug(fmt, ...) if (debug) \
    printf(fmt "\n" VA_ARGS(__VA_ARGS__))
#define x86_debugf(fmt, ...) if (debug) \
    printf("%s: " fmt "\n", __func__ VA_ARGS(__VA_ARGS__))

/*
 * register groups
 */

enum
{
    x86_reg_w     = (0 << 5),
    x86_reg_b     = (1 << 5),
    x86_reg_d     = (2 << 5),
    x86_reg_q     = (3 << 5),

    x86_reg_mmx   = (4 << 5),
    x86_reg_xmm   = (5 << 5),
    x86_reg_ymm   = (6 << 5),
    x86_reg_zmm   = (7 << 5),

    x86_reg_kmask = (8 << 5),
    x86_reg_bl    = (9 << 5),
    x86_reg_fpu   = (10 << 5),
    x86_reg_bnd   = (11 << 5),

    x86_reg_dreg  = (12 << 5),
    x86_reg_creg  = (13 << 5),
    x86_reg_sreg  = (14 << 5),
    x86_reg_sys   = (15 << 5),
};

/*
 * prefix byte
 */

enum
{
    x86_pb_26    = 0x26,
    x86_pb_2e    = 0x2e,
    x86_pb_36    = 0x36,
    x86_pb_3e    = 0x3e,
    x86_pb_41    = 0x41,
    x86_pb_42    = 0x42,
    x86_pb_44    = 0x44,
    x86_pb_48    = 0x48,
    x86_pb_62    = 0x62,
    x86_pb_64    = 0x64,
    x86_pb_65    = 0x65,
    x86_pb_66    = 0x66,
    x86_pb_67    = 0x67,
    x86_pb_9b    = 0x9b,
    x86_pb_c4    = 0xc4,
    x86_pb_c5    = 0xc5,
    x86_pb_d5    = 0xd5,
    x86_pb_f0    = 0xf0,
    x86_pb_f2    = 0xf2,
    x86_pb_f3    = 0xf3,

    x86_pb_es    = x86_pb_26,
    x86_pb_cs    = x86_pb_2e, /* branch not taken with Jcc */
    x86_pb_ss    = x86_pb_36,
    x86_pb_ds    = x86_pb_3e, /* branch taken with Jcc (CET notrack) */
    x86_pb_rex_b = x86_pb_41,
    x86_pb_rex_x = x86_pb_42,
    x86_pb_rex_r = x86_pb_44,
    x86_pb_rex_w = x86_pb_48,
    x86_pb_evex  = x86_pb_62,
    x86_pb_fs    = x86_pb_64,
    x86_pb_gs    = x86_pb_65,
    x86_pb_osize = x86_pb_66,
    x86_pb_asize = x86_pb_67,
    x86_pb_wait  = x86_pb_9b, /* x87 */
    x86_pb_vex3  = x86_pb_c4,
    x86_pb_vex2  = x86_pb_c5,
    x86_pb_rex2  = x86_pb_d5,
    x86_pb_lock  = x86_pb_f0,
    x86_pb_repne = x86_pb_f2, /* REPNE/REPNZ and XACQUIRE */
    x86_pb_rep   = x86_pb_f3, /* REP/REPE/REPZ and XRELEASE */
};

/*
 * mod values
 */

enum
{
    x86_mod_disp0,
    x86_mod_disp8,
    x86_mod_dispw,
    x86_mod_reg,
};

/*
 * SIB scale
 */

enum
{
    x86_scale_1,
    x86_scale_2,
    x86_scale_4,
    x86_scale_8,
};

/*
 * segment
 */

enum
{
    x86_seg_none,
    x86_seg_es,
    x86_seg_cs,
    x86_seg_ss,
    x86_seg_ds,
    x86_seg_fs,
    x86_seg_gs
};

/*
 * REX flags
 */

enum
{
    x86_rex_b = 0x01,
    x86_rex_x = 0x02,
    x86_rex_r = 0x04,
    x86_rex_w = 0x08,
};

/*
 * VEX prefix
 */

enum
{
    x86_pfx_none,
    x86_pfx_66,
    x86_pfx_f3,
    x86_pfx_f2,
    x86_pfx_9b,
    x86_pfx_rexw = 8
};

/*
 * VEX map
 */

enum
{
    x86_map_none,
    x86_map_0f,
    x86_map_0f38,
    x86_map_0f3a,
    x86_map_map4,
    x86_map_map5,
    x86_map_map6,
};

/*
 * VEX length
 */

enum
{
    x86_vex_l0 = 0,
    x86_vex_l1 = 1,
    x86_vex_l2 = 2,
    x86_vex_l3 = 3,
    x86_vex_lz = x86_vex_l0,
    x86_vex_lig = x86_vex_l3,
    x86_vex_l128 = x86_vex_l0,
    x86_vex_l256 = x86_vex_l1,
    x86_vex_l512 = x86_vex_l2,
};

/*
 * VEX width
 */

enum
{
    x86_vex_w0,
    x86_vex_w1,
};

/*
 * condition codes (x86)
 */

enum
{
    x86_cc_o     = 0,  /* overflow */
    x86_cc_no    = 1,  /* no overflow */
    x86_cc_b     = 2,  /* below */
    x86_cc_c     = 2,  /* carry */
    x86_cc_nae   = 2,  /* neither-above-nor-equal */
    x86_cc_nb    = 3,  /* not-below */
    x86_cc_nc    = 3,  /* not-carry */
    x86_cc_ae    = 3,  /* above-or-equal */
    x86_cc_e     = 4,  /* equal */
    x86_cc_z     = 4,  /* zero */
    x86_cc_ne    = 5,  /* not-equal */
    x86_cc_nz    = 5,  /* not-zero */
    x86_cc_be    = 6,  /* below-or-equal */
    x86_cc_na    = 6,  /* not-above */
    x86_cc_nbe   = 7,  /* neither-below-nor-equal */
    x86_cc_a     = 7,  /* above */
    x86_cc_s     = 8,  /* sign */
    x86_cc_ns    = 9,  /* no-sign */
    x86_cc_p     = 10, /* parity */
    x86_cc_pe    = 10, /* parity-even */
    x86_cc_np    = 11, /* no-parity */
    x86_cc_po    = 11, /* parity-odd */
    x86_cc_l     = 12, /* less */
    x86_cc_nge   = 12, /* neither-greater-nor-equal */
    x86_cc_nl    = 13, /* not-less */
    x86_cc_ge    = 13, /* greater-or-equal */
    x86_cc_le    = 14, /* less-or-equal */
    x86_cc_ng    = 14, /* not-greater */
    x86_cc_nle   = 15, /* neither-less-nor-equal- */
    x86_cc_g     = 15, /* greater */
};

/*
 * condition codes (agnostic)
 */

enum
{
    x86_o        = 0,  /* overflow (signed) */
    x86_no       = 1,  /* not-overflow (signed) */
    x86_c        = 2,  /* carry */
    x86_nc       = 3,  /* not-carry */
    x86_ltu      = 2,  /* less-than (unsigned) */
    x86_geu      = 3,  /* greater-than-or-equal (unsigned) */
    x86_eq       = 4,  /* equal */
    x86_ne       = 5,  /* not-equal */
    x86_leu      = 6,  /* less-than-or-equal (unsigned) */
    x86_gtu      = 7,  /* greater-than (unsigned) */
    x86_s        = 8,  /* sign */
    x86_ns       = 9,  /* not-sign */
    x86_lt       = 12, /* less-than (signed) */
    x86_ge       = 13, /* greater-than-or-equal (signed) */
    x86_le       = 14, /* less-than-or-equal (signed) */
    x86_gt       = 15, /* greater-than (signed) */
};

/*
 * compare packed operators
 *
 * oq = ordered queit, os = ordered signalling
 * uq = unordered queit, us = unordered signalling
 */

enum
{
    x86_eq_oq    = 0x00,
    x86_lt_os    = 0x01,
    x86_le_os    = 0x02,
    x86_unord_q  = 0x03,
    x86_neq_uq   = 0x04,
    x86_nlt_us   = 0x05,
    x86_nle_us   = 0x06,
    x86_ord_q    = 0x07,
    x86_eq_uq    = 0x08,
    x86_nge_us   = 0x09,
    x86_ngt_us   = 0x0a,
    x86_false_oq = 0x0b,
    x86_neq_oq   = 0x0c,
    x86_ge_os    = 0x0d,
    x86_gt_os    = 0x0e,
    x86_true_uq  = 0x0f,
    x86_eq_os    = 0x10,
    x86_lt_oq    = 0x11,
    x86_le_oq    = 0x12,
    x86_unord_s  = 0x13,
    x86_neq_us   = 0x14,
    x86_nlt_uq   = 0x15,
    x86_nle_uq   = 0x16,
    x86_ord_s    = 0x17,
    x86_eq_us    = 0x18,
    x86_nge_uq   = 0x19,
    x86_ngt_uq   = 0x1a,
    x86_false_os = 0x1b,
    x86_neq_os   = 0x1c,
    x86_ge_oq    = 0x1d,
    x86_gt_oq    = 0x1e,
    x86_true_us  = 0x1f,
};

/*
 * prefix encoding structures
 */

/*
 * ModRM struct
 */

struct x86_modrm
{
    union {
        uchar data[1];
        /* note: bitfield used for documentation purposes only */
        struct {
            /* [0:2] */ uchar rm:3;
            /* [3:5] */ uchar reg:3;
            /* [6:7] */ uchar mod:2;
        };
    };
};

/*
 * ModRM values
 */

enum
{
    x86_rm_sp_sib       = 4,
    x86_rm_bp_disp0     = 5,

    x86_modrm_rm_mask   = 7,
    x86_modrm_reg_shift = 3,
    x86_modrm_reg_mask  = 7,
    x86_modrm_mod_shift = 6,
    x86_modrm_mod_mask  = 3,
};

/*
 * ModRM accessors
 */

static inline uint x86_modrm_rm(uchar modrm) {
    return modrm & x86_modrm_rm_mask;
}
static inline uint x86_modrm_reg(uchar modrm) {
    return (modrm >> x86_modrm_reg_shift) & x86_modrm_reg_mask;
}
static inline uint x86_modrm_mod(uchar modrm) {
    return (modrm >> x86_modrm_mod_shift) & x86_modrm_mod_mask;
}

/*
 * SIB struct
 */

struct x86_sib
{
    union {
        uchar data[1];
        /* note: bitfield used for documentation purposes only */
        struct {
            /* [0:2] */ uchar b : 3; /* base[0:2] */
            /* [3:5] */ uchar x : 3; /* index[0:2] */
            /* [6:7] */ uchar s : 2; /* scale[0:2] */
        };
    };
};

/*
 * SIB values
 */

enum {
    x86_sib_b_mask = 7,
    x86_sib_x_shift = 3,
    x86_sib_x_mask = 7,
    x86_sib_s_shift = 6,
    x86_sib_s_mask = 3
};

/*
 * SIB accessors
 */

static inline uint x86_sib_b(uchar sib) {
    return sib & x86_sib_b_mask;
}
static inline uint x86_sib_x(uchar sib) {
    return (sib >> x86_sib_x_shift) & x86_sib_x_mask;
}
static inline uint x86_sib_s(uchar sib) {
    return (sib >> x86_sib_s_shift) & x86_sib_s_mask;
}

/*
 * REX struct
 */

struct x86_rex
{
    union {
        uchar data[1];
        /* note: bitfield used for documentation purposes only */
        struct {
            /* [0] */ uchar b3 : 1; /* base[3] */
            /* [1] */ uchar x3 : 1; /* index[3] */
            /* [2] */ uchar r3 : 1; /* reg[3] */
            /* [3] */ uchar w  : 1; /* width */
        };
    };
};


/*
 * REX2 struct
 */

struct x86_rex2
{
    union {
        uchar data[1];
        /* note: bitfield used for documentation purposes only */
        struct {
            /* [0] */ uchar b3 : 1; /* base[3] */
            /* [1] */ uchar x3 : 1; /* index[3] */
            /* [2] */ uchar r3 : 1; /* reg[3] */
            /* [3] */ uchar w  : 1; /* width */
            /* [4] */ uchar b4 : 1; /* base[4] */
            /* [5] */ uchar x4 : 1; /* index[4] */
            /* [6] */ uchar r4 : 1; /* reg[4] */
            /* [7] */ uchar m0 : 1; /* map prefix 0x0f */
        };
    };
};


/*
 * VEX2 struct
 */

struct x86_vex2
{
    union {
        uchar data[1];
        /* note: bitfield used for documentation purposes only */
        struct {
            /* [0:1] */ uchar p   : 2; /* see x86_pfx */
            /*   [2] */ uchar l   : 1; /* len 0=128 1=256 */
            /* [3:6] */ uchar vn  : 4; /* ~vec[0:3] */
            /*   [7] */ uchar r3n : 1; /* ~reg[3] */
        };
    };
};

/*
 * VEX3 struct
 */

struct x86_vex3
{
    union {
        uchar data[2];
        /* note: bitfield used for documentation purposes only */
        struct {
            /* [0:4] */ uchar m   : 5; /* see x86_map */
            /*   [5] */ uchar b3n : 1; /* ~base[3] */
            /*   [6] */ uchar x3n : 1; /* ~index[3] */
            /*   [7] */ uchar r3n : 1; /* ~reg[3] */

            /* [0:1] */ uchar p   : 2; /* see x86_pfx */
            /*   [2] */ uchar l   : 1; /* len 0=128 1=256 */
            /* [3:6] */ uchar vn  : 4; /* ~vec[0:3] */
            /*   [7] */ uchar w   : 1; /* width */
        };
    };
};

/*
 * EVEX struct
 */

struct x86_evex
{
    union {
        uchar data[3];
        /* note: bitfield used for documentation purposes only */
        struct {
            /* [0:2] */ uchar m   : 3; /* see x86_map */
            /*   [3] */ uchar b4  : 1; /* base[4] */
            /*   [4] */ uchar r4n : 1; /* ~reg[4] */
            /*   [5] */ uchar b3n : 1; /* ~base[3] */
            /*   [6] */ uchar x3n : 1; /* ~index[3] */
            /*   [7] */ uchar r3n : 1; /* ~reg[3] */

            /* [0:1] */ uchar p   : 2; /* see x86_pfx */
            /*   [2] */ uchar x4n : 1; /* ~index[4] */
            /* [3:6] */ uchar vn  : 4; /* ~vec[0:3] */
            /*   [7] */ uchar w   : 1; /* width */

            /* [0:2] */ uchar a   : 3; /* kmask[0:2] or map4.NF[2] no-flags */
            /*   [3] */ uchar v4n : 1; /* ~vec[4] */
            /*   [4] */ uchar br  : 1; /* broadcast or map4.ND[4] new-dest */
            /* [5:6] */ uchar l   : 2; /* len 00=128 01=256, 10=512 */
            /*   [7] */ uchar z   : 1; /* merge/zero */
        };
    };
};

/*
 * opcode encoding
 */

enum
{
    x86_enc_w_shift          = 0,
    x86_enc_m_shift          = x86_enc_w_shift + 3,
    x86_enc_p_shift          = x86_enc_m_shift + 3,
    x86_enc_l_shift          = x86_enc_p_shift + 4,
    x86_enc_t_shift          = x86_enc_l_shift + 3,
    x86_enc_o_shift          = x86_enc_t_shift + 2,
    x86_enc_f_shift          = x86_enc_o_shift + 1,
    x86_enc_i_shift          = x86_enc_f_shift + 3,
    x86_enc_j_shift          = x86_enc_i_shift + 3,
    x86_enc_r_shift          = x86_enc_j_shift + 2,
    x86_enc_s_shift          = x86_enc_r_shift + 3,

    x86_enc_w_w0             = (1 << x86_enc_w_shift),
    x86_enc_w_w1             = (2 << x86_enc_w_shift),
    x86_enc_w_wb             = (3 << x86_enc_w_shift),
    x86_enc_w_wn             = (4 << x86_enc_w_shift),
    x86_enc_w_wx             = (5 << x86_enc_w_shift),
    x86_enc_w_ww             = (6 << x86_enc_w_shift),
    x86_enc_w_wig            = (7 << x86_enc_w_shift),
    x86_enc_w_mask           = (7 << x86_enc_w_shift),

    x86_enc_m_none           = (0 << x86_enc_m_shift),
    x86_enc_m_0f             = (1 << x86_enc_m_shift),
    x86_enc_m_0f38           = (2 << x86_enc_m_shift),
    x86_enc_m_0f3a           = (3 << x86_enc_m_shift),
    x86_enc_m_map4           = (4 << x86_enc_m_shift),
    x86_enc_m_map5           = (5 << x86_enc_m_shift),
    x86_enc_m_map6           = (6 << x86_enc_m_shift),
    x86_enc_m_mask           = (7 << x86_enc_m_shift),

    x86_enc_p_none           = (0 << x86_enc_p_shift),
    x86_enc_p_66             = (1 << x86_enc_p_shift),
    x86_enc_p_f3             = (2 << x86_enc_p_shift),
    x86_enc_p_f2             = (3 << x86_enc_p_shift),
    x86_enc_p_9b             = (4 << x86_enc_p_shift),
    x86_enc_p_rexw           = (8 << x86_enc_p_shift),
    x86_enc_p_mask           = (7 << x86_enc_p_shift),
    x86_enc_prexw_mask       = (15 << x86_enc_p_shift),

    x86_enc_l_lz             = (1 << x86_enc_l_shift),
    x86_enc_l_l0             = (2 << x86_enc_l_shift),
    x86_enc_l_l1             = (3 << x86_enc_l_shift),
    x86_enc_l_128            = (4 << x86_enc_l_shift),
    x86_enc_l_256            = (5 << x86_enc_l_shift),
    x86_enc_l_512            = (6 << x86_enc_l_shift),
    x86_enc_l_lig            = (7 << x86_enc_l_shift),
    x86_enc_l_mask           = (7 << x86_enc_l_shift),

    x86_enc_t_none           = (0 << x86_enc_t_shift),
    x86_enc_t_lex            = (1 << x86_enc_t_shift),
    x86_enc_t_vex            = (2 << x86_enc_t_shift),
    x86_enc_t_evex           = (3 << x86_enc_t_shift),
    x86_enc_t_mask           = (3 << x86_enc_t_shift),

    x86_enc_o_opcode_r       = (1 << x86_enc_o_shift), /* XX+r */
    x86_enc_o_mask           = (1 << x86_enc_o_shift),

    x86_enc_f_modrm_r        = (1 << x86_enc_f_shift), /* /r */
    x86_enc_f_modrm_n        = (2 << x86_enc_f_shift), /* /N */
    x86_enc_f_opcode         = (3 << x86_enc_f_shift), /* XX */
    x86_enc_f_opcode_r       = (4 << x86_enc_f_shift), /* XX+r */
    x86_enc_f_mask           = (7 << x86_enc_f_shift),

    x86_enc_i_ib             = (1 << x86_enc_i_shift),
    x86_enc_i_iw             = (2 << x86_enc_i_shift),
    x86_enc_i_iwd            = (3 << x86_enc_i_shift),
    x86_enc_i_i16            = (4 << x86_enc_i_shift),
    x86_enc_i_i32            = (5 << x86_enc_i_shift),
    x86_enc_i_i64            = (6 << x86_enc_i_shift),
    x86_enc_i_mask           = (7 << x86_enc_i_shift),

    x86_enc_j_ib             = (1 << x86_enc_j_shift),
    x86_enc_j_i16            = (2 << x86_enc_j_shift),
    x86_enc_j_mask           = (3 << x86_enc_j_shift),

    x86_enc_r_rep            = (1 << x86_enc_r_shift),
    x86_enc_r_lock           = (2 << x86_enc_r_shift),
    x86_enc_r_norexb         = (4 << x86_enc_r_shift),
    x86_enc_r_mask           = (7 << x86_enc_r_shift),

    x86_enc_s_o16            = (1 << x86_enc_s_shift),
    x86_enc_s_o32            = (2 << x86_enc_s_shift),
    x86_enc_s_o64            = (3 << x86_enc_s_shift),
    x86_enc_s_a16            = (4 << x86_enc_s_shift),
    x86_enc_s_a32            = (5 << x86_enc_s_shift),
    x86_enc_s_a64            = (6 << x86_enc_s_shift),
    x86_enc_s_mask           = (7 << x86_enc_s_shift),

    x86_enc_immediate_mask   = x86_enc_i_mask | x86_enc_j_mask,
    x86_enc_suffix_mask      = x86_enc_r_mask | x86_enc_s_mask,
    x86_enc_param_mask       = x86_enc_immediate_mask | x86_enc_suffix_mask
};

/*
 * opcode encoding accessors
 */

static inline uint x86_enc_width(uint enc) {
    return enc & x86_enc_w_mask;
}
static inline uint x86_enc_prefix(uint enc) {
    return enc & x86_enc_prexw_mask;
}
static inline uint x86_enc_length(uint enc) {
    return enc & x86_enc_l_mask;
}
static inline uint x86_enc_opcode(uint enc) {
    return enc & x86_enc_o_mask;
}
static inline uint x86_enc_func(uint enc) {
    return enc & x86_enc_f_mask;
}
static inline uint x86_enc_map(uint enc) {
    return enc & x86_enc_m_mask;
}
static inline uint x86_enc_imm(uint enc) {
    return enc & x86_enc_i_mask;
}
static inline uint x86_enc_imm2(uint enc) {
    return enc & x86_enc_j_mask;
}
static inline uint x86_enc_type(uint enc) {
    return enc & x86_enc_t_mask;
}
static inline uint x86_enc_suffix(uint enc) {
    return enc & x86_enc_suffix_mask;
}
static inline uint x86_enc_leading(uint enc) {
    return enc & ~x86_enc_param_mask;
}
static inline uint x86_enc_has_rep(uint enc) {
    return enc & x86_enc_r_rep;
}
static inline uint x86_enc_has_lock(uint enc) {
    return enc & x86_enc_r_lock;
}
static inline uint x86_enc_has_norexb(uint enc) {
    return enc & x86_enc_r_norexb;
}
static inline uint x86_enc_has_o16(uint enc) {
    return (enc & x86_enc_s_mask) == x86_enc_s_o16;
}
static inline uint x86_enc_has_o32(uint enc) {
    return (enc & x86_enc_s_mask) == x86_enc_s_o32;
}
static inline uint x86_enc_has_o64(uint enc) {
    return (enc & x86_enc_s_mask) == x86_enc_s_o64;
}
static inline uint x86_enc_has_a16(uint enc) {
    return (enc & x86_enc_s_mask) == x86_enc_s_a16;
}
static inline uint x86_enc_has_a32(uint enc) {
    return (enc & x86_enc_s_mask) == x86_enc_s_a32;
}
static inline uint x86_enc_has_a64(uint enc) {
    return (enc & x86_enc_s_mask) == x86_enc_s_a64;
}

/*
 * operand encoding
 */

enum
{
    x86_opr_none,

    /* operand field shifts */
    x86_opr_ot               = 8,
    x86_opr_os               = x86_opr_ot + 4,
    x86_opr_ms               = x86_opr_os + 4,
    x86_opr_av               = x86_opr_ms + 4,
    x86_opr_et               = x86_opr_av + 4,
    x86_opr_ew               = x86_opr_et + 2,
    x86_opr_ec               = x86_opr_ew + 3,

    x86_opr_ot_mask          = (15 << x86_opr_ot), /* operand type */
    x86_opr_os_mask          = (15 << x86_opr_os), /* operand size */
    x86_opr_ms_mask          = (15 << x86_opr_ms), /* memory size */
    x86_opr_av_mask          = (15 << x86_opr_av), /* auxiliary value */
    x86_opr_et_mask          = (3 << x86_opr_et), /* element type */
    x86_opr_ew_mask          = (7 << x86_opr_ew), /* element width */
    x86_opr_ec_mask          = (7 << x86_opr_ec), /* element count */

    /* operand flags */
    x86_opr_mem              = (1 << 0),
    x86_opr_ind              = (1 << 1),
    x86_opr_bcst             = (1 << 2),
    x86_opr_flag_er          = (1 << 3),
    x86_opr_flag_k           = (1 << 4),
    x86_opr_flag_sae         = (1 << 5),
    x86_opr_flag_z           = (1 << 6),
    x86_opr_flag_rs          = (1 << 7),

    /* operand type */
    x86_opr_const            = (1 << x86_opr_ot), /* constant */
    x86_opr_imm              = (2 << x86_opr_ot), /* immediate */
    x86_opr_reg              = (3 << x86_opr_ot), /* register */
    x86_opr_vec              = (4 << x86_opr_ot), /* vector */
    x86_opr_mmx              = (5 << x86_opr_ot), /* mmx */
    x86_opr_st               = (6 << x86_opr_ot), /* x87 */
    x86_opr_k                = (7 << x86_opr_ot), /* mask */
    x86_opr_seg              = (8 << x86_opr_ot), /* segment */
    x86_opr_creg             = (9 << x86_opr_ot), /* control */
    x86_opr_dreg             = (10 << x86_opr_ot), /* debug */
    x86_opr_bnd              = (11 << x86_opr_ot), /* bound */
    x86_opr_type_mask        = x86_opr_ot_mask,

    /* operand sizes */
    x86_opr_size_8           = (1 << x86_opr_os),
    x86_opr_size_16          = (2 << x86_opr_os),
    x86_opr_size_32          = (3 << x86_opr_os),
    x86_opr_size_64          = (4 << x86_opr_os),
    x86_opr_size_128         = (5 << x86_opr_os),
    x86_opr_size_256         = (6 << x86_opr_os),
    x86_opr_size_512         = (7 << x86_opr_os),
    x86_opr_size_1024        = (8 << x86_opr_os),
    x86_opr_size_80          = (9 << x86_opr_os),
    x86_opr_size_wd          = (13 << x86_opr_os),
    x86_opr_size_w           = (14 << x86_opr_os),
    x86_opr_size_a           = (15 << x86_opr_os),
    x86_opr_size_mask        = x86_opr_os_mask,

    /* sized register */
    x86_opr_r8               = x86_opr_reg | x86_opr_size_8,
    x86_opr_r16              = x86_opr_reg | x86_opr_size_16,
    x86_opr_r32              = x86_opr_reg | x86_opr_size_32,
    x86_opr_r64              = x86_opr_reg | x86_opr_size_64,
    x86_opr_rw               = x86_opr_reg | x86_opr_size_w,
    x86_opr_ra               = x86_opr_reg | x86_opr_size_a,
    x86_opr_mm               = x86_opr_vec | x86_opr_size_64,
    x86_opr_xmm              = x86_opr_vec | x86_opr_size_128,
    x86_opr_ymm              = x86_opr_vec | x86_opr_size_256,
    x86_opr_zmm              = x86_opr_vec | x86_opr_size_512,

    /* sized memory */
    x86_opr_m8               = x86_opr_mem | (1 << x86_opr_ms), /* byte */
    x86_opr_m16              = x86_opr_mem | (2 << x86_opr_ms), /* word */
    x86_opr_m32              = x86_opr_mem | (3 << x86_opr_ms), /* dword */
    x86_opr_m64              = x86_opr_mem | (4 << x86_opr_ms), /* qword */
    x86_opr_m128             = x86_opr_mem | (5 << x86_opr_ms), /* oword/xmmword */
    x86_opr_m256             = x86_opr_mem | (6 << x86_opr_ms), /* ymmword */
    x86_opr_m512             = x86_opr_mem | (7 << x86_opr_ms), /* zmmword */
    x86_opr_m1024            = x86_opr_mem | (8 << x86_opr_ms),
    x86_opr_m80              = x86_opr_mem | (9 << x86_opr_ms), /* tword/tbyte */
    x86_opr_m384             = x86_opr_mem | (10 << x86_opr_ms),
    x86_opr_mib              = x86_opr_mem | (11 << x86_opr_ms), /* bound */
    x86_opr_vm32             = x86_opr_mem | (12 << x86_opr_ms), /* vecmem32 */
    x86_opr_vm64             = x86_opr_mem | (13 << x86_opr_ms), /* vecmem64 */
    x86_opr_mw               = x86_opr_mem | (14 << x86_opr_ms), /* 16/32/64 */
    x86_opr_mp               = x86_opr_mem | (15 << x86_opr_ms), /* pointer */
    x86_opr_mem_mask         = x86_opr_mem | (15 << x86_opr_ms),

    /* unsized register / sized memory */
    x86_opr_r_m8             = x86_opr_reg | x86_opr_m8,
    x86_opr_r_m16            = x86_opr_reg | x86_opr_m16,
    x86_opr_r_m32            = x86_opr_reg | x86_opr_m32,
    x86_opr_r_m64            = x86_opr_reg | x86_opr_m64,

    /* sized register / sized memory */
    x86_opr_r8_m8            = x86_opr_r8  | x86_opr_m8,
    x86_opr_r16_m8           = x86_opr_r16 | x86_opr_m8,
    x86_opr_r16_m16          = x86_opr_r16 | x86_opr_m16,
    x86_opr_r32_m8           = x86_opr_r32 | x86_opr_m8,
    x86_opr_r32_m16          = x86_opr_r32 | x86_opr_m16,
    x86_opr_r32_m32          = x86_opr_r32 | x86_opr_m32,
    x86_opr_r64_m8           = x86_opr_r64 | x86_opr_m8,
    x86_opr_r64_m16          = x86_opr_r64 | x86_opr_m16,
    x86_opr_r64_m32          = x86_opr_r64 | x86_opr_m32,
    x86_opr_r64_m64          = x86_opr_r64 | x86_opr_m64,
    x86_opr_rw_mw            = x86_opr_rw  | x86_opr_mw,

    /* sized vector / sized memory */
    x86_opr_mm_m32           = x86_opr_mm  | x86_opr_m32,
    x86_opr_mm_m64           = x86_opr_mm  | x86_opr_m64,
    x86_opr_xmm_m8           = x86_opr_xmm | x86_opr_m8,
    x86_opr_xmm_m16          = x86_opr_xmm | x86_opr_m16,
    x86_opr_xmm_m32          = x86_opr_xmm | x86_opr_m32,
    x86_opr_xmm_m64          = x86_opr_xmm | x86_opr_m64,
    x86_opr_xmm_m128         = x86_opr_xmm | x86_opr_m128,
    x86_opr_ymm_m256         = x86_opr_ymm | x86_opr_m256,
    x86_opr_zmm_m512         = x86_opr_zmm | x86_opr_m512,

    /* sized vector memory */
    x86_opr_vm32x            = x86_opr_xmm | x86_opr_vm32,
    x86_opr_vm32y            = x86_opr_ymm | x86_opr_vm32,
    x86_opr_vm32z            = x86_opr_zmm | x86_opr_vm32,
    x86_opr_vm64x            = x86_opr_xmm | x86_opr_vm64,
    x86_opr_vm64y            = x86_opr_ymm | x86_opr_vm64,
    x86_opr_vm64z            = x86_opr_zmm | x86_opr_vm64,

    /* mask / sized memory */
    x86_opr_k_m8             = x86_opr_k   | x86_opr_m8,
    x86_opr_k_m16            = x86_opr_k   | x86_opr_m16,
    x86_opr_k_m32            = x86_opr_k   | x86_opr_m32,
    x86_opr_k_m64            = x86_opr_k   | x86_opr_m64,

    /* bound / memory */
    x86_opr_bnd_mem          = x86_opr_bnd | x86_opr_mem,

    /* memory operand alias */
    x86_opr_m16int           = x86_opr_m16,
    x86_opr_m32fp            = x86_opr_m32,
    x86_opr_m32int           = x86_opr_m32,
    x86_opr_m64fp            = x86_opr_m64,
    x86_opr_m64int           = x86_opr_m64,
    x86_opr_m80bcd           = x86_opr_m80,
    x86_opr_m80dec           = x86_opr_m80,
    x86_opr_m80fp            = x86_opr_m80,

    /* sized immediate */
    x86_opr_ib               = x86_opr_imm | x86_opr_size_8,
    x86_opr_i16              = x86_opr_imm | x86_opr_size_16,
    x86_opr_i32              = x86_opr_imm | x86_opr_size_32,
    x86_opr_i64              = x86_opr_imm | x86_opr_size_64,
    x86_opr_iwd              = x86_opr_imm | x86_opr_size_wd,
    x86_opr_iw               = x86_opr_imm | x86_opr_size_w,

    /* memory offset */
    x86_opr_moffs            = x86_opr_imm | x86_opr_mem | x86_opr_size_w,

    /* constant */
    x86_opr_1                = x86_opr_const | (1 << x86_opr_av),

    /* registers sequential */
    x86_opr_flag_rs2         = x86_opr_flag_rs | (1 << x86_opr_av),
    x86_opr_flag_rs4         = x86_opr_flag_rs | (2 << x86_opr_av),

    /* sized broadcast */
    x86_opr_m16bcst          = x86_opr_bcst | (1 << x86_opr_av),
    x86_opr_m32bcst          = x86_opr_bcst | (2 << x86_opr_av),
    x86_opr_m64bcst          = x86_opr_bcst | (3 << x86_opr_av),
    x86_opr_bcst_mask        = x86_opr_bcst | x86_opr_av_mask,

    /* sized vector / sized memory / broadcast */
    x86_opr_xmm_m32_m16bcst  = x86_opr_mm_m32   | x86_opr_m16bcst,
    x86_opr_xmm_m64_m16bcst  = x86_opr_xmm_m64  | x86_opr_m16bcst,
    x86_opr_xmm_m64_m32bcst  = x86_opr_xmm_m64  | x86_opr_m32bcst,
    x86_opr_xmm_m128_m16bcst = x86_opr_xmm_m128 | x86_opr_m16bcst,
    x86_opr_xmm_m128_m32bcst = x86_opr_xmm_m128 | x86_opr_m32bcst,
    x86_opr_xmm_m128_m64bcst = x86_opr_xmm_m128 | x86_opr_m64bcst,
    x86_opr_ymm_m256_m16bcst = x86_opr_ymm_m256 | x86_opr_m16bcst,
    x86_opr_ymm_m256_m32bcst = x86_opr_ymm_m256 | x86_opr_m32bcst,
    x86_opr_ymm_m256_m64bcst = x86_opr_ymm_m256 | x86_opr_m64bcst,
    x86_opr_zmm_m512_m16bcst = x86_opr_zmm_m512 | x86_opr_m16bcst,
    x86_opr_zmm_m512_m32bcst = x86_opr_zmm_m512 | x86_opr_m32bcst,
    x86_opr_zmm_m512_m64bcst = x86_opr_zmm_m512 | x86_opr_m64bcst,

    /* relative displacement */
    x86_opr_rel8             = x86_opr_imm | x86_opr_ind | x86_opr_size_8,
    x86_opr_relw             = x86_opr_imm | x86_opr_ind | x86_opr_size_w,

    /* enter / far displacement */
    x86_opr_far16_16         = x86_opr_imm | x86_opr_ind | (1 << x86_opr_av),
    x86_opr_far16_32         = x86_opr_imm | x86_opr_ind | (2 << x86_opr_av),

    /* far memory indirect */
    x86_opr_memfar16_16      = x86_opr_mem | x86_opr_ind | (3 << x86_opr_av),
    x86_opr_memfar16_32      = x86_opr_mem | x86_opr_ind | (4 << x86_opr_av),
    x86_opr_memfar16_64      = x86_opr_mem | x86_opr_ind | (5 << x86_opr_av),

    /* implicit register */
    x86_opr_reg_a            = x86_opr_reg | x86_opr_ind | (1 << x86_opr_av),
    x86_opr_reg_c            = x86_opr_reg | x86_opr_ind | (2 << x86_opr_av),
    x86_opr_reg_d            = x86_opr_reg | x86_opr_ind | (3 << x86_opr_av),
    x86_opr_reg_b            = x86_opr_reg | x86_opr_ind | (4 << x86_opr_av),
    x86_opr_reg_si           = x86_opr_reg | x86_opr_ind | (5 << x86_opr_av),
    x86_opr_reg_di           = x86_opr_reg | x86_opr_ind | (6 << x86_opr_av),
    x86_opr_reg_ah           = x86_opr_reg | x86_opr_ind | (7 << x86_opr_av),
    x86_opr_reg_v0           = x86_opr_vec | x86_opr_ind | (1 << x86_opr_av),
    x86_opr_reg_st0          = x86_opr_st  | x86_opr_ind | (1 << x86_opr_av),
    x86_opr_seg_es           = x86_opr_seg | x86_opr_ind | (1 << x86_opr_av),
    x86_opr_seg_cs           = x86_opr_seg | x86_opr_ind | (2 << x86_opr_av),
    x86_opr_seg_ss           = x86_opr_seg | x86_opr_ind | (3 << x86_opr_av),
    x86_opr_seg_ds           = x86_opr_seg | x86_opr_ind | (4 << x86_opr_av),
    x86_opr_seg_fs           = x86_opr_seg | x86_opr_ind | (5 << x86_opr_av),
    x86_opr_seg_gs           = x86_opr_seg | x86_opr_ind | (6 << x86_opr_av),

    /* sized implicit register */
    x86_opr_reg_al           = x86_opr_reg_a | x86_opr_size_8,
    x86_opr_reg_cl           = x86_opr_reg_c | x86_opr_size_8,
    x86_opr_reg_ax           = x86_opr_reg_a | x86_opr_size_16,
    x86_opr_reg_cx           = x86_opr_reg_c | x86_opr_size_16,
    x86_opr_reg_dx           = x86_opr_reg_d | x86_opr_size_16,
    x86_opr_reg_bx           = x86_opr_reg_b | x86_opr_size_16,
    x86_opr_reg_eax          = x86_opr_reg_a | x86_opr_size_32,
    x86_opr_reg_ecx          = x86_opr_reg_c | x86_opr_size_32,
    x86_opr_reg_edx          = x86_opr_reg_d | x86_opr_size_32,
    x86_opr_reg_ebx          = x86_opr_reg_b | x86_opr_size_32,
    x86_opr_reg_rax          = x86_opr_reg_a | x86_opr_size_64,
    x86_opr_reg_rcx          = x86_opr_reg_c | x86_opr_size_64,
    x86_opr_reg_rdx          = x86_opr_reg_d | x86_opr_size_64,
    x86_opr_reg_rbx          = x86_opr_reg_b | x86_opr_size_64,
    x86_opr_reg_aw           = x86_opr_reg_a | x86_opr_size_w,
    x86_opr_reg_cw           = x86_opr_reg_c | x86_opr_size_w,
    x86_opr_reg_dw           = x86_opr_reg_d | x86_opr_size_w,
    x86_opr_reg_bw           = x86_opr_reg_b | x86_opr_size_w,
    x86_opr_reg_pa           = x86_opr_reg_a | x86_opr_size_a,
    x86_opr_reg_pc           = x86_opr_reg_c | x86_opr_size_a,
    x86_opr_reg_pd           = x86_opr_reg_d | x86_opr_size_a,
    x86_opr_reg_pb           = x86_opr_reg_b | x86_opr_size_a,
    x86_opr_reg_psi          = x86_opr_reg_si | x86_opr_size_a,
    x86_opr_reg_pdi          = x86_opr_reg_di | x86_opr_size_a,
    x86_opr_reg_xmm0         = x86_opr_reg_v0 | x86_opr_size_128,
    x86_opr_reg_xmm0_7       = x86_opr_reg_v0 | x86_opr_size_1024,

    /* element type */
    x86_opr_et_none          = (0 << x86_opr_et),
    x86_opr_et_i             = (1 << x86_opr_et),
    x86_opr_et_f             = (2 << x86_opr_et),
    x86_opr_et_bf            = (3 << x86_opr_et),

    /* element width */
    x86_opr_ew_8             = (1 << x86_opr_ew),
    x86_opr_ew_16            = (2 << x86_opr_ew),
    x86_opr_ew_32            = (3 << x86_opr_ew),
    x86_opr_ew_64            = (4 << x86_opr_ew),
    x86_opr_ew_128           = (5 << x86_opr_ew),
    x86_opr_ew_256           = (6 << x86_opr_ew),
    x86_opr_ew_512           = (7 << x86_opr_ew),

    /* element count */
    x86_opr_ec_x1            = (1 << x86_opr_ec),
    x86_opr_ec_x2            = (2 << x86_opr_ec),
    x86_opr_ec_x4            = (3 << x86_opr_ec),
    x86_opr_ec_x8            = (4 << x86_opr_ec),
    x86_opr_ec_x16           = (5 << x86_opr_ec),
    x86_opr_ec_x32           = (6 << x86_opr_ec),
    x86_opr_ec_x64           = (7 << x86_opr_ec),

    /* tuple type */
    x86_opr_i8x1             = x86_opr_et_i | x86_opr_ew_8 | x86_opr_ec_x1,
    x86_opr_i8x2             = x86_opr_et_i | x86_opr_ew_8 | x86_opr_ec_x2,
    x86_opr_i8x4             = x86_opr_et_i | x86_opr_ew_8 | x86_opr_ec_x4,
    x86_opr_i8x8             = x86_opr_et_i | x86_opr_ew_8 | x86_opr_ec_x8,
    x86_opr_i8x16            = x86_opr_et_i | x86_opr_ew_8 | x86_opr_ec_x16,
    x86_opr_i8x32            = x86_opr_et_i | x86_opr_ew_8 | x86_opr_ec_x32,
    x86_opr_i8x64            = x86_opr_et_i | x86_opr_ew_8 | x86_opr_ec_x64,
    x86_opr_i16x1            = x86_opr_et_i | x86_opr_ew_16 | x86_opr_ec_x1,
    x86_opr_i16x2            = x86_opr_et_i | x86_opr_ew_16 | x86_opr_ec_x2,
    x86_opr_i16x4            = x86_opr_et_i | x86_opr_ew_16 | x86_opr_ec_x4,
    x86_opr_i16x8            = x86_opr_et_i | x86_opr_ew_16 | x86_opr_ec_x8,
    x86_opr_i16x16           = x86_opr_et_i | x86_opr_ew_16 | x86_opr_ec_x16,
    x86_opr_i16x32           = x86_opr_et_i | x86_opr_ew_16 | x86_opr_ec_x32,
    x86_opr_i32x1            = x86_opr_et_i | x86_opr_ew_32 | x86_opr_ec_x1,
    x86_opr_i32x2            = x86_opr_et_i | x86_opr_ew_32 | x86_opr_ec_x2,
    x86_opr_i32x4            = x86_opr_et_i | x86_opr_ew_32 | x86_opr_ec_x4,
    x86_opr_i32x8            = x86_opr_et_i | x86_opr_ew_32 | x86_opr_ec_x8,
    x86_opr_i32x16           = x86_opr_et_i | x86_opr_ew_32 | x86_opr_ec_x16,
    x86_opr_i64x1            = x86_opr_et_i | x86_opr_ew_64 | x86_opr_ec_x1,
    x86_opr_i64x2            = x86_opr_et_i | x86_opr_ew_64 | x86_opr_ec_x2,
    x86_opr_i64x4            = x86_opr_et_i | x86_opr_ew_64 | x86_opr_ec_x4,
    x86_opr_i64x8            = x86_opr_et_i | x86_opr_ew_64 | x86_opr_ec_x8,
    x86_opr_i128x1           = x86_opr_et_i | x86_opr_ew_128 | x86_opr_ec_x1,
    x86_opr_i128x2           = x86_opr_et_i | x86_opr_ew_128 | x86_opr_ec_x2,
    x86_opr_i128x4           = x86_opr_et_i | x86_opr_ew_128 | x86_opr_ec_x4,
    x86_opr_i256x1           = x86_opr_et_i | x86_opr_ew_256 | x86_opr_ec_x1,
    x86_opr_i256x2           = x86_opr_et_i | x86_opr_ew_256 | x86_opr_ec_x2,
    x86_opr_i512x1           = x86_opr_et_i | x86_opr_ew_512 | x86_opr_ec_x1,
    x86_opr_f8x1             = x86_opr_et_f | x86_opr_ew_8 | x86_opr_ec_x1,
    x86_opr_f8x2             = x86_opr_et_f | x86_opr_ew_8 | x86_opr_ec_x2,
    x86_opr_f8x4             = x86_opr_et_f | x86_opr_ew_8 | x86_opr_ec_x4,
    x86_opr_f8x8             = x86_opr_et_f | x86_opr_ew_8 | x86_opr_ec_x8,
    x86_opr_f8x16            = x86_opr_et_f | x86_opr_ew_8 | x86_opr_ec_x16,
    x86_opr_f8x32            = x86_opr_et_f | x86_opr_ew_8 | x86_opr_ec_x32,
    x86_opr_f8x64            = x86_opr_et_f | x86_opr_ew_8 | x86_opr_ec_x64,
    x86_opr_f16x1            = x86_opr_et_f | x86_opr_ew_16 | x86_opr_ec_x1,
    x86_opr_f16x2            = x86_opr_et_f | x86_opr_ew_16 | x86_opr_ec_x2,
    x86_opr_f16x4            = x86_opr_et_f | x86_opr_ew_16 | x86_opr_ec_x4,
    x86_opr_f16x8            = x86_opr_et_f | x86_opr_ew_16 | x86_opr_ec_x8,
    x86_opr_f16x16           = x86_opr_et_f | x86_opr_ew_16 | x86_opr_ec_x16,
    x86_opr_f16x32           = x86_opr_et_f | x86_opr_ew_16 | x86_opr_ec_x32,
    x86_opr_f32x1            = x86_opr_et_f | x86_opr_ew_32 | x86_opr_ec_x1,
    x86_opr_f32x2            = x86_opr_et_f | x86_opr_ew_32 | x86_opr_ec_x2,
    x86_opr_f32x4            = x86_opr_et_f | x86_opr_ew_32 | x86_opr_ec_x4,
    x86_opr_f32x8            = x86_opr_et_f | x86_opr_ew_32 | x86_opr_ec_x8,
    x86_opr_f32x16           = x86_opr_et_f | x86_opr_ew_32 | x86_opr_ec_x16,
    x86_opr_f64x1            = x86_opr_et_f | x86_opr_ew_64 | x86_opr_ec_x1,
    x86_opr_f64x2            = x86_opr_et_f | x86_opr_ew_64 | x86_opr_ec_x2,
    x86_opr_f64x4            = x86_opr_et_f | x86_opr_ew_64 | x86_opr_ec_x4,
    x86_opr_f64x8            = x86_opr_et_f | x86_opr_ew_64 | x86_opr_ec_x8,
};

/*
 * operand encoding accessors
 */

static inline uint x86_opr_has_mem(uint opr) {
    return (opr & x86_opr_mem) != 0;
}
static inline uint x86_opr_type_val(uint opr) {
    return opr & x86_opr_type_mask;
}
static inline uint x86_opr_size_val(uint opr) {
    return opr & x86_opr_size_mask;
}
static inline uint x86_opr_mem_val(uint opr) {
    return opr & x86_opr_mem_mask;
}
static inline uint x86_opr_bcst_val(uint opr) {
    return opr & x86_opr_bcst_mask;
}
static inline uint x86_opr_et_val(uint opr) {
    return opr & x86_opr_et_mask;
}
static inline uint x86_opr_ec_val(uint opr) {
    return opr & x86_opr_ec_mask;
}
static inline uint x86_opr_ew_val(uint opr) {
    return opr & x86_opr_ew_mask;
}

static inline uint x86_opr_mem_size(uint opr)
{
    switch (x86_opr_mem_val(opr)) {
    case x86_opr_m8: return x86_opr_size_8;
    case x86_opr_m16: return x86_opr_size_16;
    case x86_opr_m32: return x86_opr_size_32;
    case x86_opr_m64: return x86_opr_size_64;
    case x86_opr_m80: return x86_opr_size_80;
    case x86_opr_m128: return x86_opr_size_128;
    case x86_opr_m256: return x86_opr_size_256;
    case x86_opr_m512: return x86_opr_size_512;
    case x86_opr_mw:
    default: return x86_opr_size_w;
    }
}

static inline uint x86_opr_ew_bytes(uint opr)
{
    switch (x86_opr_ew_val(opr)) {
    case x86_opr_ew_8: return 1;
    case x86_opr_ew_16: return 2;
    case x86_opr_ew_32: return 4;
    case x86_opr_ew_64: return 8;
    case x86_opr_ew_128: return 16;
    case x86_opr_ew_256: return 32;
    case x86_opr_ew_512: return 64;
    }
    return 0;
}

static inline uint x86_opr_ew_size(uint opr)
{
    switch (x86_opr_ew_val(opr)) {
    case x86_opr_ew_8: return x86_opr_size_8;
    case x86_opr_ew_16: return x86_opr_size_16;
    case x86_opr_ew_32: return x86_opr_size_32;
    case x86_opr_ew_64: return x86_opr_size_64;
    case x86_opr_ew_128: return x86_opr_size_128;
    case x86_opr_ew_256: return x86_opr_size_256;
    case x86_opr_ew_512: return x86_opr_size_512;
    }
    return 0;
}

static inline uint x86_opr_ec_mult(uint opr)
{
    switch (x86_opr_ec_val(opr)) {
    case x86_opr_ec_x1: return 1;
    case x86_opr_ec_x2: return 2;
    case x86_opr_ec_x4: return 4;
    case x86_opr_ec_x8: return 8;
    case x86_opr_ec_x16: return 16;
    case x86_opr_ec_x32: return 32;
    case x86_opr_ec_x64: return 64;
    }
    return 0;
}

/*
 * order encoding
 */

enum
{
    x86_ord_none,

    x86_ord_s1               = 0,
    x86_ord_s2               = x86_ord_s1 + 3,
    x86_ord_s3               = x86_ord_s2 + 3,

    x86_ord_const            = (1 << x86_ord_s1), /* - */
    x86_ord_imm              = (2 << x86_ord_s1), /* I */
    x86_ord_reg              = (3 << x86_ord_s1), /* R */
    x86_ord_mrm              = (4 << x86_ord_s1), /* M */
    x86_ord_vec              = (5 << x86_ord_s1), /* V */
    x86_ord_opr              = (6 << x86_ord_s1), /* O */
    x86_ord_type_mask        = (7 << x86_ord_s1),

    x86_ord_r                = (1 << x86_ord_s2),
    x86_ord_w                = (2 << x86_ord_s2),
    x86_ord_rw               = (3 << x86_ord_s2),
    x86_ord_i                = (4 << x86_ord_s2),
    x86_ord_ri               = (5 << x86_ord_s2),
    x86_ord_wi               = (6 << x86_ord_s2),
    x86_ord_rwi              = (7 << x86_ord_s2),
    x86_ord_flag_mask        = (7 << x86_ord_s2),

    x86_ord_one              = x86_ord_const | (1 << x86_ord_s3),
    x86_ord_rax              = x86_ord_const | (2 << x86_ord_s3),
    x86_ord_rcx              = x86_ord_const | (3 << x86_ord_s3),
    x86_ord_rdx              = x86_ord_const | (4 << x86_ord_s3),
    x86_ord_rbx              = x86_ord_const | (5 << x86_ord_s3),
    x86_ord_rsp              = x86_ord_const | (6 << x86_ord_s3),
    x86_ord_rbp              = x86_ord_const | (7 << x86_ord_s3),
    x86_ord_rsi              = x86_ord_const | (8 << x86_ord_s3),
    x86_ord_rdi              = x86_ord_const | (9 << x86_ord_s3),
    x86_ord_st0              = x86_ord_const | (10 << x86_ord_s3),
    x86_ord_stx              = x86_ord_const | (11 << x86_ord_s3),
    x86_ord_seg              = x86_ord_const | (12 << x86_ord_s3),
    x86_ord_xmm0             = x86_ord_const | (13 << x86_ord_s3),
    x86_ord_xmm0_7           = x86_ord_const | (14 << x86_ord_s3),
    x86_ord_mxcsr            = x86_ord_const | (15 << x86_ord_s3),
    x86_ord_rflags           = x86_ord_const | (16 << x86_ord_s3),

    x86_ord_sib              = x86_ord_mrm | (1 << x86_ord_s3),

    x86_ord_is4              = x86_ord_imm | (1 << x86_ord_s3),
    x86_ord_ime              = x86_ord_imm | (2 << x86_ord_s3),
};

/*
 * order encoding accessors
 */

static inline uint x86_ord_type_val(uint ord) {
    return ord & x86_ord_type_mask;
}

/*
 * codec flags
 */

enum
{
    x86_ce_shift   = 0,
    x86_cm_shift   = 3,
    x86_ci_shift   = 5,
    x86_cj_shift   = 8,

    /* [0:2] encoding */
    x86_ce_none    = (0 << x86_ce_shift),
    x86_ce_rex     = (1 << x86_ce_shift),
    x86_ce_rex2    = (2 << x86_ce_shift),
    x86_ce_vex2    = (3 << x86_ce_shift),
    x86_ce_vex3    = (4 << x86_ce_shift),
    x86_ce_evex    = (5 << x86_ce_shift),
    x86_ce_mask    = (7 << x86_ce_shift),

    /* [3:4] map */
    x86_cm_none    = (0 << x86_cm_shift),
    x86_cm_0f      = (1 << x86_cm_shift),
    x86_cm_0f38    = (2 << x86_cm_shift),
    x86_cm_0f3a    = (3 << x86_cm_shift),
    x86_cm_mask    = (3 << x86_cm_shift),

    /* [5:7] imm */
    x86_ci_none    = (0 << x86_ci_shift),
    x86_ci_ib      = (1 << x86_ci_shift),
    x86_ci_iw      = (2 << x86_ci_shift),
    x86_ci_iwd     = (3 << x86_ci_shift),
    x86_ci_i16     = (4 << x86_ci_shift),
    x86_ci_i32     = (5 << x86_ci_shift),
    x86_ci_i64     = (6 << x86_ci_shift),
    x86_ci_mask    = (7 << x86_ci_shift),

    /* [8:9] imm2 */
    x86_cj_ib      = (1 << x86_cj_shift),
    x86_cj_i16     = (2 << x86_cj_shift),
    x86_cj_mask    = (3 << x86_cj_shift),

    /* [10:15] prefixes */
    x86_cp_osize   = (1 << 10), /* 0x66 */
    x86_cp_asize   = (1 << 11), /* 0x67 */
    x86_cp_wait    = (1 << 12), /* 0x9B */
    x86_cp_lock    = (1 << 13), /* 0xF0 */
    x86_cp_repne   = (1 << 14), /* 0xF2 */
    x86_cp_rep     = (1 << 15), /* 0xF3 */

    /* [16:18] flags */
    x86_cf_modrm   = (1 << 16),
    x86_cf_ia32    = (1 << 17),
    x86_cf_amd64   = (1 << 18),
};

/*
 * codec struct
 */

struct x86_codec
{
    union {
        x86_rex rex;
        x86_rex2 rex2;
        x86_vex2 vex2;
        x86_vex3 vex3;
        x86_evex evex;
    };

    union {
        uchar opc[2];
        struct {
            uchar opc0;
            x86_modrm modrm;
        };
    };

    uchar opclen : 2;
    uchar seg : 3;
    uchar spare;
    x86_sib sib;

    uint flags;
    ushort rec;
    short imm2;

    union {
        struct {
            int disp32;
            int imm32;
        };
        struct {
            ullong imm64;
        };
    };
};

/*
 * codec accessors
 */

static inline int x86_codec_field_ce(x86_codec *c) {
    return c->flags & x86_ce_mask;
}
static inline int x86_codec_field_cm(x86_codec *c) {
    return c->flags & x86_cm_mask;
}
static inline int x86_codec_field_ci(x86_codec *c) {
    return c->flags & x86_ci_mask;
}
static inline int x86_codec_field_cj(x86_codec *c) {
    return c->flags & x86_cj_mask;
}
static inline int x86_codec_has_wait(x86_codec *c) {
    return (c->flags & x86_cp_wait) != 0;
}
static inline int x86_codec_has_lock(x86_codec *c) {
    return (c->flags & x86_cp_lock) != 0;
}
static inline int x86_codec_has_rep(x86_codec *c) {
    return (c->flags & x86_cp_rep) != 0;
}
static inline int x86_codec_has_repne(x86_codec *c) {
    return (c->flags & x86_cp_repne) != 0;
}
static inline int x86_codec_has_osize(x86_codec *c) {
    return (c->flags & x86_cp_osize) != 0;
}
static inline int x86_codec_has_asize(x86_codec *c) {
    return (c->flags & x86_cp_asize) != 0;
}
static inline int x86_codec_has_modrm(x86_codec *c) {
    return (c->flags & x86_cf_modrm) != 0;
}
static inline int x86_codec_is16(x86_codec *c) {
    return (c->flags & (x86_cf_ia32 | x86_cf_amd64)) == 0;
}
static inline int x86_codec_is32(x86_codec *c) {
    return (c->flags & x86_cf_ia32) != 0;
}
static inline int x86_codec_is64(x86_codec *c) {
    return (c->flags & x86_cf_amd64) != 0;
}

/*
 * modes
 */

enum
{
    x86_modes_16 = (1 << 0),
    x86_modes_32 = (1 << 1),
    x86_modes_64 = (1 << 2),
};

/*
 * modes accessors
 */

static inline int x86_mode_has16(uint mode) {
    return (mode & x86_modes_16) != 0;
}
static inline int x86_mode_has32(uint mode) {
    return (mode & x86_modes_32) != 0;
}
static inline int x86_mode_has64(uint mode) {
    return (mode & x86_modes_64) != 0;
}

/*
 * memory operand
 */

struct x86_mem
{
    union {
        uint code;
        struct {
            uint base : 9;
            uint index : 9;
            uint scale : 2;
        };
    };
    int disp;
};

/*
 * opcode metadata
 *
 * type, prefix, map, opcode, mask, plus operand and order records.
 *
 * opcode - opcode number from opcode enum for name lookup
 * mode   - operating mode (16 | 32 | 64)
 * opr    - operand list (r8/m8, rw/mw, xmm/m128, etc)
 * ord    - operand order (register, immediate, regmem, etc)
 * enc    - operand encoding (type, width, prefix, map, immediate, etc)
 * opc    - opcode, ModRM function byte or second opcode byte.
 * opm    - opcode mask (f8 for XX+r), ModRM function or second byte mask.
 *
 * prefix and map are provisioned as 6 bits each to align the bitfield.
 * there are 3 types, 10 prefixes (5 * 2), and 7 maps (up to map6).
 */

struct x86_opc_data
{
    ushort op;
    ushort mode;
    ushort opr;
    ushort ord;
    uint enc;
    union {
        uchar opc[2];
        ushort opc_s;
    };
    union {
        uchar opm[2];
        ushort opm_s;
    };
};

struct x86_opr_data
{
    uint opr[4];
};

struct x86_ord_data
{
    ushort ord[4];
};

/*
 * ModRM encoder
 */

static inline x86_modrm x86_enc_modrm(uint mod, uint reg, uint rm)
{
    x86_modrm modrm = {
        .data = {
            /* [0:2] */ (rm & 7u) |
            /* [3:5] */ ((reg & 7u) << 3) |
            /* [6:7] */ ((mod & 3u) << 6)
        }
    };
    return modrm;
}

/*
 * SIB encoder
 */

static inline x86_sib x86_enc_sib(uint s, uint x, uint b)
{
    x86_sib sib = {
        .data = {
            /* [0:2] */ (b & 7u) |
            /* [3:5] */ ((x & 7u) << 3) |
            /* [6:7] */ (((uint)s & 3u) << 6)
        }
    };
    return sib;
}

/*
 * REX encoder
 */

static inline x86_rex x86_enc_rex(uint w, uint r, uint x, uint b)
{
    x86_rex rex = {
        .data = {
            /*   [0] */ ((b & 8u) >> 3) |
            /*   [1] */ ((x & 8u) >> 2) |
            /*   [2] */ ((r & 8u) >> 1) |
            /*   [3] */ ((w & 1u) << 3) |
            /* [4:7] */ 0x40
        }
    };
    return rex;
}

/*
 * REX2 encoder
 */

static inline x86_rex2 x86_enc_rex2(uint m, uint w, uint r, uint x, uint b)
{
    x86_rex2 rex2 = {
        .data = {
            /*   [0] */ ((b &  8u) >> 3) |
            /*   [1] */ ((x &  8u) >> 2) |
            /*   [2] */ ((r &  8u) >> 1) |
            /*   [3] */ ((w &  1u) << 3) |
            /*   [4] */ ((b & 16u) << 0) |
            /*   [5] */ ((x & 16u) << 1) |
            /*   [6] */ ((r & 16u) << 2) |
            /*   [7] */ ((m &  1u) << 7)
        }
    };
    return rex2;
}

/*
 * VEX2 encoder
 */

static inline x86_vex2 x86_enc_vex2(uint p, uint l, uint r, uint v)
{
    x86_vex2 vex2 = {
        .data = {
            /* [0:1] */ ((uint)p & 3u) |
            /*   [2] */ ((l & 1u) << 2) |
            /* [3:6] */ ((~v & 15u) << 3) |
            /*   [7] */ ((~r & 8u) << 4)
        }
    };
    return vex2;
}

/*
 * VEX3 encoder
 */

static inline x86_vex3 x86_enc_vex3(uint m, uint p,
    uint l, uint w, uint r, uint x, uint b, uint v)
{
    x86_vex3 vex3 = {
        .data = {
            /* [0:4] */ ((m &  31u) >> 0) |
            /*   [5] */ ((~b &  8u) << 2) |
            /*   [6] */ ((~x &  8u) << 3) |
            /*   [7] */ ((~r &  8u) << 4),
            /* [0:1] */ ((p &   3u) >> 0) |
            /*   [2] */ ((l  &  1u) << 2) |
            /* [3:6] */ ((~v & 15u) << 3) |
            /*   [7] */ ((w  &  1u) << 7)
        }
    };
    return vex3;
}

/*
 * EVEX encoder
 */

static inline x86_evex x86_enc_evex(uint m, uint p,
    uint l, uint w, uint r, uint x, uint b, uint v,
    uint k, uint brd, uint z)
{
    x86_evex evex = {
        .data = {
            /* [0:2] */ ((m &   7u) >> 0) |
            /*   [3] */ ((b &  16u) >> 1) |
            /*   [4] */ ((~r & 16u) >> 0) |
            /*   [5] */ ((~b &  8u) << 2) |
            /*   [6] */ ((~x &  8u) << 3) |
            /*   [7] */ ((~r &  8u) << 4),
            /* [0:1] */ ((p &   3u) >> 0) |
            /*   [2] */ ((~x & 16u) >> 2) |
            /* [3:6] */ ((~v & 15u) << 3) |
            /*   [7] */ ((w  &  1u) << 7),
            /* [0:2] */ ((k &   7u) >> 0) |
            /*   [3] */ ((~v & 16u) >> 1) |
            /*   [4] */ ((brd & 1u) << 4) |
            /* [5:6] */ ((l &   3u) << 5) |
            /*   [7] */ ((z &   1u) << 7)
        }
    };
    return evex;
}

/*
 * table sort types
 */

enum
{
    x86_sort_none,
    x86_sort_numeric,
    x86_sort_alpha
};

/*
 * table encoding prefix types
 */

enum
{
    x86_table_none,
    x86_table_lex,
    x86_table_vex,
    x86_table_evex
};

/*
 * table sort indices
 */

struct x86_table_idx
{
    size_t count;
    size_t *idx;
};

/*
 * opcode index decode tables
 */

struct x86_acc_idx
{
    size_t map_count;
    x86_opc_data *map;
    size_t acc_count;
    x86_acc_entry *acc;
    uchar *page_offsets;
};

/*
 * opcode index acceleration entry
 */

struct x86_acc_entry
{
    uint idx : 24;
    uint nent : 8;
};

/*
 * opcode acceleration functions
 */

static inline uint x86_acc_page(uint type, uint prefix, uint map)
{
    return (type & 3) | ((prefix & 15) << 2) | ((map & 7) << 6);
}

static inline size_t x86_acc_offset(x86_acc_idx *idx, size_t acc_page)
{
    return (size_t)idx->page_offsets[acc_page] << 8;
}

static inline x86_acc_entry *x86_acc_lookup(x86_acc_idx *idx, size_t offset)
{
    return idx->acc + offset;
}

/*
 * bitmap utility functions
 */

static inline size_t x86_bitmap_idx(size_t i) { return i >> 6; }
static inline size_t x86_bitmap_shift(size_t i) { return (i & 63); }

static inline int x86_bitmap_get(ullong *bitmap, size_t i)
{
    return (int)((bitmap[x86_bitmap_idx(i)] >> x86_bitmap_shift(i)) & 1);
}

static inline void x86_bitmap_set(ullong *bitmap, size_t i, int value)
{
    bitmap[x86_bitmap_idx(i)] |= ((ullong)value << x86_bitmap_shift(i));
}

/*
 * context for encoder, decoder, formatter and parser
 */

struct x86_ctx
{
    uint mode;
    x86_acc_idx *idx;
};

/*
 * buffer
 *
 * simplified buffer with read (start) and write (end) cursors
 * capacity is user managed because it does no limit checking.
 */

struct x86_buffer
{
    uchar *data;
    size_t start;
    size_t end;
};

/*
 * buffer functions
 */

static inline void x86_buffer_init(x86_buffer *b, uchar *data)
{
    b->start = 0;
    b->end = 0;
    b->data = data;
}

static inline void x86_buffer_init_ex(x86_buffer *b, uchar *data,
    size_t start, size_t end)
{
    b->start = start;
    b->end = end;
    b->data = data;
}

static inline size_t x86_buffer_read(x86_buffer *b, uchar *buf, size_t len)
{
    memcpy(buf, b->data + b->start, len);
    b->start += len;
    return len;
}

static inline size_t x86_buffer_unread(x86_buffer *b, size_t len)
{
    b->start -= len;
    return len;
}

static inline size_t x86_buffer_write(x86_buffer *b, uchar *buf, size_t len)
{
    memcpy(b->data + b->end, buf, len);
    b->end += len;
    return len;
}

static inline size_t x86_buffer_unwrite(x86_buffer *b, size_t len)
{
    b->end -= len;
    return len;
}

static inline size_t x86_out8(x86_buffer *buf, u8 v)
{
    return x86_buffer_write(buf, (void *)&v, sizeof(u8));
}

static inline size_t x86_out16(x86_buffer *buf, u16 v)
{
    u16 t = cpu_to_le16(v);
    return x86_buffer_write(buf, (void *)&t, sizeof(u16));
}

static inline size_t x86_out32(x86_buffer *buf, u32 v)
{
    u32 t = cpu_to_le32(v);
    return x86_buffer_write(buf, (void *)&t, sizeof(u32));
}

static inline size_t x86_out64(x86_buffer *buf, u64 v)
{
    u64 t = cpu_to_le64(v);
    return x86_buffer_write(buf, (void *)&t, sizeof(u64));
}

static inline size_t x86_unput(x86_buffer *buf, size_t n)
{
    return x86_buffer_unwrite(buf, n);
}

static inline u8 x86_in8(x86_buffer *buf)
{
    u8 t = 0;
    x86_buffer_read(buf, (void *)&t, sizeof(u8));
    return t;
}

static inline u16 x86_in16(x86_buffer *buf)
{
    u16 t = 0;
    x86_buffer_read(buf, (void *)&t, sizeof(u16));
    return le16_to_cpu(t);
}

static inline u32 x86_in32(x86_buffer *buf)
{
    u32 t = 0;
    x86_buffer_read(buf, (void *)&t, sizeof(u32));
    return le32_to_cpu(t);
}

static inline u64 x86_in64(x86_buffer *buf)
{
    u64 t = 0;
    x86_buffer_read(buf, (void *)&t, sizeof(u64));
    return le64_to_cpu(t);
}

/*
 * metadata tables
 */

extern const char *x86_reg_names[];
extern const size_t x86_op_names_size;
extern const char *x86_op_names[];
extern const size_t x86_opc_table_size;
extern const x86_opc_data x86_opc_table[];
extern const size_t x86_opr_table_size;
extern const x86_opr_data x86_opr_table[];
extern const size_t x86_ord_table_size;
extern const x86_ord_data x86_ord_table[];

/*
 * encoder, decoder, table lookup, disassembly
 */

void x86_set_debug(uint d);
size_t x86_mode_name(char *buf, size_t len, uint mode, const char *sep);
size_t x86_map_name(char *buf, size_t len, uint mode, const char *sep);
size_t x86_ord_name(char *buf, size_t len, uint ord, const char *sep);
size_t x86_ord_mnem(char *buf, size_t len, const ushort *ord);
size_t x86_opr_name(char *buf, size_t len, uint opr);
size_t x86_enc_name(char *buf, size_t len, uint enc);
const char *x86_reg_name(uint reg);
const char *x86_table_type_name(uint type);
const char *x86_table_map_name(uint map);
const char *x86_table_prefix_name(uint prefix);
int x86_enc_filter_rex(x86_rex prefix, uint enc);
int x86_enc_filter_rex2(x86_rex2 prefix, uint enc);
int x86_enc_filter_vex2(x86_vex2 prefix, uint enc);
int x86_enc_filter_vex3(x86_vex3 prefix, uint enc);
int x86_enc_filter_evex(x86_evex prefix, uint enc);
x86_table_idx x86_opc_table_identity(void);
x86_table_idx x86_opc_table_sorted(x86_table_idx tab, uint sort);
x86_table_idx x86_opc_table_filter(x86_table_idx tab, uint modes);
x86_opc_data *x86_table_lookup(x86_acc_idx *idx, const x86_opc_data *m);
void x86_print_op(const x86_opc_data *d, uint compact, uint opcode);
size_t x86_format_op(char *buf, size_t len, x86_ctx *ctx, x86_codec *c);
typedef size_t (*x86_fmt_symbol)(char *buf, size_t buflen, x86_codec *c,
    size_t pc_offset);
size_t x86_format_op_symbol(char *buf, size_t buflen, x86_ctx *ctx,
    x86_codec *c, size_t pc_offset, x86_fmt_symbol sym_cb);
size_t x86_format_hex(char *buf, size_t len, uchar *data, size_t datalen);
x86_ctx *x86_ctx_create(uint mode);
void x86_ctx_destroy(x86_ctx *ctx);
int x86_codec_write(x86_ctx *ctx, x86_buffer *buf, x86_codec c, size_t *len);
int x86_codec_read(x86_ctx *ctx, x86_buffer *buf, x86_codec *c, size_t *len);

/*
 * registers sand opcodes
 */

#include "disas/x86-enums.inc"
