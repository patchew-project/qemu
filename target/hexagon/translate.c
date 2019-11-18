/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 * Hexagon translation
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "tcg-op.h"
#include "disas/disas.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "translate.h"
#include "regs.h"

#include "exec/translator.h"
#include "exec/log.h"

TCGv hex_gpr[TOTAL_PER_THREAD_REGS];
TCGv hex_pred[NUM_PREGS];


#include "exec/gen-icount.h"

void gen_exception(int excp)
{
    TCGv_i32 helper_tmp = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
}

void gen_exception_debug(void)
{
    gen_exception(EXCP_DEBUG);
}

static void decode_packet(CPUHexagonState *env, DisasContext *ctx)
{
    size4u_t words[4];
    int i;
    /* Brute force way to make sure current PC is set */
    tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->base.pc_next);

    for (i = 0; i < 4; i++) {
        words[i] = cpu_ldl_code(env, ctx->base.pc_next + i * sizeof(size4u_t));
    }
    /*
     * Brute force just enough to get the first program to execute.
     */
    switch (words[0]) {
    case 0x7800c806:                                /* r6 = #64 */
        tcg_gen_movi_tl(hex_gpr[6], 64);
        ctx->base.pc_next += 4;
        break;
    case 0x7800c020:                                /* r0 = #1 */
        tcg_gen_movi_tl(hex_gpr[0], 1);
        ctx->base.pc_next += 4;
        break;
    case 0x00044002:
        if (words[1] == 0x7800c001) {               /* r1 = ##0x400080 */
            tcg_gen_movi_tl(hex_gpr[1], 0x400080);
            ctx->base.pc_next += 8;
        } else {
            printf("ERROR: Unknown instruction 0x%x\n", words[1]);
            g_assert_not_reached();
        }
        break;
    case 0x7800c0e2:                                /* r2 = #7 */
        tcg_gen_movi_tl(hex_gpr[2], 7);
        ctx->base.pc_next += 4;
        break;
    case 0x5400c004:                               /* trap0(#1) */
    {
        TCGv excp_trap0 = tcg_const_tl(HEX_EXCP_TRAP0);
        gen_helper_raise_exception(cpu_env, excp_trap0);
        tcg_temp_free(excp_trap0);
        ctx->base.pc_next += 4;
        break;
    }
    case 0x7800cbc6:                               /* r6 = #94 */
        tcg_gen_movi_tl(hex_gpr[6], 94);
        ctx->base.pc_next += 4;
        break;
    case 0x7800cba6:                               /* r6 = #93 */
        tcg_gen_movi_tl(hex_gpr[6], 93);
        ctx->base.pc_next += 4;
        break;
    case 0x7800c000:                               /* r0 = #0 */
        tcg_gen_movi_tl(hex_gpr[0], 0);
        ctx->base.pc_next += 4;
        break;
    default:
        ctx->base.is_jmp = DISAS_TOO_MANY;
        ctx->base.pc_next += 4;
    }
}

static void hexagon_tr_init_disas_context(DisasContextBase *dcbase,
                                          CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->mem_idx = ctx->base.tb->flags & TB_FLAGS_MMU_MASK;
}

static void hexagon_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void hexagon_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static bool hexagon_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
                                        const CPUBreakpoint *bp)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->base.pc_next);
    ctx->base.is_jmp = DISAS_NORETURN;
    gen_exception_debug();
    /*
     * The address covered by the breakpoint must be included in
     * [tb->pc, tb->pc + tb->size) in order to for it to be
     * properly cleared -- thus we increment the PC here so that
     * the logic setting tb->size below does the right thing.
     */
    ctx->base.pc_next += 4;
    return true;
}


static void hexagon_tr_translate_packet(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUHexagonState *env = cpu->env_ptr;

    decode_packet(env, ctx);

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_start;

        page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
        if (ctx->base.pc_next - page_start >= TARGET_PAGE_SIZE) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }

#ifdef DEBUG_HEX
        /* When debugging, force the end of the TB after each packet */
        if (ctx->base.pc_next - ctx->base.pc_first >= 0x04) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
#endif
    }
}

static void hexagon_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_TOO_MANY:
        tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->base.pc_next);
        if (ctx->base.singlestep_enabled) {
            gen_exception_debug();
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void hexagon_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
    log_target_disas(cpu, dcbase->pc_first, dcbase->tb->size);
}


static const TranslatorOps hexagon_tr_ops = {
    .init_disas_context = hexagon_tr_init_disas_context,
    .tb_start           = hexagon_tr_tb_start,
    .insn_start         = hexagon_tr_insn_start,
    .breakpoint_check   = hexagon_tr_breakpoint_check,
    .translate_insn     = hexagon_tr_translate_packet,
    .tb_stop            = hexagon_tr_tb_stop,
    .disas_log          = hexagon_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext ctx;

    translator_loop(&hexagon_tr_ops, &ctx.base, cs, tb, max_insns);
}

void hexagon_translate_init(void)
{
    int i;

    for (i = 0; i < TOTAL_PER_THREAD_REGS; i++) {
        hex_gpr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, gpr[i]), hexagon_regnames[i]);
    }
    for (i = 0; i < NUM_PREGS; i++) {
        hex_pred[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, pred[i]), hexagon_prednames[i]);
    }
}

