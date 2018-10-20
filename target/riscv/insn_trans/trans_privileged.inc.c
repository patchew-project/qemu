/*
 * RISC-V translation routines for the RISC-V privileged instructions.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2018 Peer Adelt, peer.adelt@hni.uni-paderborn.de
 *                    Bastian Koppelmann, kbastian@mail.uni-paderborn.de
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

static bool trans_ecall(DisasContext *ctx, arg_ecall *a, uint32_t insn)
{
    /* always generates U-level ECALL, fixed in do_interrupt handler */
    generate_exception(ctx, RISCV_EXCP_U_ECALL);
    tcg_gen_exit_tb(NULL, 0); /* no chaining */
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_ebreak(DisasContext *ctx, arg_ebreak *a, uint32_t insn)
{
    generate_exception(ctx, RISCV_EXCP_BREAKPOINT);
    tcg_gen_exit_tb(NULL, 0); /* no chaining */
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_uret(DisasContext *ctx, arg_uret *a, uint32_t insn)
{
    gen_exception_illegal(ctx);
    return true;
}

static bool trans_sret(DisasContext *ctx, arg_sret *a, uint32_t insn)
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    CPURISCVState *env = current_cpu->env_ptr;
    if (riscv_has_ext(env, RVS)) {
        gen_helper_sret(cpu_pc, cpu_env, cpu_pc);
        tcg_gen_exit_tb(NULL, 0); /* no chaining */
        ctx->base.is_jmp = DISAS_NORETURN;
    } else {
        gen_exception_illegal(ctx);
    }
    return true;
#else
    return false;
#endif
}

static bool trans_hret(DisasContext *ctx, arg_hret *a, uint32_t insn)
{
    gen_exception_illegal(ctx);
    return true;
}

static bool trans_mret(DisasContext *ctx, arg_mret *a, uint32_t insn)
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    gen_helper_mret(cpu_pc, cpu_env, cpu_pc);
    tcg_gen_exit_tb(NULL, 0); /* no chaining */
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
#else
    return false;
#endif
}

static bool trans_wfi(DisasContext *ctx, arg_wfi *a, uint32_t insn)
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
    gen_helper_wfi(cpu_env);
    return true;
#else
    return false;
#endif
}

static bool trans_sfence_vma(DisasContext *ctx, arg_sfence_vma *a,
                             uint32_t insn)
{
#ifndef CONFIG_USER_ONLY
    gen_helper_tlb_flush(cpu_env);
    return true;
#else
    return false;
#endif
}

static bool trans_sfence_vm(DisasContext *ctx, arg_sfence_vm *a, uint32_t insn)
{
#ifndef CONFIG_USER_ONLY
    gen_helper_tlb_flush(cpu_env);
    return true;
#else
    return false;
#endif
}
