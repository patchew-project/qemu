/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op.h"
#include "trace-tcg.h"
#include "exec/exec-all.h"
#include "exec/gen-icount.h"
#include "exec/log.h"
#include "exec/translator.h"

/* Pairs with tcg_clear_temp_count.
   To be called by #TranslatorOps.{translate_insn,tb_stop} if
   (1) the target is sufficiently clean to support reporting,
   (2) as and when all temporaries are known to be consumed.
   For most targets, (2) is at the end of translate_insn.  */
void translator_loop_temp_check(DisasContextBase *db)
{
    if (tcg_check_temp_count()) {
        qemu_log("warning: TCG temporary leaks before "
                 TARGET_FMT_lx "\n", db->pc_next);
    }
}

void translator_loop(const TranslatorOps *ops, DisasContextBase *db,
                     CPUState *cpu, TranslationBlock *tb)
{
    target_ulong pc_bbl, pc_insn = 0;
    bool translated_insn = false;
    int max_insns;

    /* Initialize DisasContext */
    db->tb = tb;
    db->pc_first = tb->pc;
    db->pc_next = db->pc_first;
    db->is_jmp = DISAS_NEXT;
    db->num_insns = 0;
    db->singlestep_enabled = cpu->singlestep_enabled;

    /* Instruction counting */
    max_insns = db->tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }
    if (db->singlestep_enabled || singlestep) {
        max_insns = 1;
    }

    max_insns = ops->init_disas_context(db, cpu, max_insns);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    /* Reset the temp count so that we can identify leaks */
    tcg_clear_temp_count();

    /* Tracking gen_goto_tb / gen_exit_tb */
    pc_bbl = db->pc_first;
    tcg_ctx.disas.seen_goto_tb = false;
    tcg_ctx.disas.in_guest_code = false;

    /* Start translating.  */
    gen_tb_start(db->tb);
    ops->tb_start(db, cpu);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    while (true) {
        TCGv_i32 insn_size_tcg = 0;
        int insn_size_opcode_idx;

        /* Tracing after (previous instruction) */
        if (db->num_insns > 0) {
            trace_guest_inst_after_tcg(cpu, tcg_ctx.tcg_env, pc_insn);
        }
        pc_insn = db->pc_next;

        db->num_insns++;
        if (db->num_insns == 1) {
            tcg_ctx.disas.in_guest_code = true;
            tcg_ctx.disas.inline_label = NULL;
        }

        ops->insn_start(db, cpu);
        tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

        /* Pass breakpoint hits to target for further processing */
        if (unlikely(!QTAILQ_EMPTY(&cpu->breakpoints))) {
            CPUBreakpoint *bp;
            QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
                if (bp->pc == db->pc_next) {
                    if (ops->breakpoint_check(db, cpu, bp)) {
                        break;
                    }
                }
            }
            /* The breakpoint_check hook may use DISAS_TOO_MANY to indicate
               that only one more instruction is to be executed.  Otherwise
               it should use DISAS_NORETURN when generating an exception,
               but may use a DISAS_TARGET_* value for Something Else.  */
            if (db->is_jmp > DISAS_TOO_MANY) {
                break;
            }
        }

        /* Tracing before */
        if (db->num_insns == 1) {
            trace_guest_bbl_before_tcg(cpu, tcg_ctx.tcg_env, db->pc_first);
        }
        trace_guest_inst_before_tcg(cpu, tcg_ctx.tcg_env, pc_insn);
        if (TRACE_GUEST_INST_INFO_BEFORE_EXEC_ENABLED) {
            insn_size_tcg = tcg_temp_new_i32();
            insn_size_opcode_idx = tcg_op_buf_count();
            tcg_gen_movi_i32(insn_size_tcg, 0xdeadbeef);

            trace_guest_inst_info_before_tcg(
                cpu, tcg_ctx.tcg_env, pc_insn, insn_size_tcg);

            tcg_temp_free_i32(insn_size_tcg);
        }

        /* Disassemble one instruction.  The translate_insn hook should
           update db->pc_next and db->is_jmp to indicate what should be
           done next -- either exiting this loop or locate the start of
           the next instruction.  */
        if (db->num_insns == max_insns && (db->tb->cflags & CF_LAST_IO)) {
            /* Accept I/O on the last instruction.  */
            gen_io_start();
            ops->translate_insn(db, cpu);
            gen_io_end();
        } else {
            ops->translate_insn(db, cpu);
        }

        translated_insn = true;
        /* Tracing after (patched values) */
        if (TRACE_GUEST_INST_INFO_BEFORE_EXEC_ENABLED) {
            unsigned int insn_size = db->pc_next - pc_insn;
            tcg_set_insn_param(insn_size_opcode_idx, 1, insn_size);
        }

        /* Stop translation if translate_insn so indicated.  */
        if (db->is_jmp != DISAS_NEXT) {
            break;
        }

        /* Stop translation if the output buffer is full,
           or we have executed all of the allowed instructions.  */
        if (tcg_op_buf_full() || db->num_insns >= max_insns) {
            db->is_jmp = DISAS_TOO_MANY;
            break;
        }
    }

    /* Tracing after */
    if (TRACE_GUEST_BBL_AFTER_ENABLED ||
        TRACE_GUEST_INST_AFTER_ENABLED) {
        tcg_ctx.disas.in_guest_code = false;
        if (tcg_ctx.disas.inline_label == NULL) {
            tcg_ctx.disas.inline_label = gen_new_inline_label();
        }

        gen_set_inline_region_begin(tcg_ctx.disas.inline_label);

        if (TRACE_GUEST_INST_AFTER_ENABLED && translated_insn) {
            trace_guest_inst_after_tcg(cpu, tcg_ctx.tcg_env, pc_insn);
        }
        if (TRACE_GUEST_BBL_AFTER_ENABLED) {
            trace_guest_bbl_after_tcg(cpu, tcg_ctx.tcg_env, pc_bbl);
        }

        gen_set_inline_region_end(tcg_ctx.disas.inline_label);
    }

    /* Emit code to exit the TB, as indicated by db->is_jmp.  */
    ops->tb_stop(db, cpu);
    gen_tb_end(db->tb, db->num_insns);

    /* The disas_log hook may use these values rather than recompute.  */
    db->tb->size = db->pc_next - db->pc_first;
    db->tb->icount = db->num_insns;

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(db->pc_first)) {
        qemu_log_lock();
        qemu_log("----------------\n");
        ops->disas_log(db, cpu);
        qemu_log("\n");
        qemu_log_unlock();
    }
#endif
}


void translator__gen_goto_tb(TCGContext *ctx)
{
    if (ctx->disas.in_guest_code &&
        (TRACE_GUEST_BBL_AFTER_ENABLED ||
         TRACE_GUEST_INST_AFTER_ENABLED)) {
        if (ctx->disas.inline_label == NULL) {
            ctx->disas.inline_label = gen_new_inline_label();
        }
        gen_set_inline_point(ctx->disas.inline_label);
        /* disable next exit_tb */
        ctx->disas.seen_goto_tb = true;
    }
}

void translator__gen_exit_tb(TCGContext *ctx)
{
    if (ctx->disas.in_guest_code && !ctx->disas.seen_goto_tb &&
        (TRACE_GUEST_BBL_AFTER_ENABLED ||
         TRACE_GUEST_INST_AFTER_ENABLED)) {
        if (ctx->disas.inline_label == NULL) {
            ctx->disas.inline_label = gen_new_inline_label();
        }
        gen_set_inline_point(ctx->disas.inline_label);
        /* enable next exit_tb */
        ctx->disas.seen_goto_tb = false;
    }
}
