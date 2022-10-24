/*
 * Loongson EXT and MMI translation routines.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This code is licensed under the LGPL v2.1 or later.
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/helper-gen.h"
#include "translate.h"

enum {
    OPC_CP2      = (0x12 << 26),
    OPC_SPECIAL2 = (0x1C << 26),
    OPC_SPECIAL3 = (0x1F << 26),
    OPC_LWC2     = (0x32 << 26),
    OPC_LDC2     = (0x36 << 26),
    OPC_SWC2     = (0x3A << 26),
    OPC_SDC2     = (0x3E << 26),
};

/* Special2 opcodes */
#define MASK_2F_SPECIAL2(op)    (MASK_OP_MAJOR(op) | (op & 0x3F))

enum {
    OPC_MULT_G_2F   = 0x10 | OPC_SPECIAL2,
    OPC_DMULT_G_2F  = 0x11 | OPC_SPECIAL2,
    OPC_MULTU_G_2F  = 0x12 | OPC_SPECIAL2,
    OPC_DMULTU_G_2F = 0x13 | OPC_SPECIAL2,
    OPC_DIV_G_2F    = 0x14 | OPC_SPECIAL2,
    OPC_DDIV_G_2F   = 0x15 | OPC_SPECIAL2,
    OPC_DIVU_G_2F   = 0x16 | OPC_SPECIAL2,
    OPC_DDIVU_G_2F  = 0x17 | OPC_SPECIAL2,
    OPC_MOD_G_2F    = 0x1c | OPC_SPECIAL2,
    OPC_DMOD_G_2F   = 0x1d | OPC_SPECIAL2,
    OPC_MODU_G_2F   = 0x1e | OPC_SPECIAL2,
    OPC_DMODU_G_2F  = 0x1f | OPC_SPECIAL2,
};

/* Special3 opcodes */
#define MASK_2E_SPECIAL3(op)    (MASK_OP_MAJOR(op) | (op & 0x3F))
enum {
    /* Loongson 2E */
    OPC_MULT_G_2E   = 0x18 | OPC_SPECIAL3,
    OPC_MULTU_G_2E  = 0x19 | OPC_SPECIAL3,
    OPC_DIV_G_2E    = 0x1A | OPC_SPECIAL3,
    OPC_DIVU_G_2E   = 0x1B | OPC_SPECIAL3,
    OPC_DMULT_G_2E  = 0x1C | OPC_SPECIAL3,
    OPC_DMULTU_G_2E = 0x1D | OPC_SPECIAL3,
    OPC_DDIV_G_2E   = 0x1E | OPC_SPECIAL3,
    OPC_DDIVU_G_2E  = 0x1F | OPC_SPECIAL3,
    OPC_MOD_G_2E    = 0x22 | OPC_SPECIAL3,
    OPC_MODU_G_2E   = 0x23 | OPC_SPECIAL3,
    OPC_DMOD_G_2E   = 0x26 | OPC_SPECIAL3,
    OPC_DMODU_G_2E  = 0x27 | OPC_SPECIAL3,
};

/* Loongson EXT load/store quad word opcodes */
#define MASK_LOONGSON_GSLSQ(op)           (MASK_OP_MAJOR(op) | (op & 0x8020))
enum {
    OPC_GSLQ        = 0x0020 | OPC_LWC2,
    OPC_GSLQC1      = 0x8020 | OPC_LWC2,
    OPC_GSSHFL      = OPC_LWC2,
    OPC_GSSQ        = 0x0020 | OPC_SWC2,
    OPC_GSSQC1      = 0x8020 | OPC_SWC2,
    OPC_GSSHFS      = OPC_SWC2,
};

/* Loongson EXT shifted load/store opcodes */
#define MASK_LOONGSON_GSSHFLS(op)         (MASK_OP_MAJOR(op) | (op & 0xc03f))
enum {
    OPC_GSLWLC1     = 0x4 | OPC_GSSHFL,
    OPC_GSLWRC1     = 0x5 | OPC_GSSHFL,
    OPC_GSLDLC1     = 0x6 | OPC_GSSHFL,
    OPC_GSLDRC1     = 0x7 | OPC_GSSHFL,
    OPC_GSSWLC1     = 0x4 | OPC_GSSHFS,
    OPC_GSSWRC1     = 0x5 | OPC_GSSHFS,
    OPC_GSSDLC1     = 0x6 | OPC_GSSHFS,
    OPC_GSSDRC1     = 0x7 | OPC_GSSHFS,
};

/* Loongson EXT LDC2/SDC2 opcodes */
#define MASK_LOONGSON_LSDC2(op)           (MASK_OP_MAJOR(op) | (op & 0x7))

enum {
    OPC_GSLBX      = 0x0 | OPC_LDC2,
    OPC_GSLHX      = 0x1 | OPC_LDC2,
    OPC_GSLWX      = 0x2 | OPC_LDC2,
    OPC_GSLDX      = 0x3 | OPC_LDC2,
    OPC_GSLWXC1    = 0x6 | OPC_LDC2,
    OPC_GSLDXC1    = 0x7 | OPC_LDC2,
    OPC_GSSBX      = 0x0 | OPC_SDC2,
    OPC_GSSHX      = 0x1 | OPC_SDC2,
    OPC_GSSWX      = 0x2 | OPC_SDC2,
    OPC_GSSDX      = 0x3 | OPC_SDC2,
    OPC_GSSWXC1    = 0x6 | OPC_SDC2,
    OPC_GSSDXC1    = 0x7 | OPC_SDC2,
};

#define MASK_LMMI(op)    (MASK_OP_MAJOR(op) | (op & (0x1F << 21)) | (op & 0x1F))

enum {
    OPC_PADDSH      = (24 << 21) | (0x00) | OPC_CP2,
    OPC_PADDUSH     = (25 << 21) | (0x00) | OPC_CP2,
    OPC_PADDH       = (26 << 21) | (0x00) | OPC_CP2,
    OPC_PADDW       = (27 << 21) | (0x00) | OPC_CP2,
    OPC_PADDSB      = (28 << 21) | (0x00) | OPC_CP2,
    OPC_PADDUSB     = (29 << 21) | (0x00) | OPC_CP2,
    OPC_PADDB       = (30 << 21) | (0x00) | OPC_CP2,
    OPC_PADDD       = (31 << 21) | (0x00) | OPC_CP2,

    OPC_PSUBSH      = (24 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBUSH     = (25 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBH       = (26 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBW       = (27 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBSB      = (28 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBUSB     = (29 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBB       = (30 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBD       = (31 << 21) | (0x01) | OPC_CP2,

    OPC_PSHUFH      = (24 << 21) | (0x02) | OPC_CP2,
    OPC_PACKSSWH    = (25 << 21) | (0x02) | OPC_CP2,
    OPC_PACKSSHB    = (26 << 21) | (0x02) | OPC_CP2,
    OPC_PACKUSHB    = (27 << 21) | (0x02) | OPC_CP2,
    OPC_XOR_CP2     = (28 << 21) | (0x02) | OPC_CP2,
    OPC_NOR_CP2     = (29 << 21) | (0x02) | OPC_CP2,
    OPC_AND_CP2     = (30 << 21) | (0x02) | OPC_CP2,
    OPC_PANDN       = (31 << 21) | (0x02) | OPC_CP2,

    OPC_PUNPCKLHW   = (24 << 21) | (0x03) | OPC_CP2,
    OPC_PUNPCKHHW   = (25 << 21) | (0x03) | OPC_CP2,
    OPC_PUNPCKLBH   = (26 << 21) | (0x03) | OPC_CP2,
    OPC_PUNPCKHBH   = (27 << 21) | (0x03) | OPC_CP2,
    OPC_PINSRH_0    = (28 << 21) | (0x03) | OPC_CP2,
    OPC_PINSRH_1    = (29 << 21) | (0x03) | OPC_CP2,
    OPC_PINSRH_2    = (30 << 21) | (0x03) | OPC_CP2,
    OPC_PINSRH_3    = (31 << 21) | (0x03) | OPC_CP2,

    OPC_PAVGH       = (24 << 21) | (0x08) | OPC_CP2,
    OPC_PAVGB       = (25 << 21) | (0x08) | OPC_CP2,
    OPC_PMAXSH      = (26 << 21) | (0x08) | OPC_CP2,
    OPC_PMINSH      = (27 << 21) | (0x08) | OPC_CP2,
    OPC_PMAXUB      = (28 << 21) | (0x08) | OPC_CP2,
    OPC_PMINUB      = (29 << 21) | (0x08) | OPC_CP2,

    OPC_PCMPEQW     = (24 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPGTW     = (25 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPEQH     = (26 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPGTH     = (27 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPEQB     = (28 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPGTB     = (29 << 21) | (0x09) | OPC_CP2,

    OPC_PSLLW       = (24 << 21) | (0x0A) | OPC_CP2,
    OPC_PSLLH       = (25 << 21) | (0x0A) | OPC_CP2,
    OPC_PMULLH      = (26 << 21) | (0x0A) | OPC_CP2,
    OPC_PMULHH      = (27 << 21) | (0x0A) | OPC_CP2,
    OPC_PMULUW      = (28 << 21) | (0x0A) | OPC_CP2,
    OPC_PMULHUH     = (29 << 21) | (0x0A) | OPC_CP2,

    OPC_PSRLW       = (24 << 21) | (0x0B) | OPC_CP2,
    OPC_PSRLH       = (25 << 21) | (0x0B) | OPC_CP2,
    OPC_PSRAW       = (26 << 21) | (0x0B) | OPC_CP2,
    OPC_PSRAH       = (27 << 21) | (0x0B) | OPC_CP2,
    OPC_PUNPCKLWD   = (28 << 21) | (0x0B) | OPC_CP2,
    OPC_PUNPCKHWD   = (29 << 21) | (0x0B) | OPC_CP2,

    OPC_ADDU_CP2    = (24 << 21) | (0x0C) | OPC_CP2,
    OPC_OR_CP2      = (25 << 21) | (0x0C) | OPC_CP2,
    OPC_ADD_CP2     = (26 << 21) | (0x0C) | OPC_CP2,
    OPC_DADD_CP2    = (27 << 21) | (0x0C) | OPC_CP2,
    OPC_SEQU_CP2    = (28 << 21) | (0x0C) | OPC_CP2,
    OPC_SEQ_CP2     = (29 << 21) | (0x0C) | OPC_CP2,

    OPC_SUBU_CP2    = (24 << 21) | (0x0D) | OPC_CP2,
    OPC_PASUBUB     = (25 << 21) | (0x0D) | OPC_CP2,
    OPC_SUB_CP2     = (26 << 21) | (0x0D) | OPC_CP2,
    OPC_DSUB_CP2    = (27 << 21) | (0x0D) | OPC_CP2,
    OPC_SLTU_CP2    = (28 << 21) | (0x0D) | OPC_CP2,
    OPC_SLT_CP2     = (29 << 21) | (0x0D) | OPC_CP2,

    OPC_SLL_CP2     = (24 << 21) | (0x0E) | OPC_CP2,
    OPC_DSLL_CP2    = (25 << 21) | (0x0E) | OPC_CP2,
    OPC_PEXTRH      = (26 << 21) | (0x0E) | OPC_CP2,
    OPC_PMADDHW     = (27 << 21) | (0x0E) | OPC_CP2,
    OPC_SLEU_CP2    = (28 << 21) | (0x0E) | OPC_CP2,
    OPC_SLE_CP2     = (29 << 21) | (0x0E) | OPC_CP2,

    OPC_SRL_CP2     = (24 << 21) | (0x0F) | OPC_CP2,
    OPC_DSRL_CP2    = (25 << 21) | (0x0F) | OPC_CP2,
    OPC_SRA_CP2     = (26 << 21) | (0x0F) | OPC_CP2,
    OPC_DSRA_CP2    = (27 << 21) | (0x0F) | OPC_CP2,
    OPC_BIADD       = (28 << 21) | (0x0F) | OPC_CP2,
    OPC_PMOVMSKB    = (29 << 21) | (0x0F) | OPC_CP2,
};

/* Godson integer instructions */
static void gen_loongson_integer(DisasContext *ctx, uint32_t opc,
                                 int rd, int rs, int rt)
{
    TCGv t0, t1;

    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }

    switch (opc) {
    case OPC_MULT_G_2E:
    case OPC_MULT_G_2F:
    case OPC_MULTU_G_2E:
    case OPC_MULTU_G_2F:
#if defined(TARGET_MIPS64)
    case OPC_DMULT_G_2E:
    case OPC_DMULT_G_2F:
    case OPC_DMULTU_G_2E:
    case OPC_DMULTU_G_2F:
#endif
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        break;
    default:
        t0 = tcg_temp_local_new();
        t1 = tcg_temp_local_new();
        break;
    }

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    switch (opc) {
    case OPC_MULT_G_2E:
    case OPC_MULT_G_2F:
        tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
        break;
    case OPC_MULTU_G_2E:
    case OPC_MULTU_G_2F:
        tcg_gen_ext32u_tl(t0, t0);
        tcg_gen_ext32u_tl(t1, t1);
        tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
        break;
    case OPC_DIV_G_2E:
    case OPC_DIV_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            TCGLabel *l3 = gen_new_label();
            tcg_gen_ext32s_tl(t0, t0);
            tcg_gen_ext32s_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l3);
            gen_set_label(l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, INT_MIN, l2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1, l2);
            tcg_gen_mov_tl(cpu_gpr[rd], t0);
            tcg_gen_br(l3);
            gen_set_label(l2);
            tcg_gen_div_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            gen_set_label(l3);
        }
        break;
    case OPC_DIVU_G_2E:
    case OPC_DIVU_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_divu_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            gen_set_label(l2);
        }
        break;
    case OPC_MOD_G_2E:
    case OPC_MOD_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            TCGLabel *l3 = gen_new_label();
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, INT_MIN, l2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1, l2);
            gen_set_label(l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l3);
            gen_set_label(l2);
            tcg_gen_rem_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            gen_set_label(l3);
        }
        break;
    case OPC_MODU_G_2E:
    case OPC_MODU_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_remu_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            gen_set_label(l2);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DMULT_G_2E:
    case OPC_DMULT_G_2F:
        tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
        break;
    case OPC_DMULTU_G_2E:
    case OPC_DMULTU_G_2F:
        tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
        break;
    case OPC_DDIV_G_2E:
    case OPC_DDIV_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            TCGLabel *l3 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l3);
            gen_set_label(l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, -1LL << 63, l2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1LL, l2);
            tcg_gen_mov_tl(cpu_gpr[rd], t0);
            tcg_gen_br(l3);
            gen_set_label(l2);
            tcg_gen_div_tl(cpu_gpr[rd], t0, t1);
            gen_set_label(l3);
        }
        break;
    case OPC_DDIVU_G_2E:
    case OPC_DDIVU_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_divu_tl(cpu_gpr[rd], t0, t1);
            gen_set_label(l2);
        }
        break;
    case OPC_DMOD_G_2E:
    case OPC_DMOD_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            TCGLabel *l3 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, -1LL << 63, l2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1LL, l2);
            gen_set_label(l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l3);
            gen_set_label(l2);
            tcg_gen_rem_tl(cpu_gpr[rd], t0, t1);
            gen_set_label(l3);
        }
        break;
    case OPC_DMODU_G_2E:
    case OPC_DMODU_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_remu_tl(cpu_gpr[rd], t0, t1);
            gen_set_label(l2);
        }
        break;
#endif
    }

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* Loongson multimedia instructions */
static void gen_loongson_multimedia(DisasContext *ctx, int rd, int rs, int rt)
{
    uint32_t opc, shift_max;
    TCGv_i64 t0, t1;
    TCGCond cond;

    opc = MASK_LMMI(ctx->opcode);
    switch (opc) {
    case OPC_ADD_CP2:
    case OPC_SUB_CP2:
    case OPC_DADD_CP2:
    case OPC_DSUB_CP2:
        t0 = tcg_temp_local_new_i64();
        t1 = tcg_temp_local_new_i64();
        break;
    default:
        t0 = tcg_temp_new_i64();
        t1 = tcg_temp_new_i64();
        break;
    }

    check_cp1_enabled(ctx);
    gen_load_fpr64(ctx, t0, rs);
    gen_load_fpr64(ctx, t1, rt);

    switch (opc) {
    case OPC_PADDSH:
        gen_helper_paddsh(t0, t0, t1);
        break;
    case OPC_PADDUSH:
        gen_helper_paddush(t0, t0, t1);
        break;
    case OPC_PADDH:
        gen_helper_paddh(t0, t0, t1);
        break;
    case OPC_PADDW:
        gen_helper_paddw(t0, t0, t1);
        break;
    case OPC_PADDSB:
        gen_helper_paddsb(t0, t0, t1);
        break;
    case OPC_PADDUSB:
        gen_helper_paddusb(t0, t0, t1);
        break;
    case OPC_PADDB:
        gen_helper_paddb(t0, t0, t1);
        break;

    case OPC_PSUBSH:
        gen_helper_psubsh(t0, t0, t1);
        break;
    case OPC_PSUBUSH:
        gen_helper_psubush(t0, t0, t1);
        break;
    case OPC_PSUBH:
        gen_helper_psubh(t0, t0, t1);
        break;
    case OPC_PSUBW:
        gen_helper_psubw(t0, t0, t1);
        break;
    case OPC_PSUBSB:
        gen_helper_psubsb(t0, t0, t1);
        break;
    case OPC_PSUBUSB:
        gen_helper_psubusb(t0, t0, t1);
        break;
    case OPC_PSUBB:
        gen_helper_psubb(t0, t0, t1);
        break;

    case OPC_PSHUFH:
        gen_helper_pshufh(t0, t0, t1);
        break;
    case OPC_PACKSSWH:
        gen_helper_packsswh(t0, t0, t1);
        break;
    case OPC_PACKSSHB:
        gen_helper_packsshb(t0, t0, t1);
        break;
    case OPC_PACKUSHB:
        gen_helper_packushb(t0, t0, t1);
        break;

    case OPC_PUNPCKLHW:
        gen_helper_punpcklhw(t0, t0, t1);
        break;
    case OPC_PUNPCKHHW:
        gen_helper_punpckhhw(t0, t0, t1);
        break;
    case OPC_PUNPCKLBH:
        gen_helper_punpcklbh(t0, t0, t1);
        break;
    case OPC_PUNPCKHBH:
        gen_helper_punpckhbh(t0, t0, t1);
        break;
    case OPC_PUNPCKLWD:
        gen_helper_punpcklwd(t0, t0, t1);
        break;
    case OPC_PUNPCKHWD:
        gen_helper_punpckhwd(t0, t0, t1);
        break;

    case OPC_PAVGH:
        gen_helper_pavgh(t0, t0, t1);
        break;
    case OPC_PAVGB:
        gen_helper_pavgb(t0, t0, t1);
        break;
    case OPC_PMAXSH:
        gen_helper_pmaxsh(t0, t0, t1);
        break;
    case OPC_PMINSH:
        gen_helper_pminsh(t0, t0, t1);
        break;
    case OPC_PMAXUB:
        gen_helper_pmaxub(t0, t0, t1);
        break;
    case OPC_PMINUB:
        gen_helper_pminub(t0, t0, t1);
        break;

    case OPC_PCMPEQW:
        gen_helper_pcmpeqw(t0, t0, t1);
        break;
    case OPC_PCMPGTW:
        gen_helper_pcmpgtw(t0, t0, t1);
        break;
    case OPC_PCMPEQH:
        gen_helper_pcmpeqh(t0, t0, t1);
        break;
    case OPC_PCMPGTH:
        gen_helper_pcmpgth(t0, t0, t1);
        break;
    case OPC_PCMPEQB:
        gen_helper_pcmpeqb(t0, t0, t1);
        break;
    case OPC_PCMPGTB:
        gen_helper_pcmpgtb(t0, t0, t1);
        break;

    case OPC_PSLLW:
        gen_helper_psllw(t0, t0, t1);
        break;
    case OPC_PSLLH:
        gen_helper_psllh(t0, t0, t1);
        break;
    case OPC_PSRLW:
        gen_helper_psrlw(t0, t0, t1);
        break;
    case OPC_PSRLH:
        gen_helper_psrlh(t0, t0, t1);
        break;
    case OPC_PSRAW:
        gen_helper_psraw(t0, t0, t1);
        break;
    case OPC_PSRAH:
        gen_helper_psrah(t0, t0, t1);
        break;

    case OPC_PMULLH:
        gen_helper_pmullh(t0, t0, t1);
        break;
    case OPC_PMULHH:
        gen_helper_pmulhh(t0, t0, t1);
        break;
    case OPC_PMULHUH:
        gen_helper_pmulhuh(t0, t0, t1);
        break;
    case OPC_PMADDHW:
        gen_helper_pmaddhw(t0, t0, t1);
        break;

    case OPC_PASUBUB:
        gen_helper_pasubub(t0, t0, t1);
        break;
    case OPC_BIADD:
        gen_helper_biadd(t0, t0);
        break;
    case OPC_PMOVMSKB:
        gen_helper_pmovmskb(t0, t0);
        break;

    case OPC_PADDD:
        tcg_gen_add_i64(t0, t0, t1);
        break;
    case OPC_PSUBD:
        tcg_gen_sub_i64(t0, t0, t1);
        break;
    case OPC_XOR_CP2:
        tcg_gen_xor_i64(t0, t0, t1);
        break;
    case OPC_NOR_CP2:
        tcg_gen_nor_i64(t0, t0, t1);
        break;
    case OPC_AND_CP2:
        tcg_gen_and_i64(t0, t0, t1);
        break;
    case OPC_OR_CP2:
        tcg_gen_or_i64(t0, t0, t1);
        break;

    case OPC_PANDN:
        tcg_gen_andc_i64(t0, t1, t0);
        break;

    case OPC_PINSRH_0:
        tcg_gen_deposit_i64(t0, t0, t1, 0, 16);
        break;
    case OPC_PINSRH_1:
        tcg_gen_deposit_i64(t0, t0, t1, 16, 16);
        break;
    case OPC_PINSRH_2:
        tcg_gen_deposit_i64(t0, t0, t1, 32, 16);
        break;
    case OPC_PINSRH_3:
        tcg_gen_deposit_i64(t0, t0, t1, 48, 16);
        break;

    case OPC_PEXTRH:
        tcg_gen_andi_i64(t1, t1, 3);
        tcg_gen_shli_i64(t1, t1, 4);
        tcg_gen_shr_i64(t0, t0, t1);
        tcg_gen_ext16u_i64(t0, t0);
        break;

    case OPC_ADDU_CP2:
        tcg_gen_add_i64(t0, t0, t1);
        tcg_gen_ext32s_i64(t0, t0);
        break;
    case OPC_SUBU_CP2:
        tcg_gen_sub_i64(t0, t0, t1);
        tcg_gen_ext32s_i64(t0, t0);
        break;

    case OPC_SLL_CP2:
        shift_max = 32;
        goto do_shift;
    case OPC_SRL_CP2:
        shift_max = 32;
        goto do_shift;
    case OPC_SRA_CP2:
        shift_max = 32;
        goto do_shift;
    case OPC_DSLL_CP2:
        shift_max = 64;
        goto do_shift;
    case OPC_DSRL_CP2:
        shift_max = 64;
        goto do_shift;
    case OPC_DSRA_CP2:
        shift_max = 64;
        goto do_shift;
    do_shift:
        /* Make sure shift count isn't TCG undefined behaviour.  */
        tcg_gen_andi_i64(t1, t1, shift_max - 1);

        switch (opc) {
        case OPC_SLL_CP2:
        case OPC_DSLL_CP2:
            tcg_gen_shl_i64(t0, t0, t1);
            break;
        case OPC_SRA_CP2:
        case OPC_DSRA_CP2:
            /*
             * Since SRA is UndefinedResult without sign-extended inputs,
             * we can treat SRA and DSRA the same.
             */
            tcg_gen_sar_i64(t0, t0, t1);
            break;
        case OPC_SRL_CP2:
            /* We want to shift in zeros for SRL; zero-extend first.  */
            tcg_gen_ext32u_i64(t0, t0);
            /* FALLTHRU */
        case OPC_DSRL_CP2:
            tcg_gen_shr_i64(t0, t0, t1);
            break;
        }

        if (shift_max == 32) {
            tcg_gen_ext32s_i64(t0, t0);
        }

        /* Shifts larger than MAX produce zero.  */
        tcg_gen_setcondi_i64(TCG_COND_LTU, t1, t1, shift_max);
        tcg_gen_neg_i64(t1, t1);
        tcg_gen_and_i64(t0, t0, t1);
        break;

    case OPC_ADD_CP2:
    case OPC_DADD_CP2:
        {
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGLabel *lab = gen_new_label();

            tcg_gen_mov_i64(t2, t0);
            tcg_gen_add_i64(t0, t1, t2);
            if (opc == OPC_ADD_CP2) {
                tcg_gen_ext32s_i64(t0, t0);
            }
            tcg_gen_xor_i64(t1, t1, t2);
            tcg_gen_xor_i64(t2, t2, t0);
            tcg_gen_andc_i64(t1, t2, t1);
            tcg_temp_free_i64(t2);
            tcg_gen_brcondi_i64(TCG_COND_GE, t1, 0, lab);
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(lab);
            break;
        }

    case OPC_SUB_CP2:
    case OPC_DSUB_CP2:
        {
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGLabel *lab = gen_new_label();

            tcg_gen_mov_i64(t2, t0);
            tcg_gen_sub_i64(t0, t1, t2);
            if (opc == OPC_SUB_CP2) {
                tcg_gen_ext32s_i64(t0, t0);
            }
            tcg_gen_xor_i64(t1, t1, t2);
            tcg_gen_xor_i64(t2, t2, t0);
            tcg_gen_and_i64(t1, t1, t2);
            tcg_temp_free_i64(t2);
            tcg_gen_brcondi_i64(TCG_COND_GE, t1, 0, lab);
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(lab);
            break;
        }

    case OPC_PMULUW:
        tcg_gen_ext32u_i64(t0, t0);
        tcg_gen_ext32u_i64(t1, t1);
        tcg_gen_mul_i64(t0, t0, t1);
        break;

    case OPC_SEQU_CP2:
    case OPC_SEQ_CP2:
        cond = TCG_COND_EQ;
        goto do_cc_cond;
        break;
    case OPC_SLTU_CP2:
        cond = TCG_COND_LTU;
        goto do_cc_cond;
        break;
    case OPC_SLT_CP2:
        cond = TCG_COND_LT;
        goto do_cc_cond;
        break;
    case OPC_SLEU_CP2:
        cond = TCG_COND_LEU;
        goto do_cc_cond;
        break;
    case OPC_SLE_CP2:
        cond = TCG_COND_LE;
    do_cc_cond:
        {
            int cc = (ctx->opcode >> 8) & 0x7;
            TCGv_i64 t64 = tcg_temp_new_i64();
            TCGv_i32 t32 = tcg_temp_new_i32();

            tcg_gen_setcond_i64(cond, t64, t0, t1);
            tcg_gen_extrl_i64_i32(t32, t64);
            tcg_gen_deposit_i32(fpu_fcr31, fpu_fcr31, t32,
                                get_fp_bit(cc), 1);

            tcg_temp_free_i32(t32);
            tcg_temp_free_i64(t64);
        }
        goto no_rd;
        break;
    default:
        MIPS_INVAL("loongson_cp2");
        gen_reserved_instruction(ctx);
        return;
    }

    gen_store_fpr64(ctx, t0, rd);

no_rd:
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static void gen_loongson_lswc2(DisasContext *ctx, int rt,
                               int rs, int rd)
{
    TCGv t0, t1, t2;
    TCGv_i32 fp0;
#if defined(TARGET_MIPS64)
    int lsq_rt1 = ctx->opcode & 0x1f;
    int lsq_offset = sextract32(ctx->opcode, 6, 9) << 4;
#endif
    int shf_offset = sextract32(ctx->opcode, 6, 8);

    t0 = tcg_temp_new();

    switch (MASK_LOONGSON_GSLSQ(ctx->opcode)) {
#if defined(TARGET_MIPS64)
    case OPC_GSLQ:
        t1 = tcg_temp_new();
        gen_base_offset_addr(ctx, t0, rs, lsq_offset);
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_base_offset_addr(ctx, t0, rs, lsq_offset + 8);
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t1, rt);
        gen_store_gpr(t0, lsq_rt1);
        tcg_temp_free(t1);
        break;
    case OPC_GSLQC1:
        check_cp1_enabled(ctx);
        t1 = tcg_temp_new();
        gen_base_offset_addr(ctx, t0, rs, lsq_offset);
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_base_offset_addr(ctx, t0, rs, lsq_offset + 8);
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_store_fpr64(ctx, t1, rt);
        gen_store_fpr64(ctx, t0, lsq_rt1);
        tcg_temp_free(t1);
        break;
    case OPC_GSSQ:
        t1 = tcg_temp_new();
        gen_base_offset_addr(ctx, t0, rs, lsq_offset);
        gen_load_gpr(t1, rt);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_base_offset_addr(ctx, t0, rs, lsq_offset + 8);
        gen_load_gpr(t1, lsq_rt1);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        tcg_temp_free(t1);
        break;
    case OPC_GSSQC1:
        check_cp1_enabled(ctx);
        t1 = tcg_temp_new();
        gen_base_offset_addr(ctx, t0, rs, lsq_offset);
        gen_load_fpr64(ctx, t1, rt);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_base_offset_addr(ctx, t0, rs, lsq_offset + 8);
        gen_load_fpr64(ctx, t1, lsq_rt1);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        tcg_temp_free(t1);
        break;
#endif
    case OPC_GSSHFL:
        switch (MASK_LOONGSON_GSSHFLS(ctx->opcode)) {
        case OPC_GSLWLC1:
            check_cp1_enabled(ctx);
            gen_base_offset_addr(ctx, t0, rs, shf_offset);
            t1 = tcg_temp_new();
            tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
            tcg_gen_andi_tl(t1, t0, 3);
            if (!cpu_is_bigendian(ctx)) {
                tcg_gen_xori_tl(t1, t1, 3);
            }
            tcg_gen_shli_tl(t1, t1, 3);
            tcg_gen_andi_tl(t0, t0, ~3);
            tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUL);
            tcg_gen_shl_tl(t0, t0, t1);
            t2 = tcg_const_tl(-1);
            tcg_gen_shl_tl(t2, t2, t1);
            fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, rt);
            tcg_gen_ext_i32_tl(t1, fp0);
            tcg_gen_andc_tl(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_or_tl(t0, t0, t1);
            tcg_temp_free(t1);
#if defined(TARGET_MIPS64)
            tcg_gen_extrl_i64_i32(fp0, t0);
#else
            tcg_gen_ext32s_tl(fp0, t0);
#endif
            gen_store_fpr32(ctx, fp0, rt);
            tcg_temp_free_i32(fp0);
            break;
        case OPC_GSLWRC1:
            check_cp1_enabled(ctx);
            gen_base_offset_addr(ctx, t0, rs, shf_offset);
            t1 = tcg_temp_new();
            tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
            tcg_gen_andi_tl(t1, t0, 3);
            if (cpu_is_bigendian(ctx)) {
                tcg_gen_xori_tl(t1, t1, 3);
            }
            tcg_gen_shli_tl(t1, t1, 3);
            tcg_gen_andi_tl(t0, t0, ~3);
            tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUL);
            tcg_gen_shr_tl(t0, t0, t1);
            tcg_gen_xori_tl(t1, t1, 31);
            t2 = tcg_const_tl(0xfffffffeull);
            tcg_gen_shl_tl(t2, t2, t1);
            fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, rt);
            tcg_gen_ext_i32_tl(t1, fp0);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_or_tl(t0, t0, t1);
            tcg_temp_free(t1);
#if defined(TARGET_MIPS64)
            tcg_gen_extrl_i64_i32(fp0, t0);
#else
            tcg_gen_ext32s_tl(fp0, t0);
#endif
            gen_store_fpr32(ctx, fp0, rt);
            tcg_temp_free_i32(fp0);
            break;
#if defined(TARGET_MIPS64)
        case OPC_GSLDLC1:
            check_cp1_enabled(ctx);
            gen_base_offset_addr(ctx, t0, rs, shf_offset);
            t1 = tcg_temp_new();
            tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
            tcg_gen_andi_tl(t1, t0, 7);
            if (!cpu_is_bigendian(ctx)) {
                tcg_gen_xori_tl(t1, t1, 7);
            }
            tcg_gen_shli_tl(t1, t1, 3);
            tcg_gen_andi_tl(t0, t0, ~7);
            tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUQ);
            tcg_gen_shl_tl(t0, t0, t1);
            t2 = tcg_const_tl(-1);
            tcg_gen_shl_tl(t2, t2, t1);
            gen_load_fpr64(ctx, t1, rt);
            tcg_gen_andc_tl(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_or_tl(t0, t0, t1);
            tcg_temp_free(t1);
            gen_store_fpr64(ctx, t0, rt);
            break;
        case OPC_GSLDRC1:
            check_cp1_enabled(ctx);
            gen_base_offset_addr(ctx, t0, rs, shf_offset);
            t1 = tcg_temp_new();
            tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_UB);
            tcg_gen_andi_tl(t1, t0, 7);
            if (cpu_is_bigendian(ctx)) {
                tcg_gen_xori_tl(t1, t1, 7);
            }
            tcg_gen_shli_tl(t1, t1, 3);
            tcg_gen_andi_tl(t0, t0, ~7);
            tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUQ);
            tcg_gen_shr_tl(t0, t0, t1);
            tcg_gen_xori_tl(t1, t1, 63);
            t2 = tcg_const_tl(0xfffffffffffffffeull);
            tcg_gen_shl_tl(t2, t2, t1);
            gen_load_fpr64(ctx, t1, rt);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_or_tl(t0, t0, t1);
            tcg_temp_free(t1);
            gen_store_fpr64(ctx, t0, rt);
            break;
#endif
        default:
            MIPS_INVAL("loongson_gsshfl");
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case OPC_GSSHFS:
        switch (MASK_LOONGSON_GSSHFLS(ctx->opcode)) {
        case OPC_GSSWLC1:
            check_cp1_enabled(ctx);
            t1 = tcg_temp_new();
            gen_base_offset_addr(ctx, t0, rs, shf_offset);
            fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, rt);
            tcg_gen_ext_i32_tl(t1, fp0);
            gen_helper_0e2i(swl, t1, t0, ctx->mem_idx);
            tcg_temp_free_i32(fp0);
            tcg_temp_free(t1);
            break;
        case OPC_GSSWRC1:
            check_cp1_enabled(ctx);
            t1 = tcg_temp_new();
            gen_base_offset_addr(ctx, t0, rs, shf_offset);
            fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, rt);
            tcg_gen_ext_i32_tl(t1, fp0);
            gen_helper_0e2i(swr, t1, t0, ctx->mem_idx);
            tcg_temp_free_i32(fp0);
            tcg_temp_free(t1);
            break;
#if defined(TARGET_MIPS64)
        case OPC_GSSDLC1:
            check_cp1_enabled(ctx);
            t1 = tcg_temp_new();
            gen_base_offset_addr(ctx, t0, rs, shf_offset);
            gen_load_fpr64(ctx, t1, rt);
            gen_helper_0e2i(sdl, t1, t0, ctx->mem_idx);
            tcg_temp_free(t1);
            break;
        case OPC_GSSDRC1:
            check_cp1_enabled(ctx);
            t1 = tcg_temp_new();
            gen_base_offset_addr(ctx, t0, rs, shf_offset);
            gen_load_fpr64(ctx, t1, rt);
            gen_helper_0e2i(sdr, t1, t0, ctx->mem_idx);
            tcg_temp_free(t1);
            break;
#endif
        default:
            MIPS_INVAL("loongson_gsshfs");
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    default:
        MIPS_INVAL("loongson_gslsq");
        gen_reserved_instruction(ctx);
        break;
    }
    tcg_temp_free(t0);
}

/* Loongson EXT LDC2/SDC2 */
static void gen_loongson_lsdc2(DisasContext *ctx, int rt,
                               int rs, int rd)
{
    int offset = sextract32(ctx->opcode, 3, 8);
    uint32_t opc = MASK_LOONGSON_LSDC2(ctx->opcode);
    TCGv t0, t1;
    TCGv_i32 fp0;

    /* Pre-conditions */
    switch (opc) {
    case OPC_GSLBX:
    case OPC_GSLHX:
    case OPC_GSLWX:
    case OPC_GSLDX:
        /* prefetch, implement as NOP */
        if (rt == 0) {
            return;
        }
        break;
    case OPC_GSSBX:
    case OPC_GSSHX:
    case OPC_GSSWX:
    case OPC_GSSDX:
        break;
    case OPC_GSLWXC1:
#if defined(TARGET_MIPS64)
    case OPC_GSLDXC1:
#endif
        check_cp1_enabled(ctx);
        /* prefetch, implement as NOP */
        if (rt == 0) {
            return;
        }
        break;
    case OPC_GSSWXC1:
#if defined(TARGET_MIPS64)
    case OPC_GSSDXC1:
#endif
        check_cp1_enabled(ctx);
        break;
    default:
        MIPS_INVAL("loongson_lsdc2");
        gen_reserved_instruction(ctx);
        return;
        break;
    }

    t0 = tcg_temp_new();

    gen_base_offset_addr(ctx, t0, rs, offset);
    gen_op_addr_add(ctx, t0, cpu_gpr[rd], t0);

    switch (opc) {
    case OPC_GSLBX:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_SB);
        gen_store_gpr(t0, rt);
        break;
    case OPC_GSLHX:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESW |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t0, rt);
        break;
    case OPC_GSLWX:
        gen_base_offset_addr(ctx, t0, rs, offset);
        if (rd) {
            gen_op_addr_add(ctx, t0, cpu_gpr[rd], t0);
        }
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESL |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t0, rt);
        break;
#if defined(TARGET_MIPS64)
    case OPC_GSLDX:
        gen_base_offset_addr(ctx, t0, rs, offset);
        if (rd) {
            gen_op_addr_add(ctx, t0, cpu_gpr[rd], t0);
        }
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t0, rt);
        break;
#endif
    case OPC_GSLWXC1:
        gen_base_offset_addr(ctx, t0, rs, offset);
        if (rd) {
            gen_op_addr_add(ctx, t0, cpu_gpr[rd], t0);
        }
        fp0 = tcg_temp_new_i32();
        tcg_gen_qemu_ld_i32(fp0, t0, ctx->mem_idx, MO_TESL |
                            ctx->default_tcg_memop_mask);
        gen_store_fpr32(ctx, fp0, rt);
        tcg_temp_free_i32(fp0);
        break;
#if defined(TARGET_MIPS64)
    case OPC_GSLDXC1:
        gen_base_offset_addr(ctx, t0, rs, offset);
        if (rd) {
            gen_op_addr_add(ctx, t0, cpu_gpr[rd], t0);
        }
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_store_fpr64(ctx, t0, rt);
        break;
#endif
    case OPC_GSSBX:
        t1 = tcg_temp_new();
        gen_load_gpr(t1, rt);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_SB);
        tcg_temp_free(t1);
        break;
    case OPC_GSSHX:
        t1 = tcg_temp_new();
        gen_load_gpr(t1, rt);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUW |
                           ctx->default_tcg_memop_mask);
        tcg_temp_free(t1);
        break;
    case OPC_GSSWX:
        t1 = tcg_temp_new();
        gen_load_gpr(t1, rt);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
        tcg_temp_free(t1);
        break;
#if defined(TARGET_MIPS64)
    case OPC_GSSDX:
        t1 = tcg_temp_new();
        gen_load_gpr(t1, rt);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        tcg_temp_free(t1);
        break;
#endif
    case OPC_GSSWXC1:
        fp0 = tcg_temp_new_i32();
        gen_load_fpr32(ctx, fp0, rt);
        tcg_gen_qemu_st_i32(fp0, t0, ctx->mem_idx, MO_TEUL |
                            ctx->default_tcg_memop_mask);
        tcg_temp_free_i32(fp0);
        break;
#if defined(TARGET_MIPS64)
    case OPC_GSSDXC1:
        t1 = tcg_temp_new();
        gen_load_fpr64(ctx, t1, rt);
        tcg_gen_qemu_st_i64(t1, t0, ctx->mem_idx, MO_TEUQ |
                            ctx->default_tcg_memop_mask);
        tcg_temp_free(t1);
        break;
#endif
    default:
        break;
    }

    tcg_temp_free(t0);
}

bool decode_ext_loongson2e(DisasContext *ctx, uint32_t insn)
{
    int rs, rt, rd;
    uint32_t op;

    op = MASK_2E_SPECIAL3(insn);
    rs = (insn >> 21) & 0x1f;
    rt = (insn >> 16) & 0x1f;
    rd = (insn >> 11) & 0x1f;

    switch (op) {
    case OPC_DIV_G_2E:
    case OPC_DIVU_G_2E:
    case OPC_MOD_G_2E:
    case OPC_MODU_G_2E:
    case OPC_MULT_G_2E:
    case OPC_MULTU_G_2E:
        gen_loongson_integer(ctx, op, rd, rs, rt);
        return true;
#if defined(TARGET_MIPS64)
    case OPC_DDIV_G_2E:
    case OPC_DDIVU_G_2E:
    case OPC_DMULT_G_2E:
    case OPC_DMULTU_G_2E:
    case OPC_DMOD_G_2E:
    case OPC_DMODU_G_2E:
        gen_loongson_integer(ctx, op, rd, rs, rt);
        return true;
#endif
    default:
        return false;
    }
}

bool decode_ext_loongson2f(DisasContext *ctx, uint32_t insn)
{
    int rs, rt, rd;
    uint32_t op;

    op = MASK_2F_SPECIAL2(insn);
    rs = (insn >> 21) & 0x1f;
    rt = (insn >> 16) & 0x1f;
    rd = (insn >> 11) & 0x1f;

    switch (op) {
    case OPC_DIV_G_2F:
    case OPC_DIVU_G_2F:
    case OPC_MULT_G_2F:
    case OPC_MULTU_G_2F:
    case OPC_MOD_G_2F:
    case OPC_MODU_G_2F:
        gen_loongson_integer(ctx, op, rd, rs, rt);
        return true;
#if defined(TARGET_MIPS64)
    case OPC_DMULT_G_2F:
    case OPC_DMULTU_G_2F:
    case OPC_DDIV_G_2F:
    case OPC_DDIVU_G_2F:
    case OPC_DMOD_G_2F:
    case OPC_DMODU_G_2F:
        gen_loongson_integer(ctx, op, rd, rs, rt);
        return true;
#endif
    default:
        return false;
    }
}

bool decode_ase_lext(DisasContext *ctx, uint32_t insn)
{
    int rs, rt, rd;
    uint32_t op;

    op = MASK_OP_MAJOR(insn);
    rs = (insn >> 21) & 0x1f;
    rt = (insn >> 16) & 0x1f;
    rd = (insn >> 11) & 0x1f;

    switch (op) {
    case OPC_SPECIAL2:
        /* LEXT inherits Loongson2F integer extensions */
        return decode_ext_loongson2f(ctx, insn);
    case OPC_LWC2:
    case OPC_SWC2:
        gen_loongson_lswc2(ctx, rt, rs, rd);
        return true;
    case OPC_LDC2:
    case OPC_SDC2:
        gen_loongson_lsdc2(ctx, rt, rs, rd);
        return true;
    default:
        return false;
    }
}

bool decode_ase_lmmi(DisasContext *ctx, uint32_t insn)
{
    int sa, rt, rd;
    uint32_t op;

    op = MASK_OP_MAJOR(insn);
    rt = (insn >> 16) & 0x1f;
    rd = (insn >> 11) & 0x1f;
    sa = (insn >> 6) & 0x1f;

    switch (op) {
    case OPC_CP2:
        gen_loongson_multimedia(ctx, sa, rd, rt);
        return true;
    default:
        return false;
    }
}
