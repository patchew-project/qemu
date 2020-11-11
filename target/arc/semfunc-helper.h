/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 * Contributed by Cupertino Miranda <cmiranda@synopsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#ifndef TRANSLATE_INST_H_
#define TRANSLATE_INST_H_

#include "translate.h"
#include "qemu/bitops.h"
#include "tcg/tcg.h"
#include "target/arc/regs.h"

typedef enum ARC_COND {
    ARC_COND_AL      = 0x00,
    ARC_COND_RA      = 0x00,
    ARC_COND_EQ      = 0x01,
    ARC_COND_Z       = 0x01,
    ARC_COND_NE      = 0x02,
    ARC_COND_NZ      = 0x02,
    ARC_COND_PL      = 0x03,
    ARC_COND_P       = 0x03,
    ARC_COND_MI      = 0x04,
    ARC_COND_N       = 0x04,
    ARC_COND_CS      = 0x05,
    ARC_COND_C       = 0x05,
    ARC_COND_LO      = 0x05,
    ARC_COND_CC      = 0x06,
    ARC_COND_NC      = 0x06,
    ARC_COND_HS      = 0x06,
    ARC_COND_VS      = 0x07,
    ARC_COND_V       = 0x07,
    ARC_COND_VC      = 0x08,
    ARC_COND_NV      = 0x08,
    ARC_COND_GT      = 0x09,
    ARC_COND_GE      = 0x0a,
    ARC_COND_LT      = 0x0b,
    ARC_COND_LE      = 0x0c,
    ARC_COND_HI      = 0x0d,
    ARC_COND_LS      = 0x0e,
    ARC_COND_PNZ     = 0x0f,
} ARC_COND;

#define ARC_HELPER(NAME, RET, ...) \
    gen_helper_##NAME(RET, cpu_env, __VA_ARGS__)

enum arc_registers {
    R_SP = 0,
    R_STATUS32,
    R_ACCLO,
    R_ACCHI
};

enum target_options {
    INVALID_TARGET_OPTIONS = -1,
    DIV_REM_OPTION,
    STACK_CHECKING,
    LL64_OPTION
};

/* TODO: Change this to allow something else then ARC HS. */
#define LP_START    \
    (arc_aux_reg_address_for(AUX_ID_lp_start, ARC_OPCODE_ARCv2HS))
#define LP_END      \
    (arc_aux_reg_address_for(AUX_ID_lp_end, ARC_OPCODE_ARCv2HS))

#define ReplMask(DEST, SRC, MASK) \
    gen_helper_repl_mask(DEST, DEST, SRC, MASK)

void arc_gen_verifyCCFlag(const DisasCtxt *ctx, TCGv ret);
#define getCCFlag(R)    arc_gen_verifyCCFlag(ctx, R)

#define getFFlag(R) ((int) ctx->insn.f)

void to_implement(DisasCtxt *ctx);
void to_implement_wo_abort(DisasCtxt *ctx);

#define killDelaySlot()
void arc_gen_set_memory(
        DisasCtxt *ctx, TCGv addr, int size, TCGv src, bool sign_extend);
#define setMemory(ADDRESS, SIZE, VALUE) \
    arc_gen_set_memory(ctx, ADDRESS, SIZE, VALUE, getFlagX())
void arc_gen_get_memory(
        DisasCtxt *ctx, TCGv ret, TCGv addr, int size, bool sign_extend);
#define getMemory(R, ADDRESS, SIZE) \
    arc_gen_get_memory(ctx, R, ADDRESS, SIZE, getFlagX())

#define getFlagX()  (ctx->insn.x)
#define getZZFlag() (ctx->insn.zz)
#define getAAFlag() (ctx->insn.aa)

#define SignExtend(VALUE, SIZE) VALUE
void arc_gen_no_further_loads_pending(DisasCtxt *ctx, TCGv ret);
#define NoFurtherLoadsPending(R)    arc_gen_no_further_loads_pending(ctx, R)
void arc_gen_set_debug(DisasCtxt *ctx, bool value);
#define setDebugLD(A)   arc_gen_set_debug(ctx, A)
void arc_gen_execute_delayslot(DisasCtxt *ctx, TCGv bta, TCGv take_branch);
#define executeDelaySlot(bta, take_branch) \
    arc_gen_execute_delayslot(ctx, bta, take_branch)

#define shouldExecuteDelaySlot()    (ctx->insn.d != 0)

#define setNFlag(ELEM)  tcg_gen_shri_tl(cpu_Nf, ELEM, 31)
#define getNFlag(R)     cpu_Nf

#define setCFlag(ELEM)  tcg_gen_mov_tl(cpu_Cf, ELEM)
#define getCFlag(R)     tcg_gen_mov_tl(R, cpu_Cf)

#define setVFlag(ELEM)  tcg_gen_mov_tl(cpu_Vf, ELEM)

#define setZFlag(ELEM)  \
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_Zf, ELEM, 0);

#define nextInsnAddressAfterDelaySlot(R) \
  { \
    uint16_t delayslot_buffer[2]; \
    uint8_t delayslot_length; \
    ctx->env->pc = ctx->cpc; \
    ctx->env->stat.is_delay_slot_instruction = 1; \
    delayslot_buffer[0] = cpu_lduw_code(ctx->env, ctx->npc); \
    delayslot_length = arc_insn_length(delayslot_buffer[0], ctx->env->family); \
    tcg_gen_movi_tl(R, ctx->npc + delayslot_length); \
  }

#define nextInsnAddress(R)  tcg_gen_movi_tl(R, ctx->npc)
#define getPCL(R)           tcg_gen_movi_tl(R, ctx->pcl)

#define setPC(NEW_PC)                                   \
    do {                                                \
        gen_goto_tb(ctx, 1, NEW_PC);                    \
        ret = ret == DISAS_NEXT ? DISAS_NORETURN : ret; \
    } while (0)

#define setBLINK(BLINK_ADDR) \
  tcg_gen_mov_i32(cpu_blink, BLINK_ADDR);

#define Carry(R, A)             tcg_gen_shri_tl(R, A, 31);

#define CarryADD(R, A, B, C)    gen_helper_carry_add_flag(R, A, B, C)
#define OverflowADD(R, A, B, C) gen_helper_overflow_add_flag(R, A, B, C)

void arc_gen_sub_Cf(TCGv ret, TCGv dest, TCGv src1, TCGv src2);
#define CarrySUB(R, A, B, C)    arc_gen_sub_Cf(R, A, B, C)
#define OverflowSUB(R, A, B, C) gen_helper_overflow_sub_flag(R, A, B, C)

#define unsignedLT(R, B, C)           tcg_gen_setcond_i32(TCG_COND_LTU, R, B, C)
#define unsignedGE(R, B, C)           tcg_gen_setcond_i32(TCG_COND_GEU, R, B, C)
#define logicalShiftRight(R, B, C)    tcg_gen_shr_i32(R, B, C)
#define logicalShiftLeft(R, B, C)     tcg_gen_shl_i32(R, B, C)
#define arithmeticShiftRight(R, B, C) tcg_gen_sar_i32(R, B, C)
#define rotateLeft(R, B, C)           tcg_gen_rotl_i32(R, B, C)
#define rotateRight(R, B, C)          tcg_gen_rotr_i32(R, B, C)

void arc_gen_get_bit(TCGv ret, TCGv a, TCGv pos);
#define getBit(R, A, POS)   arc_gen_get_bit(R, A, POS)

#define getRegIndex(R, ID)  tcg_gen_movi_tl(R, (int) ID)

#define readAuxReg(R, A)    gen_helper_lr(R, cpu_env, A)
/*
 * Here, by returning DISAS_UPDATE we are making SR the end
 * of a Translation Block (TB). This is necessary because
 * sometimes writing to control registers updates how a TB is
 * handled, like enabling MMU/MPU. If SR is not marked as the
 * end, the next instructions are fetched and generated and
 * the updated outcome (page/region permissions) is not taken
 * into account.
 */
#define writeAuxReg(NAME, B)             \
    do {                                 \
        gen_helper_sr(cpu_env, B, NAME); \
        ret = DISAS_UPDATE;              \
    } while (0)

/*
 * At the end of a SYNC instruction, it is guaranteed that
 * handling the current interrupt is finished and the raising
 * pulse signal (if any), is cleared. By marking SYNC as the
 * end of a TB we gave a chance to interrupt threads to execute.
 */
#define syncReturnDisasUpdate()     (ret = DISAS_UPDATE)

/*
 * An enter_s may change code like below:
 * ----
 * r13 .. r26 <== shell opcodes
 * sp <= pc+56
 * enter_s
 * ---
 * It's not that we are promoting these type of instructions.
 * nevertheless we must be able to emulate them. Hence, once
 * again: ret = DISAS_UPDATE
 */
#define helperEnter(U6)                 \
    do {                                \
        gen_helper_enter(cpu_env, U6);  \
        ret = DISAS_UPDATE;             \
    } while (0)

/* A leave_s may jump to blink, hence the DISAS_UPDATE */
#define helperLeave(U7)                                           \
    do {                                                          \
        tcg_gen_movi_tl(cpu_pc, ctx->cpc);                        \
        gen_helper_leave(cpu_env, U7);                            \
        TCGv jump_to_blink = tcg_temp_local_new_i32();            \
        TCGLabel *done = gen_new_label();                         \
        tcg_gen_shri_i32(jump_to_blink, U7, 6);                   \
        tcg_gen_brcondi_i32(TCG_COND_EQ, jump_to_blink, 0, done); \
        gen_goto_tb(ctx, 1, cpu_pc);                              \
        ret = DISAS_NORETURN;                                     \
        gen_set_label(done);                                      \
        tcg_temp_free(jump_to_blink);                             \
    } while (0)

void arc_gen_mac(TCGv phi, TCGv_i32 b, TCGv_i32 c);
#define MAC(R, B, C)  arc_gen_mac(R, B, C)
void arc_gen_macu(TCGv phi, TCGv_i32 b, TCGv_i32 c);
#define MACU(R, B, C) arc_gen_macu(R, B, C)

void arc_gen_extract_bits(TCGv ret, TCGv a, TCGv start, TCGv end);
#define extractBits(R, ELEM, START, END) \
    arc_gen_extract_bits(R, ELEM, START, END)
void arc_gen_get_register(TCGv ret, enum arc_registers reg);
#define getRegister(R, REG) arc_gen_get_register(R, REG)
void arc_gen_set_register(enum arc_registers reg, TCGv value);
#define setRegister(REG, VALUE) arc_gen_set_register(REG, VALUE)

#define divSigned(R, SRC1, SRC2)            tcg_gen_div_i32(R, SRC1, SRC2)
#define divUnsigned(R, SRC1, SRC2)          tcg_gen_divu_i32(R, SRC1, SRC2)
#define divRemainingSigned(R, SRC1, SRC2)   tcg_gen_rem_i32(R, SRC1, SRC2)
#define divRemainingUnsigned(R, SRC1, SRC2) tcg_gen_remu_i32(R, SRC1, SRC2)

/* TODO: To implement */
#define Halt()

void arc_has_interrupts(DisasCtxt *ctx, TCGv ret);
#define hasInterrupts(R)    arc_has_interrupts(ctx, R)
#define doNothing()

#define setLF(VALUE)    tcg_gen_mov_tl(cpu_lock_lf_var, VALUE)
#define getLF(R)        tcg_gen_mov_tl(R, cpu_lock_lf_var)

/* Statically inferred return function */

TCGv arc_gen_next_reg(const DisasCtxt *ctx, TCGv reg);
#define nextReg(A) arc_gen_next_reg(ctx, A)

/* TODO (issue #62): This must be removed. */
#define Zero()  (ctx->zero)

bool arc_target_has_option(enum target_options option);
#define targetHasOption(OPTION) arc_target_has_option(OPTION)

bool arc_is_instruction_operand_a_register(const DisasCtxt *ctx, int nop);
#define instructionHasRegisterOperandIn(NOP) \
    arc_is_instruction_operand_a_register(ctx, NOP)

void tcg_gen_shlfi_i32(TCGv a, int b, TCGv c);

#endif /* TRANSLATE_INST_H_ */


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
