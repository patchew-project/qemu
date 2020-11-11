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

#include "qemu/osdep.h"
#include "translate.h"
#include "qemu/bitops.h"
#include "tcg/tcg.h"
#include "semfunc-helper.h"
#include "translate.h"

void arc_gen_verifyCCFlag(const DisasCtxt *ctx, TCGv ret)
{
    TCGv c1 = tcg_temp_new_i32();

    TCGv nZ = tcg_temp_new_i32();
    TCGv nN = tcg_temp_new_i32();
    TCGv nV = tcg_temp_new_i32();
    TCGv nC = tcg_temp_new_i32();

    switch (ctx->insn.cc) {
    /* AL, RA */
    case 0x00:
        tcg_gen_movi_i32(ret, 1);
        break;
    /* EQ, Z */
    case 0x01:
        tcg_gen_mov_i32(ret, cpu_Zf);
        break;
    /* NE, NZ */
    case 0x02:
        tcg_gen_xori_tl(ret, cpu_Zf, 1);
        break;
    /* PL, P */
    case 0x03:
        tcg_gen_xori_tl(ret, cpu_Nf, 1);
        break;
    /* MI, N: */
    case 0x04:
        tcg_gen_mov_i32(ret, cpu_Nf);
        break;
    /* CS, C, LO */
    case 0x05:
        tcg_gen_mov_i32(ret, cpu_Cf);
        break;
    /* CC, NC, HS */
    case 0x06:
        tcg_gen_xori_tl(ret, cpu_Cf, 1);
        break;
    /* VS, V */
    case 0x07:
        tcg_gen_mov_i32(ret, cpu_Vf);
        break;
    /* VC, NV */
    case 0x08:
        tcg_gen_xori_tl(ret, cpu_Vf, 1);
        break;
    /* GT */
    case 0x09:
        /* (N & V & !Z) | (!N & !V & !Z) */
        tcg_gen_xori_tl(nZ, cpu_Zf, 1);
        tcg_gen_xori_tl(nN, cpu_Nf, 1);
        tcg_gen_xori_tl(nV, cpu_Vf, 1);

        tcg_gen_mov_tl(c1, cpu_Nf);
        tcg_gen_and_tl(c1, c1, cpu_Vf);
        tcg_gen_and_tl(c1, c1, nZ);

        tcg_gen_mov_tl(ret, nN);
        tcg_gen_and_tl(ret, ret, nV);
        tcg_gen_and_tl(ret, ret, nZ);

        tcg_gen_or_tl(ret, c1, ret);
        break;
    /* GE */
    case 0x0A:
        /* (N & V) | (!N & !V) */
        tcg_gen_xori_tl(nN, cpu_Nf, 1);
        tcg_gen_xori_tl(nV, cpu_Vf, 1);

        tcg_gen_and_tl(c1, cpu_Nf, cpu_Vf);

        tcg_gen_and_tl(ret, nN, nV);

        tcg_gen_or_tl(ret, c1, ret);
        break;
    /* LT */
    case 0x0B:
        /* (N & !V) | (!N & V) */
        tcg_gen_xori_tl(nN, cpu_Nf, 1);
        tcg_gen_xori_tl(nV, cpu_Vf, 1);

        tcg_gen_and_tl(c1, cpu_Nf, nV);

        tcg_gen_and_tl(ret, nN, cpu_Vf);

        tcg_gen_or_tl(ret, ret, c1);
        break;
    /* LE */
    case 0x0C:
        /* Z | (N & !V) | (!N & V) */
        tcg_gen_xori_tl(nN, cpu_Nf, 1);
        tcg_gen_xori_tl(nV, cpu_Vf, 1);

        tcg_gen_and_tl(c1, cpu_Nf, nV);

        tcg_gen_and_tl(ret, nN, cpu_Vf);

        tcg_gen_or_tl(ret, ret, c1);

        tcg_gen_or_tl(ret, ret, cpu_Zf);
        break;
    /* HI */
    case 0x0D:
        /* !C & !Z */
        tcg_gen_xori_tl(nC, cpu_Cf, 1);
        tcg_gen_xori_tl(nZ, cpu_Zf, 1);

        tcg_gen_and_tl(ret, nC, nZ);
        break;
    /* LS */
    case 0x0E:
        /* C & Z */
        tcg_gen_or_tl(ret, cpu_Cf, cpu_Zf);
        break;
    /* PNZ */
    case 0x0F:
        /* !N & !Z */
        tcg_gen_xori_tl(nN, cpu_Nf, 1);
        tcg_gen_xori_tl(nZ, cpu_Zf, 1);

        tcg_gen_and_tl(ret, nN, nZ);
        break;
    }

    tcg_temp_free_i32(c1);
    tcg_temp_free_i32(nZ);
    tcg_temp_free_i32(nN);
    tcg_temp_free_i32(nV);
    tcg_temp_free_i32(nC);
}

#define MEMIDX (ctx->mem_idx)

void arc_gen_set_memory(DisasCtxt *ctx, TCGv vaddr, int size,
        TCGv src, bool sign_extend)
{
    switch (size) {
    case 0x00:
        tcg_gen_qemu_st_tl(src, vaddr, MEMIDX, MO_UL);
        break;

    case 0x01:
        if (sign_extend) {
            tcg_gen_qemu_st_tl(src, vaddr, MEMIDX, MO_SB);
        } else {
            tcg_gen_qemu_st_tl(src, vaddr, MEMIDX, MO_UB);
        }
        break;

    case 0x02:
        if (sign_extend) {
            tcg_gen_qemu_st_tl(src, vaddr, MEMIDX, MO_SW);
        } else {
            tcg_gen_qemu_st_tl(src, vaddr, MEMIDX, MO_UW);
        }
        break;

    case 0x03:
        assert(!"reserved");
        break;
    }
}

void arc_gen_get_memory(DisasCtxt *ctx, TCGv dest, TCGv vaddr,
        int size, bool sign_extend)
{
    switch (size) {
    case 0x00:
        tcg_gen_qemu_ld_tl(dest, vaddr, MEMIDX, MO_UL);
        break;

    case 0x01:
        if (sign_extend) {
            tcg_gen_qemu_ld_tl(dest, vaddr, MEMIDX, MO_SB);
        } else {
            tcg_gen_qemu_ld_tl(dest, vaddr, MEMIDX, MO_UB);
        }
        break;

    case 0x02:
        if (sign_extend) {
            tcg_gen_qemu_ld_tl(dest, vaddr, MEMIDX, MO_SW);
        } else {
            tcg_gen_qemu_ld_tl(dest, vaddr, MEMIDX, MO_UW);
        }
        break;

    case 0x03:
        assert(!"reserved");
        break;
    }
}


void arc_gen_no_further_loads_pending(DisasCtxt *ctx, TCGv ret)
{
    tcg_gen_movi_tl(ret, 1);
}

void arc_gen_set_debug(DisasCtxt *ctx, bool value)
{
    /* TODO: Could not find a reson to set this. */
}

extern bool enabled_interrupts;
void
arc_gen_execute_delayslot(DisasCtxt *ctx, TCGv bta, TCGv take_branch)
{
    static int in_delay_slot = false;

    assert(ctx->insn.limm_p == 0 && !in_delay_slot);

    if (ctx->insn.limm_p == 0 && !in_delay_slot) {
        in_delay_slot = true;
        uint32_t cpc = ctx->cpc;
        uint32_t pcl = ctx->pcl;
        insn_t insn = ctx->insn;

        ctx->cpc = ctx->npc;
        ctx->pcl = ctx->cpc & 0xfffffffc;

        ++ctx->ds;

        TCGLabel *do_not_set_bta_and_de = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_NE, take_branch, 1, do_not_set_bta_and_de);
        /*
         * In case an exception should be raised during the execution
         * of delay slot, bta value is used to set erbta.
         */
        tcg_gen_mov_tl(cpu_bta, bta);
        /* We are in a delay slot */
        tcg_gen_mov_tl(cpu_DEf, take_branch);
        gen_set_label(do_not_set_bta_and_de);

        tcg_gen_movi_tl(cpu_is_delay_slot_instruction, 1);

        /* Set the pc to the next pc */
        tcg_gen_movi_tl(cpu_pc, ctx->npc);
        /* Necessary for the likely call to restore_state_to_opc() */
        tcg_gen_insn_start(ctx->npc);

        DisasJumpType type = ctx->base.is_jmp;
        enabled_interrupts = false;

        /*
         * In case we might be in a situation where the delayslot is in a
         * different MMU page. Make a fake exception to interrupt
         * delayslot execution in the context of the branch.
         * The delayslot will then be re-executed in isolation after the
         * branch code has set bta and DEf status flag.
         */
        if ((cpc & PAGE_MASK) < 0x80000000 &&
            (cpc & PAGE_MASK) != (ctx->cpc & PAGE_MASK)) {
            in_delay_slot = false;
            TCGv dpc = tcg_const_local_i32(ctx->npc);
            tcg_gen_mov_tl(cpu_pc, dpc);
            gen_helper_fake_exception(cpu_env, dpc);
            tcg_temp_free_i32(dpc);
            return;
        }

        decode_opc(ctx->env, ctx);
        enabled_interrupts = true;
        ctx->base.is_jmp = type;

        tcg_gen_movi_tl(cpu_DEf, 0);
        tcg_gen_movi_tl(cpu_is_delay_slot_instruction, 0);

        /* Restore the pc back */
        tcg_gen_movi_tl(cpu_pc, cpc);
        /* Again, restore_state_to_opc() must use recent value */
        tcg_gen_insn_start(cpc);

        assert(ctx->base.is_jmp == DISAS_NEXT);

        --ctx->ds;

        /* Restore old values.  */
        ctx->cpc = cpc;
        ctx->pcl = pcl;
        ctx->insn = insn;
        in_delay_slot = false;
    }
    return;
}


/* dest = src1 - src2. Compute C, N, V and Z flags */
void arc_gen_sub_Cf(TCGv ret, TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_not_tl(t1, src1);       /* t1 = ~src1                 */
    tcg_gen_and_tl(t2, t1, src2);   /* t2 = ~src1 & src2          */
    tcg_gen_or_tl(t3, t1, src2);    /* t3 = (~src1 | src2) & dest */
    tcg_gen_and_tl(t3, t3, dest);
    /* t2 = ~src1 & src2 | ~src1 & dest | dest & src2 */
    tcg_gen_or_tl(t2, t2, t3);
    tcg_gen_shri_tl(ret, t2, 31);   /* Cf = t2(31)                */

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}


void arc_gen_get_bit(TCGv ret, TCGv a, TCGv pos)
{
    tcg_gen_rotr_i32(ret, a, pos);
    tcg_gen_andi_tl(ret, ret, 1);
}

/* accumulator += b32 * c32 */
void arc_gen_mac(TCGv phi, TCGv_i32 b32, TCGv_i32 c32)
{
    TCGv_i32 plo = tcg_temp_new_i32();
    tcg_gen_muls2_i32(plo, phi, b32, c32);

    /* Adding the product to the accumulator */
    tcg_gen_add2_i32(cpu_acclo, cpu_acchi, cpu_acclo, cpu_acchi, plo, phi);
    tcg_temp_free(plo);
}

/* Unsigned version of mac */
void arc_gen_macu(TCGv phi, TCGv_i32 b32, TCGv_i32 c32)
{
    TCGv_i32 plo = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(plo, phi, b32, c32);

    /* Adding the product to the accumulator */
    tcg_gen_add2_i32(cpu_acclo, cpu_acchi, cpu_acclo, cpu_acchi, plo, phi);
    tcg_temp_free(plo);
}

/* TODO: A better name would be tcg_gen_shil_i32() */
void tcg_gen_shlfi_i32(TCGv a, int b, TCGv c)
{
    TCGv tmp = tcg_temp_new_i32();
    tcg_gen_movi_i32(tmp, b);
    tcg_gen_shl_i32(a, tmp, c);
    tcg_temp_free(tmp);
}

/* TODO: A gen_helper() would be better for this purpose */
void arc_gen_extract_bits(TCGv ret, TCGv a, TCGv start, TCGv end)
{
    TCGv tmp1 = tcg_temp_new_i32();

    tcg_gen_shr_i32(ret, a, end);

    tcg_gen_sub_i32(tmp1, start, end);
    tcg_gen_addi_i32(tmp1, tmp1, 1);
    tcg_gen_shlfi_i32(tmp1, 1, tmp1);
    tcg_gen_subi_i32(tmp1, tmp1, 1);

    tcg_gen_and_i32(ret, ret, tmp1);

    tcg_temp_free(tmp1);
}


void arc_gen_get_register(TCGv ret, enum arc_registers reg)
{
    switch (reg) {
    case R_SP:
        tcg_gen_mov_i32(ret, cpu_sp);
        break;
    case R_STATUS32:
        gen_helper_get_status32(ret, cpu_env);
        break;
    case R_ACCLO:
        tcg_gen_mov_i32(ret, cpu_acclo);
        break;
    case R_ACCHI:
        tcg_gen_mov_i32(ret, cpu_acchi);
        break;
    default:
        g_assert_not_reached();
    }
}


void arc_gen_set_register(enum arc_registers reg, TCGv value)
{
    switch (reg) {
    case R_SP:
        tcg_gen_mov_i32(cpu_sp, value);
        break;
    case R_STATUS32:
        gen_helper_set_status32(cpu_env, value);
        break;
    case R_ACCLO:
        tcg_gen_mov_i32(cpu_acclo, value);
        break;
    case R_ACCHI:
        tcg_gen_mov_i32(cpu_acchi, value);
        break;
    default:
        g_assert_not_reached();
    }
}


/* TODO: Get this from props ... */
void arc_has_interrupts(DisasCtxt *ctx, TCGv ret)
{
    tcg_gen_movi_i32(ret, 1);
}

/*
 ***************************************
 * Statically inferred return function *
 ***************************************
 */

TCGv arc_gen_next_reg(const DisasCtxt *ctx, TCGv reg)
{
    int i;
    for (i = 0; i < 64; i += 2) {
        if (reg == cpu_r[i]) {
            return cpu_r[i + 1];
        }
    }
    /* Check if REG is an odd register. */
    for (i = 1; i < 64; i += 2) {
        /* If so, that is unsanctioned. */
        if (reg == cpu_r[i]) {
            arc_gen_excp(ctx, EXCP_INST_ERROR, 0, 0);
            return NULL;
        }
    }
    /* REG was not a register after all. */
    g_assert_not_reached();

    /* We never get here, but to accommodate -Werror ... */
    return NULL;
}

bool arc_target_has_option(enum target_options option)
{
    /* TODO: Fill with meaningful cases. */
    switch (option) {
    case LL64_OPTION:
        return true;
        break;
    default:
        break;
    }
    return false;
}


bool arc_is_instruction_operand_a_register(const DisasCtxt *ctx, int nop)
{
    assert(nop < ctx->insn.n_ops);
    operand_t operand = ctx->insn.operands[nop];

    return (operand.type & ARC_OPERAND_IR) != 0;
}


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
