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

#include "translate.h"
#include "translate-inst.h"
#include "qemu/bitops.h"

static void gen_add_CHf(TCGv R, TCGv Rd, TCGv Rr);
static void gen_add_Vf(TCGv R, TCGv Rd, TCGv Rr);
static void gen_sub_CHf(TCGv R, TCGv Rd, TCGv Rr);
static void gen_sub_Vf(TCGv R, TCGv Rd, TCGv Rr);
static void gen_NSf(TCGv R);
static void gen_ZNSf(TCGv R);
static void gen_push_ret(CPUAVRState *env, int ret);
static void gen_pop_ret(CPUAVRState *env, TCGv ret);
static void gen_jmp_ez(void);
static void gen_jmp_z(void);
static void gen_data_store(CPUAVRState *env, TCGv data, TCGv addr);

/*
    in the following 2 functions
    H assumed to be in 0x00ff0000 format
    M assumed to be in 0x000000ff format
    L assumed to be in 0x000000ff format
*/
static void gen_set_addr(TCGv addr, TCGv H, TCGv M, TCGv l); /* H:M:L = addr */
static TCGv gen_get_addr(TCGv H, TCGv M, TCGv L);            /* addr = H:M:L */

static void gen_set_xaddr(TCGv addr);
static void gen_set_yaddr(TCGv addr);
static void gen_set_zaddr(TCGv addr);

static TCGv gen_get_xaddr(void);
static TCGv gen_get_yaddr(void);
static TCGv gen_get_zaddr(void);

void gen_add_CHf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_and_tl(t1, Rd, Rr);         /*  t1 = Rd & Rr  */
    tcg_gen_andc_tl(t2, Rd, R);         /*  t2 = Rd & ~R  */
    tcg_gen_andc_tl(t3, Rr, R);         /*  t3 = Rr & ~R  */
    tcg_gen_or_tl(t1, t1, t2);          /*  t1 = t1 | t2 | t3  */
    tcg_gen_or_tl(t1, t1, t3);

    tcg_gen_shri_tl(cpu_Cf, t1, 7);     /*  Cf = t1(7)  */
    tcg_gen_shri_tl(cpu_Hf, t1, 3);     /*  Hf = t1(3)  */
    tcg_gen_andi_tl(cpu_Hf, cpu_Hf, 1);

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

void gen_add_Vf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

        /*  t1 = Rd & Rr & ~R | ~Rd & ~Rr & R = (Rd ^ R) & ~(Rd ^ Rr)   */
    tcg_gen_xor_tl(t1, Rd, R);
    tcg_gen_xor_tl(t2, Rd, Rr);
    tcg_gen_andc_tl(t1, t1, t2);

    tcg_gen_shri_tl(cpu_Vf, t1, 7);     /*  Vf = t1(7)  */

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

void gen_sub_CHf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    /*  Cf & Hf  */
    tcg_gen_not_tl(t1, Rd);             /*  t1 = ~Rd  */
    tcg_gen_and_tl(t2, t1, Rr);         /*  t2 = ~Rd & Rr  */
    tcg_gen_or_tl(t3, t1, Rr);          /*  t3 = (~Rd | Rr) & R  */
    tcg_gen_and_tl(t3, t3, R);
    tcg_gen_or_tl(t2, t2, t3);          /*  t2 = ~Rd & Rr | ~Rd & R | R & Rr  */
    tcg_gen_shri_tl(cpu_Cf, t2, 7);     /*  Cf = t2(7)  */
    tcg_gen_shri_tl(cpu_Hf, t2, 3);     /*  Hf = t2(3)  */
    tcg_gen_andi_tl(cpu_Hf, cpu_Hf, 1);

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

void gen_sub_Vf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    /*  Vf  */
        /*  t1 = Rd & ~Rr & ~R | ~Rd & Rr & R  = (Rd ^ R) & (Rd ^ R)*/
    tcg_gen_xor_tl(t1, Rd, R);
    tcg_gen_xor_tl(t2, Rd, Rr);
    tcg_gen_and_tl(t1, t1, t2);
    tcg_gen_shri_tl(cpu_Vf, t1, 7);     /*  Vf = t1(7)  */

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

void gen_NSf(TCGv R)
{
    tcg_gen_shri_tl(cpu_Nf, R, 7);          /*  Nf = R(7)  */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /*  Sf = Nf ^ Vf  */
}
void gen_ZNSf(TCGv R)
{
    tcg_gen_mov_tl(cpu_Zf, R);              /*  Zf = R  */
    tcg_gen_shri_tl(cpu_Nf, R, 7);          /*  Nf = R(7)  */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /*  Sf = Nf ^ Vf  */
}

void gen_push_ret(CPUAVRState *env, int ret)
{
    if (avr_feature(env, AVR_FEATURE_1_BYTE_PC)) {

        TCGv t0 = tcg_const_i32((ret & 0x0000ff));

        tcg_gen_qemu_st_tl(t0, cpu_sp, MMU_DATA_IDX, MO_UB);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(t0);
    } else if (avr_feature(env, AVR_FEATURE_2_BYTE_PC)) {

        TCGv t0 = tcg_const_i32((ret & 0x00ffff));

        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_st_tl(t0, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(t0);

    } else if (avr_feature(env, AVR_FEATURE_3_BYTE_PC)) {

        TCGv lo = tcg_const_i32((ret & 0x0000ff));
        TCGv hi = tcg_const_i32((ret & 0xffff00) >> 8);

        tcg_gen_qemu_st_tl(lo, cpu_sp, MMU_DATA_IDX, MO_UB);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 2);
        tcg_gen_qemu_st_tl(hi, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(lo);
        tcg_temp_free_i32(hi);
    }
}

void gen_pop_ret(CPUAVRState *env, TCGv ret)
{
    if (avr_feature(env, AVR_FEATURE_1_BYTE_PC)) {

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(ret, cpu_sp, MMU_DATA_IDX, MO_UB);

    } else if (avr_feature(env, AVR_FEATURE_2_BYTE_PC)) {

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(ret, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);

    } else if (avr_feature(env, AVR_FEATURE_3_BYTE_PC)) {

        TCGv lo = tcg_temp_new_i32();
        TCGv hi = tcg_temp_new_i32();

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(hi, cpu_sp, MMU_DATA_IDX, MO_BEUW);

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 2);
        tcg_gen_qemu_ld_tl(lo, cpu_sp, MMU_DATA_IDX, MO_UB);

        tcg_gen_deposit_tl(ret, lo, hi, 8, 16);

        tcg_temp_free_i32(lo);
        tcg_temp_free_i32(hi);
    }
}

void gen_jmp_ez(void)
{
    tcg_gen_deposit_tl(cpu_pc, cpu_r[30], cpu_r[31], 8, 8);
    tcg_gen_or_tl(cpu_pc, cpu_pc, cpu_eind);
    tcg_gen_exit_tb(0);
}

void gen_jmp_z(void)
{
    tcg_gen_deposit_tl(cpu_pc, cpu_r[30], cpu_r[31], 8, 8);
    tcg_gen_exit_tb(0);
}

void gen_set_addr(TCGv addr, TCGv H, TCGv M, TCGv L)
{

    tcg_gen_andi_tl(L, addr, 0x000000ff);

    tcg_gen_andi_tl(M, addr, 0x0000ff00);
    tcg_gen_shri_tl(M, M, 8);

    tcg_gen_andi_tl(H, addr, 0x00ff0000);
}

void gen_set_xaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampX, cpu_r[27], cpu_r[26]);
}

void gen_set_yaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampY, cpu_r[29], cpu_r[28]);
}

void gen_set_zaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampZ, cpu_r[31], cpu_r[30]);
}

TCGv gen_get_addr(TCGv H, TCGv M, TCGv L)
{
    TCGv addr = tcg_temp_new_i32();

    tcg_gen_deposit_tl(addr, M, H, 8, 8);
    tcg_gen_deposit_tl(addr, L, addr, 8, 16);

    return  addr;
}

TCGv gen_get_xaddr(void)
{
    return  gen_get_addr(cpu_rampX, cpu_r[27], cpu_r[26]);
}

TCGv gen_get_yaddr(void)
{
    return  gen_get_addr(cpu_rampY, cpu_r[29], cpu_r[28]);
}

TCGv gen_get_zaddr(void)
{
    return  gen_get_addr(cpu_rampZ, cpu_r[31], cpu_r[30]);
}

/*
    Adds two registers and the contents of the C Flag and places the result in
    the destination register Rd.
*/
int avr_translate_ADC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[ADC_Rd(opcode)];
    TCGv Rr = cpu_r[ADC_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_add_tl(R, Rd, Rr);             /*  R = Rd + Rr + Cf  */
    tcg_gen_add_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff);            /*  make it 8 bits  */

    gen_add_CHf(R, Rd, Rr);
    gen_add_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /*  R  */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Adds two registers without the C Flag and places the result in the
    destination register Rd.
*/
int avr_translate_ADD(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[ADD_Rd(opcode)];
    TCGv Rr = cpu_r[ADD_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_add_tl(R, Rd, Rr);             /*  Rd = Rd + Rr  */
    tcg_gen_andi_tl(R, R, 0xff);            /*  make it 8 bits  */

    gen_add_CHf(R, Rd, Rr);
    gen_add_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /*  R  */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Adds an immediate value (0 - 63) to a register pair and places the result
    in the register pair. This instruction operates on the upper four register
    pairs, and is well suited for operations on the pointer registers.  This
    instruction is not available in all devices. Refer to the device specific
    instruction set summary.
*/
int avr_translate_ADIW(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_ADIW_SBIW) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv RdL = cpu_r[24 + 2 * ADIW_Rd(opcode)];
    TCGv RdH = cpu_r[25 + 2 * ADIW_Rd(opcode)];
    int Imm = (ADIW_Imm(opcode));
    TCGv R = tcg_temp_new_i32();
    TCGv Rd = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_deposit_tl(Rd, RdL, RdH, 8, 8); /*  Rd  = RdH:RdL   */
    tcg_gen_addi_tl(R, Rd, Imm);            /*  R   = Rd + Imm  */
    tcg_gen_andi_tl(R, R, 0xffff);          /*  make it 16 bits */

    /*  Cf  */
    tcg_gen_andc_tl(cpu_Cf, Rd, R);         /*  Cf  = Rd & ~R   */
    tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 15);

    /*  Vf  */
    tcg_gen_andc_tl(cpu_Vf, R, Rd);         /*  Vf  = R & ~Rd   */
    tcg_gen_shri_tl(cpu_Vf, cpu_Vf, 15);

    /*  Zf  */
    tcg_gen_mov_tl(cpu_Zf, R);              /*  Zf = R          */

    /*  Nf  */
    tcg_gen_shri_tl(cpu_Nf, R, 15);         /*  Nf = R(15)      */

    /*  Sf  */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);/*  Sf = Nf ^ Vf     */

    /*  R  */
    tcg_gen_andi_tl(RdL, R, 0xff);
    tcg_gen_shri_tl(RdH, R, 8);

    tcg_temp_free_i32(Rd);
    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Performs the logical AND between the contents of register Rd and register
    Rr and places the result in the destination register Rd.
*/
int avr_translate_AND(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[AND_Rd(opcode)];
    TCGv Rr = cpu_r[AND_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_and_tl(R, Rd, Rr);             /*  Rd = Rd and Rr  */

    /*  Vf  */
    tcg_gen_movi_tl(cpu_Vf, 0x00);          /*  Vf = 0  */

    /*  Zf  */
    tcg_gen_mov_tl(cpu_Zf, R);             /*  Zf = R  */

    gen_ZNSf(R);

    /*  R  */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Performs the logical AND between the contents of register Rd and a constant
    and places the result in the destination register Rd.
*/
int avr_translate_ANDI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[ANDI_Rd(opcode) + 16];
    int Imm = (ANDI_Imm(opcode));

    /*  op  */
    tcg_gen_andi_tl(Rd, Rd, Imm);   /*  Rd = Rd & Imm  */

    tcg_gen_movi_tl(cpu_Vf, 0x00);          /*  Vf = 0  */
    gen_ZNSf(Rd);

    return  BS_NONE;
}

/*
    Shifts all bits in Rd one place to the right. Bit 7 is held constant. Bit 0
    is loaded into the C Flag of the SREG. This operation effectively divides a
    signed value by two without changing its sign. The Carry Flag can be used to
    round the result.
*/
int avr_translate_ASR(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[ASR_Rd(opcode)];
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_andi_tl(t1, Rd, 0x80);      /*  t1 = (Rd & 0x80) | (Rd >> 1)  */
    tcg_gen_shri_tl(t2, Rd, 1);
    tcg_gen_or_tl(t1, t1, t2);

    /*  Cf  */
    tcg_gen_andi_tl(cpu_Cf, Rd, 1);         /*  Cf = Rd(0)  */

    /*  Vf  */
    tcg_gen_and_tl(cpu_Vf, cpu_Nf, cpu_Cf);/*  Vf = Nf & Cf  */

    gen_ZNSf(t1);

    /*  op  */
    tcg_gen_mov_tl(Rd, t1);

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);

    return  BS_NONE;
}

/*
    Clears a single Flag in SREG.
*/
int avr_translate_BCLR(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    switch (BCLR_Bit(opcode)) {
        case    0x00: {
            tcg_gen_movi_tl(cpu_Cf, 0x00);
            break;
        }
        case    0x01: {
            tcg_gen_movi_tl(cpu_Zf, 0x01);
            break;
        }
        case    0x02: {
            tcg_gen_movi_tl(cpu_Nf, 0x00);
            break;
        }
        case    0x03: {
            tcg_gen_movi_tl(cpu_Vf, 0x00);
            break;
        }
        case    0x04: {
            tcg_gen_movi_tl(cpu_Sf, 0x00);
            break;
        }
        case    0x05: {
            tcg_gen_movi_tl(cpu_Hf, 0x00);
            break;
        }
        case    0x06: {
            tcg_gen_movi_tl(cpu_Tf, 0x00);
            break;
        }
        case    0x07: {
            tcg_gen_movi_tl(cpu_If, 0x00);
            break;
        }
    }

    return  BS_NONE;
}

/*
    Copies the T Flag in the SREG (Status Register) to bit b in register Rd.
*/
int avr_translate_BLD(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[BLD_Rd(opcode)];
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_andi_tl(Rd, Rd, ~(1u << BLD_Bit(opcode)));  /*  clear bit   */
    tcg_gen_shli_tl(t1, cpu_Tf, BLD_Bit(opcode));       /*  create mask */
    tcg_gen_or_tl(Rd, Rd, t1);

    tcg_temp_free_i32(t1);

    return  BS_NONE;
}

/*
    Conditional relative branch. Tests a single bit in SREG and branches
    relatively to PC if the bit is cleared. This instruction branches relatively
    to PC in either direction (PC - 63 < = destination <= PC + 64). The
    parameter k is the offset from PC and is represented in two’s complement
    form.
*/
int avr_translate_BRBC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGLabel   *taken = gen_new_label();
    int Imm = sextract32(BRBC_Imm(opcode), 0, 7);

    switch (BRBC_Bit(opcode)) {
        case    0x00: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Cf, 0, taken);
            break;
        }
        case    0x01: {
            tcg_gen_brcondi_i32(TCG_COND_NE, cpu_Zf, 0, taken);
            break;
        }
        case    0x02: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Nf, 0, taken);
            break;
        }
        case    0x03: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Vf, 0, taken);
            break;
        }
        case    0x04: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Sf, 0, taken);
            break;
        }
        case    0x05: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Hf, 0, taken);
            break;
        }
        case    0x06: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Tf, 0, taken);
            break;
        }
        case    0x07: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_If, 0, taken);
            break;
        }
    }

    gen_goto_tb(env, ctx, 1, ctx->inst[0].npc);
    gen_set_label(taken);
    gen_goto_tb(env, ctx, 0, ctx->inst[0].npc + Imm);

    return BS_BRANCH;
}

/*
    Conditional relative branch. Tests a single bit in SREG and branches
    relatively to PC if the bit is set. This instruction branches relatively to
    PC in either direction (PC - 63 < = destination <= PC + 64). The parameter k
    is the offset from PC and is represented in two’s complement form.
*/
int avr_translate_BRBS(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGLabel   *taken = gen_new_label();
    int Imm = sextract32(BRBS_Imm(opcode), 0, 7);

    switch (BRBS_Bit(opcode)) {
        case    0x00: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Cf, 1, taken);
            break;
        }
        case    0x01: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Zf, 0, taken);
            break;
        }
        case    0x02: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Nf, 1, taken);
            break;
        }
        case    0x03: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Vf, 1, taken);
            break;
        }
        case    0x04: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Sf, 1, taken);
            break;
        }
        case    0x05: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Hf, 1, taken);
            break;
        }
        case    0x06: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_Tf, 1, taken);
            break;
        }
        case    0x07: {
            tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_If, 1, taken);
            break;
        }
    }

    gen_goto_tb(env, ctx, 1, ctx->inst[0].npc);
    gen_set_label(taken);
    gen_goto_tb(env, ctx, 0, ctx->inst[0].npc + Imm);

    return BS_BRANCH;
}

/*
    Sets a single Flag or bit in SREG.
*/
int avr_translate_BSET(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    switch (BSET_Bit(opcode)) {
        case    0x00: {
            tcg_gen_movi_tl(cpu_Cf, 0x01);
            break;
        }
        case    0x01: {
            tcg_gen_movi_tl(cpu_Zf, 0x00);
            break;
        }
        case    0x02: {
            tcg_gen_movi_tl(cpu_Nf, 0x01);
            break;
        }
        case    0x03: {
            tcg_gen_movi_tl(cpu_Vf, 0x01);
            break;
        }
        case    0x04: {
            tcg_gen_movi_tl(cpu_Sf, 0x01);
            break;
        }
        case    0x05: {
            tcg_gen_movi_tl(cpu_Hf, 0x01);
            break;
        }
        case    0x06: {
            tcg_gen_movi_tl(cpu_Tf, 0x01);
            break;
        }
        case    0x07: {
            tcg_gen_movi_tl(cpu_If, 0x01);
            break;
        }
    }

    return  BS_NONE;
}

/*
    The BREAK instruction is used by the On-chip Debug system, and is
    normally not used in the application software. When the BREAK instruction is
    executed, the AVR CPU is set in the Stopped Mode. This gives the On-chip
    Debugger access to internal resources.  If any Lock bits are set, or either
    the JTAGEN or OCDEN Fuses are unprogrammed, the CPU will treat the BREAK
    instruction as a NOP and will not enter the Stopped mode.  This instruction
    is not available in all devices. Refer to the device specific instruction
    set summary.
*/
int avr_translate_BREAK(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_BREAK) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    /*  TODO:   ???  */
    return  BS_NONE;
}

/*
    Stores bit b from Rd to the T Flag in SREG (Status Register).
*/
int avr_translate_BST(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[BST_Rd(opcode)];

    tcg_gen_andi_tl(cpu_Tf, Rd, 1 << BST_Bit(opcode));
    tcg_gen_shri_tl(cpu_Tf, cpu_Tf, BST_Bit(opcode));

    return  BS_NONE;
}

/*
    Calls to a subroutine within the entire Program memory. The return
    address (to the instruction after the CALL) will be stored onto the Stack.
    (See also RCALL). The Stack Pointer uses a post-decrement scheme during
    CALL.  This instruction is not available in all devices. Refer to the device
    specific instruction set summary.
*/
int avr_translate_CALL(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_JMP_CALL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    int Imm = CALL_Imm(opcode);
    int ret = ctx->inst[0].npc;

    gen_push_ret(env, ret);

    gen_goto_tb(env, ctx, 0, Imm);

    return  BS_BRANCH;
}

/*
    Clears a specified bit in an I/O Register. This instruction operates on
    the lower 32 I/O Registers – addresses 0-31.  */
int avr_translate_CBI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv data = cpu_io[CBI_Imm(opcode)];
    TCGv port = tcg_const_i32(CBI_Imm(opcode));

    tcg_gen_andi_tl(data, data, ~(1 << CBI_Bit(opcode)));
    gen_helper_outb(cpu_env, port, data);

    return  BS_NONE;
}

/*
    Clears the specified bits in register Rd. Performs the logical AND
    between the contents of register Rd and the complement of the constant mask
    K. The result will be placed in register Rd.
*/
int avr_translate_COM(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[COM_Rd(opcode)];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_xori_tl(Rd, Rd, 0xff);

    tcg_gen_movi_tl(cpu_Cf, 1);         /*  Cf = 1  */
    tcg_gen_movi_tl(cpu_Vf, 0);         /*  Vf = 0  */
    gen_ZNSf(Rd);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs a compare between two registers Rd and Rr.
    None of the registers are changed. All conditional branches can be used
    after this instruction.
*/
int avr_translate_CP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[CP_Rd(opcode)];
    TCGv Rr = cpu_r[CP_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_sub_tl(R, Rd, Rr);    /*  R = Rd - Rr  */
    tcg_gen_andi_tl(R, R, 0xff);  /*  make it 8 bits  */

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs a compare between two registers Rd and Rr and
    also takes into account the previous carry. None of the registers are
    changed. All conditional branches can be used after this instruction.
*/
int avr_translate_CPC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[CPC_Rd(opcode)];
    TCGv Rr = cpu_r[CPC_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_sub_tl(R, Rd, Rr);      /*  R = Rd - Rr - Cf    */
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff);    /*  make it 8 bits      */

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    /*  Previous value remains unchanged when the result is zero;
        cleared otherwise.
    */
    tcg_gen_or_tl(cpu_Zf, cpu_Zf, R);


    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs a compare between register Rd and a constant.
    The register is not changed. All conditional branches can be used after this
    instruction.
*/
int avr_translate_CPI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[16 + CPI_Rd(opcode)];
    int Imm = CPI_Imm(opcode);
    TCGv Rr = tcg_const_i32(Imm);
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_sub_tl(R, Rd, Rr);    /*  R = Rd - Rr  */
    tcg_gen_andi_tl(R, R, 0xff);  /*  make it 8 bits  */

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs a compare between two registers Rd and Rr, and
    skips the next instruction if Rd = Rr.
*/
int avr_translate_CPSE(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[CPSE_Rd(opcode)];
    TCGv Rr = cpu_r[CPSE_Rr(opcode)];
    TCGLabel *skip = gen_new_label();

        /*  PC if next inst is skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[1].npc);
    tcg_gen_brcond_i32(TCG_COND_EQ, Rd, Rr, skip);
        /*  PC if next inst is not skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[0].npc);
    gen_set_label(skip);

    return  BS_BRANCH;
}

/*
    Subtracts one -1- from the contents of register Rd and places the result
    in the destination register Rd.  The C Flag in SREG is not affected by the
    operation, thus allowing the DEC instruction to be used on a loop counter in
    multiple-precision computations.  When operating on unsigned values, only
    BREQ and BRNE branches can be expected to perform consistently.  When
    operating on two’s complement values, all signed branches are available.
*/
int avr_translate_DEC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[DEC_Rd(opcode)];

    tcg_gen_subi_tl(Rd, Rd, 1);     /*  Rd = Rd - 1  */
    tcg_gen_andi_tl(Rd, Rd, 0xff);  /*  make it 8 bits  */

        /* cpu_Vf = Rd == 0x7f  */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Vf, Rd, 0x7f);
    gen_ZNSf(Rd);

    return  BS_NONE;
}

/*

    The module is an instruction set extension to the AVR CPU, performing
    DES iterations. The 64-bit data block (plaintext or ciphertext) is placed in
    the CPU register file, registers R0-R7, where LSB of data is placed in LSB
    of R0 and MSB of data is placed in MSB of R7. The full 64-bit key (including
    parity bits) is placed in registers R8- R15, organized in the register file
    with LSB of key in LSB of R8 and MSB of key in MSB of R15. Executing one DES
    instruction performs one round in the DES algorithm. Sixteen rounds must be
    executed in increasing order to form the correct DES ciphertext or
    plaintext. Intermediate results are stored in the register file (R0-R15)
    after each DES instruction. The instruction's operand (K) determines which
    round is executed, and the half carry flag (H) determines whether encryption
    or decryption is performed.  The DES algorithm is described in
    “Specifications for the Data Encryption Standard” (Federal Information
    Processing Standards Publication 46). Intermediate results in this
    implementation differ from the standard because the initial permutation and
    the inverse initial permutation are performed each iteration. This does not
    affect the result in the final ciphertext or plaintext, but reduces
    execution time.
*/
int avr_translate_DES(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    /*  TODO:  */
    if (avr_feature(env, AVR_FEATURE_DES) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    return  BS_NONE;
}

/*
    Indirect call of a subroutine pointed to by the Z (16 bits) Pointer
    Register in the Register File and the EIND Register in the I/O space. This
    instruction allows for indirect calls to the entire 4M (words) Program
    memory space. See also ICALL. The Stack Pointer uses a post-decrement scheme
    during EICALL.  This instruction is not available in all devices. Refer to
    the device specific instruction set summary.
*/
int avr_translate_EICALL(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_EIJMP_EICALL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    int ret = ctx->inst[0].npc;

    gen_push_ret(env, ret);

    gen_jmp_ez();

    return BS_BRANCH;
}

/*
    Indirect jump to the address pointed to by the Z (16 bits) Pointer
    Register in the Register File and the EIND Register in the I/O space. This
    instruction allows for indirect jumps to the entire 4M (words) Program
    memory space. See also IJMP.  This instruction is not available in all
    devices. Refer to the device specific instruction set summary.
*/
int avr_translate_EIJMP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_EIJMP_EICALL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    gen_jmp_ez();

    return BS_BRANCH;
}

/*
    Loads one byte pointed to by the Z-register and the RAMPZ Register in
    the I/O space, and places this byte in the destination register Rd. This
    instruction features a 100% space effective constant initialization or
    constant data fetch. The Program memory is organized in 16-bit words while
    the Z-pointer is a byte address. Thus, the least significant bit of the
    Z-pointer selects either low byte (ZLSB = 0) or high byte (ZLSB = 1). This
    instruction can address the entire Program memory space. The Z-pointer
    Register can either be left unchanged by the operation, or it can be
    incremented. The incrementation applies to the entire 24-bit concatenation
    of the RAMPZ and Z-pointer Registers.  Devices with Self-Programming
    capability can use the ELPM instruction to read the Fuse and Lock bit value.
    Refer to the device documentation for a detailed description.  This
    instruction is not available in all devices. Refer to the device specific
    instruction set summary.
*/
int avr_translate_ELPM1(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_ELPM) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rd = cpu_r[0];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX);    /*  Rd = mem[addr]  */

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_ELPM2(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_ELPM) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rd = cpu_r[ELPM2_Rd(opcode)];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX);    /*  Rd = mem[addr]  */

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_ELPMX(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_ELPMX) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rd = cpu_r[ELPMX_Rd(opcode)];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX);    /*  Rd = mem[addr]  */

    tcg_gen_addi_tl(addr, addr, 1);             /*  addr = addr + 1  */

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Performs the logical EOR between the contents of register Rd and
    register Rr and places the result in the destination register Rd.
*/
int avr_translate_EOR(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[EOR_Rd(opcode)];
    TCGv Rr = cpu_r[EOR_Rr(opcode)];

    tcg_gen_xor_tl(Rd, Rd, Rr);

    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(Rd);

    return  BS_NONE;
}

/*
    This instruction performs 8-bit x 8-bit -> 16-bit unsigned
    multiplication and shifts the result one bit left.
*/
int avr_translate_FMUL(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_MUL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[16 + FMUL_Rd(opcode)];
    TCGv Rr = cpu_r[16 + FMUL_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_mul_tl(R, Rd, Rr);    /*  R = Rd  *Rr  */
    tcg_gen_shli_tl(R, R, 1);

    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R, R, 8);
    tcg_gen_andi_tl(R1, R, 0xff);

    tcg_gen_shri_tl(cpu_Cf, R, 16);    /*  Cf = R(16)  */
    tcg_gen_andi_tl(cpu_Zf, R, 0x0000ffff);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication
    and shifts the result one bit left.
*/
int avr_translate_FMULS(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_MUL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[16 + FMULS_Rd(opcode)];
    TCGv Rr = cpu_r[16 + FMULS_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd);           /*  make Rd full 32 bit signed  */
    tcg_gen_ext8s_tl(t1, Rr);           /*  make Rr full 32 bit signed  */
    tcg_gen_mul_tl(R, t0, t1);          /*  R = Rd  *Rr  */
    tcg_gen_shli_tl(R, R, 1);

    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R, R, 8);
    tcg_gen_andi_tl(R1, R, 0xff);

    tcg_gen_shri_tl(cpu_Cf, R, 16);    /*  Cf = R(16)  */
    tcg_gen_andi_tl(cpu_Zf, R, 0x0000ffff);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication
    and shifts the result one bit left.
*/
int avr_translate_FMULSU(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_MUL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[16 + FMULSU_Rd(opcode)];
    TCGv Rr = cpu_r[16 + FMULSU_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd);           /*  make Rd full 32 bit signed  */
    tcg_gen_mul_tl(R, t0, Rr);          /*  R = Rd  *Rr  */
    tcg_gen_shli_tl(R, R, 1);

    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R, R, 8);
    tcg_gen_andi_tl(R1, R, 0xff);

    tcg_gen_shri_tl(cpu_Cf, R, 16);     /*  Cf = R(16)  */
    tcg_gen_andi_tl(cpu_Zf, R, 0x0000ffff);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Calls to a subroutine within the entire 4M (words) Program memory. The
    return address (to the instruction after the CALL) will be stored onto the
    Stack. See also RCALL. The Stack Pointer uses a post-decrement scheme during
    CALL.  This instruction is not available in all devices. Refer to the device
    specific instruction set summary.
*/
int avr_translate_ICALL(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_IJMP_ICALL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    int ret = ctx->inst[0].npc;

    gen_push_ret(env, ret);
    gen_jmp_z();

    return BS_BRANCH;
}

/*
    Indirect jump to the address pointed to by the Z (16 bits) Pointer
    Register in the Register File. The Z-pointer Register is 16 bits wide and
    allows jump within the lowest 64K words (128KB) section of Program memory.
    This instruction is not available in all devices. Refer to the device
    specific instruction set summary.
*/
int avr_translate_IJMP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_IJMP_ICALL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    gen_jmp_z();

    return BS_BRANCH;
}

/*
    Loads data from the I/O Space (Ports, Timers, Configuration Registers,
    etc.) into register Rd in the Register File.
*/
int avr_translate_IN(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[IN_Rd(opcode)];
    int Imm = IN_Imm(opcode);
    TCGv port = tcg_const_i32(Imm);
    TCGv data = cpu_io[Imm];

    gen_helper_inb(data, cpu_env, port);
    tcg_gen_mov_tl(Rd, data);

    return  BS_NONE;
}

/*
    Adds one -1- to the contents of register Rd and places the result in the
    destination register Rd.  The C Flag in SREG is not affected by the
    operation, thus allowing the INC instruction to be used on a loop counter in
    multiple-precision computations.  When operating on unsigned numbers, only
    BREQ and BRNE branches can be expected to perform consistently. When
    operating on two’s complement values, all signed branches are available.
*/
int avr_translate_INC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[INC_Rd(opcode)];

    tcg_gen_addi_tl(Rd, Rd, 1);
    tcg_gen_andi_tl(Rd, Rd, 0xff);

        /* cpu_Vf = Rd == 0x80  */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Vf, Rd, 0x80);
    gen_ZNSf(Rd);
    return  BS_NONE;
}

/*
    Jump to an address within the entire 4M (words) Program memory. See also
    RJMP.  This instruction is not available in all devices. Refer to the device
    specific instruction set summary.0
*/
int avr_translate_JMP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_JMP_CALL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    gen_goto_tb(env, ctx, 0, JMP_Imm(opcode));
    return  BS_BRANCH;
}

/*
    Load one byte indirect from data space to register and stores an clear
    the bits in data space specified by the register. The instruction can only
    be used towards internal SRAM.  The data location is pointed to by the Z (16
    bits) Pointer Register in the Register File. Memory access is limited to the
    current data segment of 64KB. To access another data segment in devices with
    more than 64KB data space, the RAMPZ in register in the I/O area has to be
    changed.  The Z-pointer Register is left unchanged by the operation. This
    instruction is especially suited for clearing status bits stored in SRAM.
*/
void gen_data_store(CPUAVRState *env, TCGv data, TCGv addr)
{
    if (env->fullwr) {
        gen_helper_fullwr(cpu_env, data, addr);
    } else {
        tcg_gen_qemu_st8(data, addr, MMU_DATA_IDX);     /*  mem[addr] = t1  */
    }
}

int avr_translate_LAC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_RMW) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rr = cpu_r[LAC_Rr(opcode)];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_qemu_ld8u(t0, addr, MMU_DATA_IDX);    /*  t0 = mem[addr]  */
        /*  t1 = t0 & (0xff - Rr) = t0 and ~Rr */
    tcg_gen_andc_tl(t1, t0, Rr);

    tcg_gen_mov_tl(Rr, t0);                     /*  Rr = t0  */
    gen_data_store(env, t1, addr);     /*  mem[addr] = t1  */

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Load one byte indirect from data space to register and set bits in data
    space specified by the register. The instruction can only be used towards
    internal SRAM.  The data location is pointed to by the Z (16 bits) Pointer
    Register in the Register File. Memory access is limited to the current data
    segment of 64KB. To access another data segment in devices with more than
    64KB data space, the RAMPZ in register in the I/O area has to be changed.
    The Z-pointer Register is left unchanged by the operation. This instruction
    is especially suited for setting status bits stored in SRAM.
*/
int avr_translate_LAS(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_RMW) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rr = cpu_r[LAS_Rr(opcode)];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_qemu_ld8u(t0, addr, MMU_DATA_IDX);    /*  t0 = mem[addr]  */
    tcg_gen_or_tl(t1, t0, Rr);

    tcg_gen_mov_tl(Rr, t0);                    /*  Rr = t0  */
    gen_data_store(env, t1, addr);    /*  mem[addr] = t1  */

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Load one byte indirect from data space to register and toggles bits in
    the data space specified by the register.  The instruction can only be used
    towards SRAM.  The data location is pointed to by the Z (16 bits) Pointer
    Register in the Register File. Memory access is limited to the current data
    segment of 64KB. To access another data segment in devices with more than
    64KB data space, the RAMPZ in register in the I/O area has to be changed.
    The Z-pointer Register is left unchanged by the operation. This instruction
    is especially suited for changing status bits stored in SRAM.
*/
int avr_translate_LAT(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_RMW) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rr = cpu_r[LAT_Rr(opcode)];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_qemu_ld8u(t0, addr, MMU_DATA_IDX);    /*  t0 = mem[addr]  */
    tcg_gen_xor_tl(t1, t0, Rr);

    tcg_gen_mov_tl(Rr, t0);                    /*  Rr = t0  */
    gen_data_store(env, t1, addr);    /*  mem[addr] = t1  */

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Loads one byte indirect from the data space to a register. For parts
    with SRAM, the data space consists of the Register File, I/O memory and
    internal SRAM (and external SRAM if applicable). For parts without SRAM, the
    data space consists of the Register File only. In some parts the Flash
    Memory has been mapped to the data space and can be read using this command.
    The EEPROM has a separate address space.  The data location is pointed to by
    the X (16 bits) Pointer Register in the Register File. Memory access is
    limited to the current data segment of 64KB. To access another data segment
    in devices with more than 64KB data space, the RAMPX in register in the I/O
    area has to be changed.  The X-pointer Register can either be left unchanged
    by the operation, or it can be post-incremented or predecremented.  These
    features are especially suited for accessing arrays, tables, and Stack
    Pointer usage of the X-pointer Register. Note that only the low byte of the
    X-pointer is updated in devices with no more than 256 bytes data space. For
    such devices, the high byte of the pointer is not used by this instruction
    and can be used for other purposes. The RAMPX Register in the I/O area is
    updated in parts with more than 64KB data space or more than 64KB Program
    memory, and the increment/decrement is added to the entire 24-bit address on
    such devices.  Not all variants of this instruction is available in all
    devices. Refer to the device specific instruction set summary.  In the
    Reduced Core tinyAVR the LD instruction can be used to achieve the same
    operation as LPM since the program memory is mapped to the data memory
    space.
*/
int avr_translate_LDX1(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDX1_Rd(opcode)];
    TCGv addr = gen_get_xaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_LDX2(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDX2_Rd(opcode)];
    TCGv addr = gen_get_xaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);
    tcg_gen_addi_tl(addr, addr, 1);             /*  addr = addr + 1  */

    gen_set_xaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_LDX3(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDX3_Rd(opcode)];
    TCGv addr = gen_get_xaddr();

    tcg_gen_subi_tl(addr, addr, 1);             /*  addr = addr - 1  */
    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);
    gen_set_xaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Loads one byte indirect with or without displacement from the data space
    to a register. For parts with SRAM, the data space consists of the Register
    File, I/O memory and internal SRAM (and external SRAM if applicable). For
    parts without SRAM, the data space consists of the Register File only. In
    some parts the Flash Memory has been mapped to the data space and can be
    read using this command. The EEPROM has a separate address space.  The data
    location is pointed to by the Y (16 bits) Pointer Register in the Register
    File. Memory access is limited to the current data segment of 64KB. To
    access another data segment in devices with more than 64KB data space, the
    RAMPY in register in the I/O area has to be changed.  The Y-pointer Register
    can either be left unchanged by the operation, or it can be post-incremented
    or predecremented.  These features are especially suited for accessing
    arrays, tables, and Stack Pointer usage of the Y-pointer Register. Note that
    only the low byte of the Y-pointer is updated in devices with no more than
    256 bytes data space. For such devices, the high byte of the pointer is not
    used by this instruction and can be used for other purposes. The RAMPY
    Register in the I/O area is updated in parts with more than 64KB data space
    or more than 64KB Program memory, and the increment/decrement/displacement
    is added to the entire 24-bit address on such devices.  Not all variants of
    this instruction is available in all devices. Refer to the device specific
    instruction set summary.  In the Reduced Core tinyAVR the LD instruction can
    be used to achieve the same operation as LPM since the program memory is
    mapped to the data memory space.
*/
int avr_translate_LDY2(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDY2_Rd(opcode)];
    TCGv addr = gen_get_yaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);
    tcg_gen_addi_tl(addr, addr, 1);             /*  addr = addr + 1  */

    gen_set_yaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_LDY3(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDY3_Rd(opcode)];
    TCGv addr = gen_get_yaddr();

    tcg_gen_subi_tl(addr, addr, 1);             /*  addr = addr - 1  */
    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);
    gen_set_yaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_LDDY(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDDY_Rd(opcode)];
    TCGv addr = gen_get_yaddr();

    tcg_gen_addi_tl(addr, addr, LDDY_Imm(opcode));
                                                    /*  addr = addr + q  */
    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Loads one byte indirect with or without displacement from the data space
    to a register. For parts with SRAM, the data space consists of the Register
    File, I/O memory and internal SRAM (and external SRAM if applicable). For
    parts without SRAM, the data space consists of the Register File only. In
    some parts the Flash Memory has been mapped to the data space and can be
    read using this command. The EEPROM has a separate address space.  The data
    location is pointed to by the Z (16 bits) Pointer Register in the Register
    File. Memory access is limited to the current data segment of 64KB. To
    access another data segment in devices with more than 64KB data space, the
    RAMPZ in register in the I/O area has to be changed.  The Z-pointer Register
    can either be left unchanged by the operation, or it can be post-incremented
    or predecremented.  These features are especially suited for Stack Pointer
    usage of the Z-pointer Register, however because the Z-pointer Register can
    be used for indirect subroutine calls, indirect jumps and table lookup, it
    is often more convenient to use the X or Y-pointer as a dedicated Stack
    Pointer. Note that only the low byte of the Z-pointer is updated in devices
    with no more than 256 bytes data space. For such devices, the high byte of
    the pointer is not used by this instruction and can be used for other
    purposes. The RAMPZ Register in the I/O area is updated in parts with more
    than 64KB data space or more than 64KB Program memory, and the
    increment/decrement/displacement is added to the entire 24-bit address on
    such devices.  Not all variants of this instruction is available in all
    devices. Refer to the device specific instruction set summary.  In the
    Reduced Core tinyAVR the LD instruction can be used to achieve the same
    operation as LPM since the program memory is mapped to the data memory
    space.  For using the Z-pointer for table lookup in Program memory see the
    LPM and ELPM instructions.
*/
int avr_translate_LDZ2(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDZ2_Rd(opcode)];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);
    tcg_gen_addi_tl(addr, addr, 1);             /*  addr = addr + 1  */

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_LDZ3(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDZ3_Rd(opcode)];
    TCGv addr = gen_get_zaddr();

    tcg_gen_subi_tl(addr, addr, 1);             /*  addr = addr - 1  */
    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_LDDZ(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LDDZ_Rd(opcode)];
    TCGv addr = gen_get_zaddr();

    tcg_gen_addi_tl(addr, addr, LDDZ_Imm(opcode));
                                                    /*  addr = addr + q  */
    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Loads an 8 bit constant directly to register 16 to 31.
*/
int avr_translate_LDI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[16 + LDI_Rd(opcode)];
    int imm = LDI_Imm(opcode);

    tcg_gen_movi_tl(Rd, imm);

    return  BS_NONE;
}

/*
    Loads one byte from the data space to a register. For parts with SRAM,
    the data space consists of the Register File, I/O memory and internal SRAM
    (and external SRAM if applicable). For parts without SRAM, the data space
    consists of the register file only. The EEPROM has a separate address space.
    A 16-bit address must be supplied. Memory access is limited to the current
    data segment of 64KB. The LDS instruction uses the RAMPD Register to access
    memory above 64KB. To access another data segment in devices with more than
    64KB data space, the RAMPD in register in the I/O area has to be changed.
    This instruction is not available in all devices. Refer to the device
    specific instruction set summary.
*/
int avr_translate_LDS(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[16 + LDS_Rd(opcode)];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_rampD;

    tcg_gen_mov_tl(addr, H);                     /*  addr = H:M:L  */
    tcg_gen_shli_tl(addr, addr, 16);
    tcg_gen_ori_tl(addr, addr, LDS_Imm(opcode));

    tcg_gen_qemu_ld8u(Rd, addr, MMU_DATA_IDX);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Loads one byte pointed to by the Z-register into the destination
    register Rd. This instruction features a 100% space effective constant
    initialization or constant data fetch. The Program memory is organized in
    16-bit words while the Z-pointer is a byte address. Thus, the least
    significant bit of the Z-pointer selects either low byte (ZLSB = 0) or high
    byte (ZLSB = 1). This instruction can address the first 64KB (32K words) of
    Program memory. The Zpointer Register can either be left unchanged by the
    operation, or it can be incremented. The incrementation does not apply to
    the RAMPZ Register.  Devices with Self-Programming capability can use the
    LPM instruction to read the Fuse and Lock bit values.  Refer to the device
    documentation for a detailed description.  The LPM instruction is not
    available in all devices. Refer to the device specific instruction set
    summary
*/
int avr_translate_LPM1(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_LPM) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rd = cpu_r[0];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8);             /*  addr = H:L  */
    tcg_gen_or_tl(addr, addr, L);

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX);    /*  Rd = mem[addr]  */

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_LPM2(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_LPM) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rd = cpu_r[LPM2_Rd(opcode)];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8);             /*  addr = H:L  */
    tcg_gen_or_tl(addr, addr, L);

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX);    /*  Rd = mem[addr]  */

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_LPMX(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_LPMX) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rd = cpu_r[LPMX_Rd(opcode)];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8);             /*  addr = H:L  */
    tcg_gen_or_tl(addr, addr, L);

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX);    /*  Rd = mem[addr]  */

    tcg_gen_addi_tl(addr, addr, 1);             /*  addr = addr + 1  */

    tcg_gen_andi_tl(L, addr, 0xff);

    tcg_gen_shri_tl(addr, addr, 8);
    tcg_gen_andi_tl(H, addr, 0xff);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Shifts all bits in Rd one place to the right. Bit 7 is cleared. Bit 0 is
    loaded into the C Flag of the SREG. This operation effectively divides an
    unsigned value by two. The C Flag can be used to round the result.
*/
int avr_translate_LSR(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[LSR_Rd(opcode)];

    tcg_gen_andi_tl(cpu_Cf, Rd, 1);

    tcg_gen_shri_tl(Rd, Rd, 1);

    gen_ZNSf(Rd);
    tcg_gen_xor_tl(cpu_Vf, cpu_Nf, cpu_Cf);
    return  BS_NONE;
}

/*
    This instruction makes a copy of one register into another. The source
    register Rr is left unchanged, while the destination register Rd is loaded
    with a copy of Rr.
*/
int avr_translate_MOV(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[MOV_Rd(opcode)];
    TCGv Rr = cpu_r[MOV_Rr(opcode)];

    tcg_gen_mov_tl(Rd, Rr);

    return  BS_NONE;
}

/*
    This instruction makes a copy of one register pair into another register
    pair. The source register pair Rr+1:Rr is left unchanged, while the
    destination register pair Rd+1:Rd is loaded with a copy of Rr + 1:Rr.  This
    instruction is not available in all devices. Refer to the device specific
    instruction set summary.
*/
int avr_translate_MOVW(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_MOVW) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv RdL = cpu_r[MOVW_Rd(opcode) * 2 + 0];
    TCGv RdH = cpu_r[MOVW_Rd(opcode) * 2 + 1];
    TCGv RrL = cpu_r[MOVW_Rr(opcode) * 2 + 0];
    TCGv RrH = cpu_r[MOVW_Rr(opcode) * 2 + 1];

    tcg_gen_mov_tl(RdH, RrH);
    tcg_gen_mov_tl(RdL, RrL);

    return  BS_NONE;
}

/*
    This instruction performs 8-bit x 8-bit -> 16-bit unsigned multiplication.
*/
int avr_translate_MUL(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_MUL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[MUL_Rd(opcode)];
    TCGv Rr = cpu_r[MUL_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_mul_tl(R, Rd, Rr);    /*  R = Rd  *Rr  */

    tcg_gen_mov_tl(R0, R);
    tcg_gen_andi_tl(R0, R0, 0xff);
    tcg_gen_shri_tl(R, R, 8);
    tcg_gen_mov_tl(R1, R);

    tcg_gen_shri_tl(cpu_Cf, R, 15);    /*  Cf = R(16)  */
    tcg_gen_mov_tl(cpu_Zf, R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication.
*/
int avr_translate_MULS(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_MUL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[16 + MULS_Rd(opcode)];
    TCGv Rr = cpu_r[16 + MULS_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd);           /*  make Rd full 32 bit signed  */
    tcg_gen_ext8s_tl(t1, Rr);           /*  make Rr full 32 bit signed  */
    tcg_gen_mul_tl(R, t0, t1);          /*  R = Rd * Rr  */

    tcg_gen_mov_tl(R0, R);
    tcg_gen_andi_tl(R0, R0, 0xff);
    tcg_gen_shri_tl(R, R, 8);
    tcg_gen_mov_tl(R1, R);
    tcg_gen_andi_tl(R1, R0, 0xff);

    tcg_gen_shri_tl(cpu_Cf, R, 15);     /*  Cf = R(16)  */
    tcg_gen_mov_tl(cpu_Zf, R);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs 8-bit x 8-bit -> 16-bit multiplication of a
    signed and an unsigned number.
*/
int avr_translate_MULSU(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_MUL) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[16 + MULSU_Rd(opcode)];
    TCGv Rr = cpu_r[16 + MULSU_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd);           /*  make Rd full 32 bit signed  */
    tcg_gen_mul_tl(R, t0, Rr);          /*  R = Rd  *Rr  */

    tcg_gen_mov_tl(R0, R);
    tcg_gen_andi_tl(R0, R0, 0xff);
    tcg_gen_shri_tl(R, R, 8);
    tcg_gen_mov_tl(R1, R);
    tcg_gen_andi_tl(R1, R0, 0xff);

    tcg_gen_shri_tl(cpu_Cf, R, 16);     /*  Cf = R(16)  */
    tcg_gen_mov_tl(cpu_Zf, R);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Replaces the contents of register Rd with its two’s complement; the
    value $80 is left unchanged.
*/
int avr_translate_NEG(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[SUB_Rd(opcode)];
    TCGv t0 = tcg_const_i32(0);
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_sub_tl(R, t0, Rd);               /*  R = 0 - Rd      */
    tcg_gen_andi_tl(R, R, 0xff);            /*  make it 8 bits  */

    gen_sub_CHf(R, t0, Rd);
    gen_sub_Vf(R, t0, Rd);
    gen_ZNSf(R);

    /*  R  */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction performs a single cycle No Operation.
*/
int avr_translate_NOP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{

    /*  NOP  */

    return  BS_NONE;
}

/*
    Performs the logical OR between the contents of register Rd and register
    Rr and places the result in the destination register Rd.
*/
int avr_translate_OR(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[OR_Rd(opcode)];
    TCGv Rr = cpu_r[OR_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_or_tl(R, Rd, Rr);

    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Performs the logical OR between the contents of register Rd and a
    constant and places the result in the destination register Rd.
*/
int avr_translate_ORI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[16 + ORI_Rd(opcode)];
    int Imm = (ORI_Imm(opcode));

    tcg_gen_ori_tl(Rd, Rd, Imm);   /*  Rd = Rd | Imm  */

    tcg_gen_movi_tl(cpu_Vf, 0x00);          /*  Vf = 0  */
    gen_ZNSf(Rd);

    return  BS_NONE;
}

/*
    Stores data from register Rr in the Register File to I/O Space (Ports,
    Timers, Configuration Registers, etc.).  */
int avr_translate_OUT(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[OUT_Rd(opcode)];
    int Imm = OUT_Imm(opcode);
    TCGv port = tcg_const_i32(Imm);
    TCGv data = cpu_io[Imm];

    tcg_gen_mov_tl(data, Rd);
    gen_helper_outb(cpu_env, port, data);

    return  BS_NONE;
}

/*
    This instruction loads register Rd with a byte from the STACK. The Stack
    Pointer is pre-incremented by 1 before the POP.  This instruction is not
    available in all devices. Refer to the device specific instruction set
    summary.
*/
int avr_translate_POP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[POP_Rd(opcode)];

    tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
    tcg_gen_qemu_ld8u(Rd, cpu_sp, MMU_DATA_IDX);

    return  BS_NONE;
}

/*
    This instruction stores the contents of register Rr on the STACK. The
    Stack Pointer is post-decremented by 1 after the PUSH.  This instruction is
    not available in all devices. Refer to the device specific instruction set
    summary.
*/
int avr_translate_PUSH(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[PUSH_Rd(opcode)];

    gen_data_store(env, Rd, cpu_sp);
    tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

    return  BS_NONE;
}

/*
    Relative call to an address within PC - 2K + 1 and PC + 2K (words). The
    return address (the instruction after the RCALL) is stored onto the Stack.
    See also CALL. For AVR microcontrollers with Program memory not exceeding 4K
    words (8KB) this instruction can address the entire memory from every
    address location. The Stack Pointer uses a post-decrement scheme during
    RCALL.
*/
int avr_translate_RCALL(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    int ret = ctx->inst[0].npc;
    int dst = ctx->inst[0].npc + sextract32(RCALL_Imm(opcode), 0, 12);

    gen_push_ret(env, ret);

    gen_goto_tb(env, ctx, 0, dst);

    return  BS_BRANCH;
}

/*
    Returns from subroutine. The return address is loaded from the STACK.
    The Stack Pointer uses a preincrement scheme during RET.
*/
int avr_translate_RET(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    gen_pop_ret(env, cpu_pc);

    tcg_gen_exit_tb(0);

    return  BS_BRANCH;
}

/*
    Returns from interrupt. The return address is loaded from the STACK and
    the Global Interrupt Flag is set.  Note that the Status Register is not
    automatically stored when entering an interrupt routine, and it is not
    restored when returning from an interrupt routine. This must be handled by
    the application program. The Stack Pointer uses a pre-increment scheme
    during RETI.
*/
int avr_translate_RETI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    gen_pop_ret(env, cpu_pc);

    tcg_gen_movi_tl(cpu_If, 1);

    tcg_gen_exit_tb(0);

    return  BS_BRANCH;
}

/*
    Relative jump to an address within PC - 2K +1 and PC + 2K (words). For
    AVR microcontrollers with Program memory not exceeding 4K words (8KB) this
    instruction can address the entire memory from every address location. See
    also JMP.
*/
int avr_translate_RJMP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    int dst = ctx->inst[0].npc + sextract32(RJMP_Imm(opcode), 0, 12);

    gen_goto_tb(env, ctx, 0, dst);

    return  BS_BRANCH;
}

/*
    Shifts all bits in Rd one place to the right. The C Flag is shifted into
    bit 7 of Rd. Bit 0 is shifted into the C Flag.  This operation, combined
    with ASR, effectively divides multi-byte signed values by two. Combined with
    LSR it effectively divides multi-byte unsigned values by two. The Carry Flag
    can be used to round the result.
*/
int avr_translate_ROR(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[ROR_Rd(opcode)];
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, cpu_Cf, 7);
    tcg_gen_andi_tl(cpu_Cf, Rd, 0);
    tcg_gen_shri_tl(Rd, Rd, 1);
    tcg_gen_or_tl(Rd, Rd, t0);

    gen_ZNSf(Rd);
    tcg_gen_xor_tl(cpu_Vf, cpu_Nf, cpu_Cf);

    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    Subtracts two registers and subtracts with the C Flag and places the
    result in the destination register Rd.
*/
int avr_translate_SBC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[SBC_Rd(opcode)];
    TCGv Rr = cpu_r[SBC_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_sub_tl(R, Rd, Rr);             /*  R = Rd - Rr - Cf  */
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff);            /*  make it 8 bits  */

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /*  R  */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    SBCI – Subtract Immediate with Carry
*/
int avr_translate_SBCI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[16 + SBCI_Rd(opcode)];
    TCGv Rr = tcg_const_i32(SBCI_Imm(opcode));
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_sub_tl(R, Rd, Rr);             /*  R = Rd - Rr - Cf  */
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff);            /*  make it 8 bits  */

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /*  R  */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Sets a specified bit in an I/O Register. This instruction operates on
    the lower 32 I/O Registers – addresses 0-31.  */
int avr_translate_SBI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv data = cpu_io[SBI_Imm(opcode)];
    TCGv port = tcg_const_i32(SBI_Imm(opcode));

    tcg_gen_ori_tl(data, data, 1 << SBI_Bit(opcode));
    gen_helper_outb(cpu_env, port, data);

    return  BS_NONE;
}

/*
    This instruction tests a single bit in an I/O Register and skips the
    next instruction if the bit is cleared. This instruction operates on the
    lower 32 I/O Registers – addresses 0-31.  */
int avr_translate_SBIC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv io = cpu_io[SBIC_Imm(opcode)];
    TCGv t0 = tcg_temp_new_i32();
    TCGLabel   *skip = gen_new_label();

        /*  PC if next inst is skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[1].npc);
    tcg_gen_andi_tl(t0, io, 1 << SBIC_Bit(opcode));
    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, skip);
        /*  PC if next inst is not skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[0].npc);
    gen_set_label(skip);

    tcg_temp_free_i32(t0);

    return  BS_BRANCH;
}

/*
    This instruction tests a single bit in an I/O Register and skips the
    next instruction if the bit is set. This instruction operates on the lower
    32 I/O Registers – addresses 0-31.  */
int avr_translate_SBIS(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv io = cpu_io[SBIS_Imm(opcode)];
    TCGv t0 = tcg_temp_new_i32();
    TCGLabel   *skip = gen_new_label();

        /*  PC if next inst is skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[1].npc);
    tcg_gen_andi_tl(t0, io, 1 << SBIS_Bit(opcode));
    tcg_gen_brcondi_i32(TCG_COND_NE, t0, 0, skip);
        /*  PC if next inst is not skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[0].npc);
    gen_set_label(skip);

    tcg_temp_free_i32(t0);

    return  BS_BRANCH;
}

/*
    Subtracts an immediate value (0-63) from a register pair and places the
    result in the register pair. This instruction operates on the upper four
    register pairs, and is well suited for operations on the Pointer Registers.
    This instruction is not available in all devices. Refer to the device
    specific instruction set summary.
*/
int avr_translate_SBIW(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_ADIW_SBIW) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv RdL = cpu_r[24 + 2 * SBIW_Rd(opcode)];
    TCGv RdH = cpu_r[25 + 2 * SBIW_Rd(opcode)];
    int Imm = (SBIW_Imm(opcode));
    TCGv R = tcg_temp_new_i32();
    TCGv Rd = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_deposit_tl(Rd, RdL, RdH, 8, 8);     /*  Rd  = RdH:RdL   */
    tcg_gen_subi_tl(R, Rd, Imm);                /*  R   = Rd - Imm  */
    tcg_gen_andi_tl(R, R, 0xffff);              /*  make it 16 bits */

    /*  Cf  */
    tcg_gen_andc_tl(cpu_Cf, R, Rd);
    tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 15);        /*  Cf = R & ~Rd    */

    /*  Vf  */
    tcg_gen_andc_tl(cpu_Vf, Rd, R);
    tcg_gen_shri_tl(cpu_Vf, cpu_Vf, 15);        /*  Vf = Rd & ~R    */

    /*  Zf  */
    tcg_gen_mov_tl(cpu_Zf, R);                  /*  Zf = R          */

    /*  Nf  */
    tcg_gen_shri_tl(cpu_Nf, R, 15);             /*  Nf = R(15)      */

    /*  Sf  */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);     /*  Sf = Nf ^ Vf    */

    /*  R  */
    tcg_gen_andi_tl(RdL, R, 0xff);
    tcg_gen_shri_tl(RdH, R, 8);

    tcg_temp_free_i32(Rd);
    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    This instruction tests a single bit in a register and skips the next
    instruction if the bit is cleared.
*/
int avr_translate_SBRC(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rr = cpu_r[16 + SBRC_Rr(opcode)];
    TCGv t0 = tcg_temp_new_i32();
    TCGLabel   *skip = gen_new_label();

        /*  PC if next inst is skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[1].npc);
    tcg_gen_andi_tl(t0, Rr, 1 << SBRC_Bit(opcode));
    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, skip);
        /*  PC if next inst is not skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[0].npc);
    gen_set_label(skip);

    tcg_temp_free_i32(t0);

    return  BS_BRANCH;
}

/*
    This instruction tests a single bit in a register and skips the next
    instruction if the bit is set.
*/
int avr_translate_SBRS(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rr = cpu_r[SBRS_Rr(opcode)];
    TCGv t0 = tcg_temp_new_i32();
    TCGLabel   *skip = gen_new_label();

        /*  PC if next inst is skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[1].npc);
    tcg_gen_andi_tl(t0, Rr, 1 << SBRS_Bit(opcode));
    tcg_gen_brcondi_i32(TCG_COND_NE, t0, 0, skip);
        /*  PC if next inst is not skipped  */
    tcg_gen_movi_tl(cpu_pc, ctx->inst[0].npc);
    gen_set_label(skip);

    tcg_temp_free_i32(t0);

    return  BS_BRANCH;
}

/*
    This instruction sets the circuit in sleep mode defined by the MCU
    Control Register.
*/
int avr_translate_SLEEP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    gen_helper_sleep(cpu_env);

    return  BS_EXCP;
}

/*
    SPM can be used to erase a page in the Program memory, to write a page
    in the Program memory (that is already erased), and to set Boot Loader Lock
    bits. In some devices, the Program memory can be written one word at a time,
    in other devices an entire page can be programmed simultaneously after first
    filling a temporary page buffer. In all cases, the Program memory must be
    erased one page at a time. When erasing the Program memory, the RAMPZ and
    Z-register are used as page address. When writing the Program memory, the
    RAMPZ and Z-register are used as page or word address, and the R1:R0
    register pair is used as data(1). When setting the Boot Loader Lock bits,
    the R1:R0 register pair is used as data. Refer to the device documentation
    for detailed description of SPM usage. This instruction can address the
    entire Program memory.  The SPM instruction is not available in all devices.
    Refer to the device specific instruction set summary.  Note: 1. R1
    determines the instruction high byte, and R0 determines the instruction low
    byte.
*/
int avr_translate_SPM(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_SPM) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    /*  TODO:   ???  */
    return  BS_NONE;
}

int avr_translate_SPMX(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_SPMX) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    /*  TODO:   ???  */
    return  BS_NONE;
}

int avr_translate_STX1(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STX1_Rr(opcode)];
    TCGv addr = gen_get_xaddr();

    gen_data_store(env, Rd, addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_STX2(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STX2_Rr(opcode)];
    TCGv addr = gen_get_xaddr();

    gen_data_store(env, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);             /*  addr = addr + 1  */
    gen_set_xaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_STX3(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STX3_Rr(opcode)];
    TCGv addr = gen_get_xaddr();

    tcg_gen_subi_tl(addr, addr, 1);             /*  addr = addr - 1  */
    gen_data_store(env, Rd, addr);
    gen_set_xaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_STY2(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STY2_Rd(opcode)];
    TCGv addr = gen_get_yaddr();

    gen_data_store(env, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);             /*  addr = addr + 1  */
    gen_set_yaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_STY3(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STY3_Rd(opcode)];
    TCGv addr = gen_get_yaddr();

    tcg_gen_subi_tl(addr, addr, 1);             /*  addr = addr - 1  */
    gen_data_store(env, Rd, addr);
    gen_set_yaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_STDY(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STDY_Rd(opcode)];
    TCGv addr = gen_get_yaddr();

    tcg_gen_addi_tl(addr, addr, STDY_Imm(opcode));
                                                /*  addr = addr + q  */
    gen_data_store(env, Rd, addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_STZ2(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STZ2_Rd(opcode)];
    TCGv addr = gen_get_zaddr();

    gen_data_store(env, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1);             /*  addr = addr + 1  */

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_STZ3(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STZ3_Rd(opcode)];
    TCGv addr = gen_get_zaddr();

    tcg_gen_subi_tl(addr, addr, 1);             /*  addr = addr - 1  */
    gen_data_store(env, Rd, addr);

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

int avr_translate_STDZ(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STDZ_Rd(opcode)];
    TCGv addr = gen_get_zaddr();

    tcg_gen_addi_tl(addr, addr, STDZ_Imm(opcode));
                                                    /*  addr = addr + q  */
    gen_data_store(env, Rd, addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Stores one byte from a Register to the data space. For parts with SRAM,
    the data space consists of the Register File, I/O memory and internal SRAM
    (and external SRAM if applicable). For parts without SRAM, the data space
    consists of the Register File only. The EEPROM has a separate address space.
    A 16-bit address must be supplied. Memory access is limited to the current
    data segment of 64KB. The STS instruction uses the RAMPD Register to access
    memory above 64KB. To access another data segment in devices with more than
    64KB data space, the RAMPD in register in the I/O area has to be changed.
    This instruction is not available in all devices. Refer to the device
    specific instruction set summary.
*/
int avr_translate_STS(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[STS_Rd(opcode)];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_rampD;

    tcg_gen_mov_tl(addr, H);                     /*  addr = H:M:L  */
    tcg_gen_shli_tl(addr, addr, 16);
    tcg_gen_ori_tl(addr, addr, STS_Imm(opcode));

    gen_data_store(env, Rd, addr);

    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

/*
    Subtracts two registers and places the result in the destination
    register Rd.
*/
int avr_translate_SUB(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[SUB_Rd(opcode)];
    TCGv Rr = cpu_r[SUB_Rr(opcode)];
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_sub_tl(R, Rd, Rr);            /*  R = Rd - Rr  */
    tcg_gen_andi_tl(R, R, 0xff);          /*  make it 8 bits  */

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /*  R  */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return  BS_NONE;
}

/*
    Subtracts a register and a constant and places the result in the
    destination register Rd. This instruction is working on Register R16 to R31
    and is very well suited for operations on the X, Y, and Z-pointers.
*/
int avr_translate_SUBI(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[16 + SUBI_Rd(opcode)];
    TCGv Rr = tcg_const_i32(SUBI_Imm(opcode));
    TCGv R = tcg_temp_new_i32();

    /*  op  */
    tcg_gen_sub_tl(R, Rd, Rr);
                                                    /*  R = Rd - Imm  */
    tcg_gen_andi_tl(R, R, 0xff);          /*  make it 8 bits  */

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /*  R  */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);
    tcg_temp_free_i32(Rr);

    return  BS_NONE;
}

/*
    Swaps high and low nibbles in a register.
*/
int avr_translate_SWAP(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    TCGv Rd = cpu_r[SWAP_Rd(opcode)];
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_andi_tl(t0, Rd, 0x0f);
    tcg_gen_shli_tl(t0, t0, 4);
    tcg_gen_andi_tl(t1, Rd, 0xf0);
    tcg_gen_shri_tl(t1, t1, 4);
    tcg_gen_or_tl(Rd, t0, t1);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);

    return  BS_NONE;
}

/*
    This instruction resets the Watchdog Timer. This instruction must be
    executed within a limited time given by the WD prescaler. See the Watchdog
    Timer hardware specification.
*/
int avr_translate_WDR(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    gen_helper_wdr(cpu_env);

    return  BS_NONE;
}

/*
    Exchanges one byte indirect between register and data space.  The data
    location is pointed to by the Z (16 bits) Pointer Register in the Register
    File. Memory access is limited to the current data segment of 64KB. To
    access another data segment in devices with more than 64KB data space, the
    RAMPZ in register in the I/O area has to be changed.  The Z-pointer Register
    is left unchanged by the operation. This instruction is especially suited
    for writing/reading status bits stored in SRAM.
*/
int avr_translate_XCH(CPUAVRState *env, DisasContext *ctx, uint32_t opcode)
{
    if (avr_feature(env, AVR_FEATURE_RMW) == false) {
        gen_helper_unsupported(cpu_env);

        return BS_EXCP;
    }

    TCGv Rd = cpu_r[XCH_Rd(opcode)];
    TCGv t0 = tcg_temp_new_i32();
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld8u(t0, addr, MMU_DATA_IDX);
    gen_data_store(env, Rd, addr);
    tcg_gen_mov_tl(Rd, t0);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(addr);

    return  BS_NONE;
}

