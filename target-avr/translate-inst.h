/*
 *  QEMU AVR CPU
 *
 *  Copyright (c) 2016 Michael Rolnik
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see
 *  <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef AVR_TRANSLATE_INST_H_
#define AVR_TRANSLATE_INST_H_

typedef struct DisasContext DisasContext;

int avr_translate_NOP(DisasContext* ctx, uint32_t opcode);

int avr_translate_MOVW(DisasContext* ctx, uint32_t opcode);
static inline uint32_t MOVW_Rr(uint32_t opcode)
{
    return extract32(opcode, 0, 4);
}

static inline uint32_t MOVW_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

int avr_translate_MULS(DisasContext* ctx, uint32_t opcode);
static inline uint32_t MULS_Rr(uint32_t opcode)
{
    return extract32(opcode, 0, 4);
}

static inline uint32_t MULS_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

int avr_translate_MULSU(DisasContext* ctx, uint32_t opcode);
static inline uint32_t MULSU_Rr(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t MULSU_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 3);
}

int avr_translate_FMUL(DisasContext* ctx, uint32_t opcode);
static inline uint32_t FMUL_Rr(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t FMUL_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 3);
}

int avr_translate_FMULS(DisasContext* ctx, uint32_t opcode);
static inline uint32_t FMULS_Rr(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t FMULS_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 3);
}

int avr_translate_FMULSU(DisasContext* ctx, uint32_t opcode);
static inline uint32_t FMULSU_Rr(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t FMULSU_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 3);
}

int avr_translate_CPC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t CPC_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t CPC_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_SBC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SBC_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t SBC_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_ADD(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ADD_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t ADD_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_AND(DisasContext* ctx, uint32_t opcode);
static inline uint32_t AND_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t AND_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_EOR(DisasContext* ctx, uint32_t opcode);
static inline uint32_t EOR_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t EOR_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_OR(DisasContext* ctx, uint32_t opcode);
static inline uint32_t OR_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t OR_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_MOV(DisasContext* ctx, uint32_t opcode);
static inline uint32_t MOV_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t MOV_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_CPSE(DisasContext* ctx, uint32_t opcode);
static inline uint32_t CPSE_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t CPSE_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_CP(DisasContext* ctx, uint32_t opcode);
static inline uint32_t CP_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t CP_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_SUB(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SUB_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t SUB_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_ADC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ADC_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t ADC_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_CPI(DisasContext* ctx, uint32_t opcode);
static inline uint32_t CPI_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

static inline uint32_t CPI_Imm(uint32_t opcode)
{
    return (extract32(opcode, 8, 4) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_SBCI(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SBCI_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

static inline uint32_t SBCI_Imm(uint32_t opcode)
{
    return (extract32(opcode, 8, 4) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_ORI(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ORI_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

static inline uint32_t ORI_Imm(uint32_t opcode)
{
    return (extract32(opcode, 8, 4) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_SUBI(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SUBI_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

static inline uint32_t SUBI_Imm(uint32_t opcode)
{
    return (extract32(opcode, 8, 4) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_ANDI(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ANDI_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

static inline uint32_t ANDI_Imm(uint32_t opcode)
{
    return (extract32(opcode, 8, 4) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_LDDZ(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDDZ_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t LDDZ_Imm(uint32_t opcode)
{
    return (extract32(opcode, 13, 1) << 5) |
            (extract32(opcode, 10, 2) << 3) |
            (extract32(opcode, 0, 3));
}

int avr_translate_LDDY(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDDY_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t LDDY_Imm(uint32_t opcode)
{
    return (extract32(opcode, 13, 1) << 5) |
            (extract32(opcode, 10, 2) << 3) |
            (extract32(opcode, 0, 3));
}

int avr_translate_STDZ(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STDZ_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t STDZ_Imm(uint32_t opcode)
{
    return (extract32(opcode, 13, 1) << 5) |
            (extract32(opcode, 10, 2) << 3) |
            (extract32(opcode, 0, 3));
}

int avr_translate_STDY(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STDY_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t STDY_Imm(uint32_t opcode)
{
    return (extract32(opcode, 13, 1) << 5) |
            (extract32(opcode, 10, 2) << 3) |
            (extract32(opcode, 0, 3));
}

int avr_translate_LDS(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDS_Imm(uint32_t opcode)
{
    return extract32(opcode, 0, 16);
}

static inline uint32_t LDS_Rd(uint32_t opcode)
{
    return extract32(opcode, 20, 5);
}

int avr_translate_LDZ2(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDZ2_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LDZ3(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDZ3_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LPM2(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LPM2_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LPMX(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LPMX_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_ELPM2(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ELPM2_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_ELPMX(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ELPMX_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LDY2(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDY2_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LDY3(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDY3_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LDX1(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDX1_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LDX2(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDX2_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LDX3(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDX3_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_POP(DisasContext* ctx, uint32_t opcode);
static inline uint32_t POP_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_STS(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STS_Imm(uint32_t opcode)
{
    return extract32(opcode, 0, 16);
}

static inline uint32_t STS_Rd(uint32_t opcode)
{
    return extract32(opcode, 20, 5);
}

int avr_translate_STZ2(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STZ2_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_STZ3(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STZ3_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_XCH(DisasContext* ctx, uint32_t opcode);
static inline uint32_t XCH_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LAS(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LAS_Rr(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LAC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LAC_Rr(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LAT(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LAT_Rr(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_STY2(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STY2_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_STY3(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STY3_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_STX1(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STX1_Rr(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_STX2(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STX2_Rr(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_STX3(DisasContext* ctx, uint32_t opcode);
static inline uint32_t STX3_Rr(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_PUSH(DisasContext* ctx, uint32_t opcode);
static inline uint32_t PUSH_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_COM(DisasContext* ctx, uint32_t opcode);
static inline uint32_t COM_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_NEG(DisasContext* ctx, uint32_t opcode);
static inline uint32_t NEG_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_SWAP(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SWAP_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_INC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t INC_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_ASR(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ASR_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_LSR(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LSR_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_ROR(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ROR_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_BSET(DisasContext* ctx, uint32_t opcode);
static inline uint32_t BSET_Bit(uint32_t opcode)
{
    return extract32(opcode, 4, 3);
}

int avr_translate_IJMP(DisasContext* ctx, uint32_t opcode);

int avr_translate_EIJMP(DisasContext* ctx, uint32_t opcode);

int avr_translate_BCLR(DisasContext* ctx, uint32_t opcode);
static inline uint32_t BCLR_Bit(uint32_t opcode)
{
    return extract32(opcode, 4, 3);
}

int avr_translate_RET(DisasContext* ctx, uint32_t opcode);

int avr_translate_RETI(DisasContext* ctx, uint32_t opcode);

int avr_translate_ICALL(DisasContext* ctx, uint32_t opcode);

int avr_translate_EICALL(DisasContext* ctx, uint32_t opcode);

int avr_translate_SLEEP(DisasContext* ctx, uint32_t opcode);

int avr_translate_BREAK(DisasContext* ctx, uint32_t opcode);

int avr_translate_WDR(DisasContext* ctx, uint32_t opcode);

int avr_translate_LPM1(DisasContext* ctx, uint32_t opcode);

int avr_translate_ELPM1(DisasContext* ctx, uint32_t opcode);

int avr_translate_SPM(DisasContext* ctx, uint32_t opcode);

int avr_translate_SPMX(DisasContext* ctx, uint32_t opcode);

int avr_translate_DEC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t DEC_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_DES(DisasContext* ctx, uint32_t opcode);
static inline uint32_t DES_Imm(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

int avr_translate_JMP(DisasContext* ctx, uint32_t opcode);
static inline uint32_t JMP_Imm(uint32_t opcode)
{
    return (extract32(opcode, 20, 5) << 17) |
            (extract32(opcode, 0, 17));
}

int avr_translate_CALL(DisasContext* ctx, uint32_t opcode);
static inline uint32_t CALL_Imm(uint32_t opcode)
{
    return (extract32(opcode, 20, 5) << 17) |
            (extract32(opcode, 0, 17));
}

int avr_translate_ADIW(DisasContext* ctx, uint32_t opcode);
static inline uint32_t ADIW_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 2);
}

static inline uint32_t ADIW_Imm(uint32_t opcode)
{
    return (extract32(opcode, 6, 2) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_SBIW(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SBIW_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 2);
}

static inline uint32_t SBIW_Imm(uint32_t opcode)
{
    return (extract32(opcode, 6, 2) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_CBI(DisasContext* ctx, uint32_t opcode);
static inline uint32_t CBI_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t CBI_Imm(uint32_t opcode)
{
    return extract32(opcode, 3, 5);
}

int avr_translate_SBIC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SBIC_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t SBIC_Imm(uint32_t opcode)
{
    return extract32(opcode, 3, 5);
}

int avr_translate_SBI(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SBI_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t SBI_Imm(uint32_t opcode)
{
    return extract32(opcode, 3, 5);
}

int avr_translate_SBIS(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SBIS_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t SBIS_Imm(uint32_t opcode)
{
    return extract32(opcode, 3, 5);
}

int avr_translate_MUL(DisasContext* ctx, uint32_t opcode);
static inline uint32_t MUL_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t MUL_Rr(uint32_t opcode)
{
    return (extract32(opcode, 9, 1) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_IN(DisasContext* ctx, uint32_t opcode);
static inline uint32_t IN_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t IN_Imm(uint32_t opcode)
{
    return (extract32(opcode, 9, 2) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_OUT(DisasContext* ctx, uint32_t opcode);
static inline uint32_t OUT_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

static inline uint32_t OUT_Imm(uint32_t opcode)
{
    return (extract32(opcode, 9, 2) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_RJMP(DisasContext* ctx, uint32_t opcode);
static inline uint32_t RJMP_Imm(uint32_t opcode)
{
    return extract32(opcode, 0, 12);
}

int avr_translate_LDI(DisasContext* ctx, uint32_t opcode);
static inline uint32_t LDI_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 4);
}

static inline uint32_t LDI_Imm(uint32_t opcode)
{
    return (extract32(opcode, 8, 4) << 4) |
            (extract32(opcode, 0, 4));
}

int avr_translate_RCALL(DisasContext* ctx, uint32_t opcode);
static inline uint32_t RCALL_Imm(uint32_t opcode)
{
    return extract32(opcode, 0, 12);
}

int avr_translate_BRBS(DisasContext* ctx, uint32_t opcode);
static inline uint32_t BRBS_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t BRBS_Imm(uint32_t opcode)
{
    return extract32(opcode, 3, 7);
}

int avr_translate_BRBC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t BRBC_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t BRBC_Imm(uint32_t opcode)
{
    return extract32(opcode, 3, 7);
}

int avr_translate_BLD(DisasContext* ctx, uint32_t opcode);
static inline uint32_t BLD_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t BLD_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_BST(DisasContext* ctx, uint32_t opcode);
static inline uint32_t BST_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t BST_Rd(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_SBRC(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SBRC_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t SBRC_Rr(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

int avr_translate_SBRS(DisasContext* ctx, uint32_t opcode);
static inline uint32_t SBRS_Bit(uint32_t opcode)
{
    return extract32(opcode, 0, 3);
}

static inline uint32_t SBRS_Rr(uint32_t opcode)
{
    return extract32(opcode, 4, 5);
}

#endif
