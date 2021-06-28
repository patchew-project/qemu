/*
 * LoongArch emulation for qemu: instruction opcode
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef TARGET_LOONGARCH_INSTMAP_H
#define TARGET_LOONGARCH_INSTMAP_H

/* fixed point opcodes */
enum {
    LA_OPC_CLO_W     = (0x000004 << 10),
    LA_OPC_CLZ_W     = (0x000005 << 10),
    LA_OPC_CLO_D     = (0x000008 << 10),
    LA_OPC_CLZ_D     = (0x000009 << 10),
    LA_OPC_REVB_2H   = (0x00000C << 10),
    LA_OPC_REVB_4H   = (0x00000D << 10),
    LA_OPC_REVH_D    = (0x000011 << 10),
    LA_OPC_BREV_4B   = (0x000012 << 10),
    LA_OPC_BREV_8B   = (0x000013 << 10),
    LA_OPC_EXT_WH    = (0x000016 << 10),
    LA_OPC_EXT_WB    = (0x000017 << 10),

    LA_OPC_ADD_W     = (0x00020 << 15),
    LA_OPC_ADD_D     = (0x00021 << 15),
    LA_OPC_SUB_W     = (0x00022 << 15),
    LA_OPC_SUB_D     = (0x00023 << 15),
    LA_OPC_SLT       = (0x00024 << 15),
    LA_OPC_SLTU      = (0x00025 << 15),
    LA_OPC_MASKEQZ   = (0x00026 << 15),
    LA_OPC_MASKNEZ   = (0x00027 << 15),
    LA_OPC_NOR       = (0x00028 << 15),
    LA_OPC_AND       = (0x00029 << 15),
    LA_OPC_OR        = (0x0002A << 15),
    LA_OPC_XOR       = (0x0002B << 15),
    LA_OPC_SLL_W     = (0x0002E << 15),
    LA_OPC_SRL_W     = (0x0002F << 15),
    LA_OPC_SRA_W     = (0x00030 << 15),
    LA_OPC_SLL_D     = (0x00031 << 15),
    LA_OPC_SRL_D     = (0x00032 << 15),
    LA_OPC_SRA_D     = (0x00033 << 15),
    LA_OPC_ROTR_W    = (0x00036 << 15),
    LA_OPC_ROTR_D    = (0x00037 << 15),
    LA_OPC_MUL_W     = (0x00038 << 15),
    LA_OPC_MULH_W    = (0x00039 << 15),
    LA_OPC_MULH_WU   = (0x0003A << 15),
    LA_OPC_MUL_D     = (0x0003B << 15),
    LA_OPC_MULH_D    = (0x0003C << 15),
    LA_OPC_MULH_DU   = (0x0003D << 15),
    LA_OPC_DIV_W     = (0x00040 << 15),
    LA_OPC_MOD_W     = (0x00041 << 15),
    LA_OPC_DIV_WU    = (0x00042 << 15),
    LA_OPC_MOD_WU    = (0x00043 << 15),
    LA_OPC_DIV_D     = (0x00044 << 15),
    LA_OPC_MOD_D     = (0x00045 << 15),
    LA_OPC_DIV_DU    = (0x00046 << 15),
    LA_OPC_MOD_DU    = (0x00047 << 15),
    LA_OPC_SRLI_W    = (0x00089 << 15),
    LA_OPC_SRAI_W    = (0x00091 << 15),
    LA_OPC_ROTRI_W   = (0x00099 << 15),

    LA_OPC_ALSL_W    = (0x0002 << 17),
    LA_OPC_ALSL_D    = (0x0016 << 17),
    LA_OPC_TRINS_W   = (0x003 << 21) | (0x0 << 15),
    LA_OPC_TRPICK_W  = (0x003 << 21) | (0x1 << 15)
};

/* floating point opcodes*/
enum {
    LA_OPC_FABS_S    = (0x004501 << 10),
    LA_OPC_FABS_D    = (0x004502 << 10),
    LA_OPC_FNEG_S    = (0x004505 << 10),
    LA_OPC_FNEG_D    = (0x004506 << 10),
    LA_OPC_FCLASS_S  = (0x00450D << 10),
    LA_OPC_FCLASS_D  = (0x00450E << 10),
    LA_OPC_FSQRT_S   = (0x004511 << 10),
    LA_OPC_FSQRT_D   = (0x004512 << 10),
    LA_OPC_FRECIP_S  = (0x004515 << 10),
    LA_OPC_FRECIP_D  = (0x004516 << 10),
    LA_OPC_FRSQRT_S  = (0x004519 << 10),
    LA_OPC_FRSQRT_D  = (0x00451A << 10),
    LA_OPC_GR2FR_W   = (0x004529 << 10),
    LA_OPC_GR2FR_D   = (0x00452A << 10),
    LA_OPC_GR2FRH_W  = (0x00452B << 10),
    LA_OPC_FR2GR_S   = (0x00452D << 10),
    LA_OPC_FR2GR_D   = (0x00452E << 10),
    LA_OPC_FRH2GR_S  = (0x00452F << 10),
    LA_OPC_FCVT_S_D      = (0x004646 << 10),
    LA_OPC_FCVT_D_S      = (0x004649 << 10),
    LA_OPC_FTINTRM_W_S   = (0x004681 << 10),
    LA_OPC_FTINTRM_W_D   = (0x004682 << 10),
    LA_OPC_FTINTRM_L_S   = (0x004689 << 10),
    LA_OPC_FTINTRM_L_D   = (0x00468A << 10),
    LA_OPC_FTINTRP_W_S   = (0x004691 << 10),
    LA_OPC_FTINTRP_W_D   = (0x004692 << 10),
    LA_OPC_FTINTRP_L_S   = (0x004699 << 10),
    LA_OPC_FTINTRP_L_D   = (0x00469A << 10),
    LA_OPC_FTINTRZ_W_S   = (0x0046A1 << 10),
    LA_OPC_FTINTRZ_W_D   = (0x0046A2 << 10),
    LA_OPC_FTINTRZ_L_S   = (0x0046A9 << 10),
    LA_OPC_FTINTRZ_L_D   = (0x0046AA << 10),
    LA_OPC_FTINTRNE_W_S  = (0x0046B1 << 10),
    LA_OPC_FTINTRNE_W_D  = (0x0046B2 << 10),
    LA_OPC_FTINTRNE_L_S  = (0x0046B9 << 10),
    LA_OPC_FTINTRNE_L_D  = (0x0046BA << 10),
    LA_OPC_FTINT_W_S     = (0x0046C1 << 10),
    LA_OPC_FTINT_W_D     = (0x0046C2 << 10),
    LA_OPC_FTINT_L_S     = (0x0046C9 << 10),
    LA_OPC_FTINT_L_D     = (0x0046CA << 10),
    LA_OPC_FFINT_S_W     = (0x004744 << 10),
    LA_OPC_FFINT_S_L     = (0x004746 << 10),
    LA_OPC_FFINT_D_W     = (0x004748 << 10),
    LA_OPC_FFINT_D_L     = (0x00474A << 10),
    LA_OPC_FRINT_S       = (0x004791 << 10),
    LA_OPC_FRINT_D       = (0x004792 << 10),

    LA_OPC_FADD_S    = (0x00201 << 15),
    LA_OPC_FADD_D    = (0x00202 << 15),
    LA_OPC_FSUB_S    = (0x00205 << 15),
    LA_OPC_FSUB_D    = (0x00206 << 15),
    LA_OPC_FMUL_S    = (0x00209 << 15),
    LA_OPC_FMUL_D    = (0x0020A << 15),
    LA_OPC_FDIV_S    = (0x0020D << 15),
    LA_OPC_FDIV_D    = (0x0020E << 15),
    LA_OPC_FMAX_S    = (0x00211 << 15),
    LA_OPC_FMAX_D    = (0x00212 << 15),
    LA_OPC_FMIN_S    = (0x00215 << 15),
    LA_OPC_FMIN_D    = (0x00216 << 15),
    LA_OPC_FMAXA_S   = (0x00219 << 15),
    LA_OPC_FMAXA_D   = (0x0021A << 15),
    LA_OPC_FMINA_S   = (0x0021D << 15),
    LA_OPC_FMINA_D   = (0x0021E << 15)
};

/* 12 bit immediate opcodes */
enum {
    LA_OPC_SLTI      = (0x008 << 22),
    LA_OPC_SLTIU     = (0x009 << 22),
    LA_OPC_ADDI_W    = (0x00A << 22),
    LA_OPC_ADDI_D    = (0x00B << 22),
    LA_OPC_ANDI      = (0x00D << 22),
    LA_OPC_ORI       = (0x00E << 22),
    LA_OPC_XORI      = (0x00F << 22)
};

/* load/store opcodes */
enum {
    LA_OPC_FLDX_S    = (0x07060 << 15),
    LA_OPC_FLDX_D    = (0x07068 << 15),
    LA_OPC_FSTX_S    = (0x07070 << 15),
    LA_OPC_FSTX_D    = (0x07078 << 15),
    LA_OPC_FLDGT_S   = (0x070E8 << 15),
    LA_OPC_FLDGT_D   = (0x070E9 << 15),
    LA_OPC_FLDLE_S   = (0x070EA << 15),
    LA_OPC_FLDLE_D   = (0x070EB << 15),
    LA_OPC_FSTGT_S   = (0x070EC << 15),
    LA_OPC_FSTGT_D   = (0x070ED << 15),
    LA_OPC_FSTLE_S   = (0x070EE << 15),
    LA_OPC_FSTLE_D   = (0x070EF << 15),

    LA_OPC_LD_B      = (0x0A0 << 22),
    LA_OPC_LD_H      = (0x0A1 << 22),
    LA_OPC_LD_W      = (0x0A2 << 22),
    LA_OPC_LD_D      = (0x0A3 << 22),
    LA_OPC_ST_B      = (0x0A4 << 22),
    LA_OPC_ST_H      = (0x0A5 << 22),
    LA_OPC_ST_W      = (0x0A6 << 22),
    LA_OPC_ST_D      = (0x0A7 << 22),
    LA_OPC_LD_BU     = (0x0A8 << 22),
    LA_OPC_LD_HU     = (0x0A9 << 22),
    LA_OPC_LD_WU     = (0x0AA << 22),
    LA_OPC_FLD_S     = (0x0AC << 22),
    LA_OPC_FST_S     = (0x0AD << 22),
    LA_OPC_FLD_D     = (0x0AE << 22),
    LA_OPC_FST_D     = (0x0AF << 22),

    LA_OPC_LL_W      = (0x20 << 24),
    LA_OPC_LL_D      = (0x22 << 24),
    LA_OPC_LDPTR_W   = (0x24 << 24),
    LA_OPC_STPTR_W   = (0x25 << 24),
    LA_OPC_LDPTR_D   = (0x26 << 24),
    LA_OPC_STPTR_D   = (0x27 << 24)
};

/* Branch opcodes */
enum {
    LA_OPC_BEQZ      = (0x10 << 26),
    LA_OPC_BNEZ      = (0x11 << 26),
    LA_OPC_B         = (0x14 << 26),
    LA_OPC_BEQ       = (0x16 << 26),
    LA_OPC_BNE       = (0x17 << 26),
    LA_OPC_BLT       = (0x18 << 26),
    LA_OPC_BGE       = (0x19 << 26),
    LA_OPC_BLTU      = (0x1A << 26),
    LA_OPC_BGEU      = (0x1B << 26)
};

#endif
