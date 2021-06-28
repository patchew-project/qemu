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

    LA_OPC_LL_W      = (0x20 << 24),
    LA_OPC_LL_D      = (0x22 << 24),
    LA_OPC_LDPTR_W   = (0x24 << 24),
    LA_OPC_STPTR_W   = (0x25 << 24),
    LA_OPC_LDPTR_D   = (0x26 << 24),
    LA_OPC_STPTR_D   = (0x27 << 24)
};

#endif
