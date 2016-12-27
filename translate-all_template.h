/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRANSLATE_ALL_TEMPLATE_H
#define TRANSLATE_ALL_TEMPLATE_H

/*
 * Include this header from a target-specific file, which must define the
 * target-specific functions declared below.
 *
 * These must be paired with instructions in "exec/translate-all_template.h".
 */


#include "cpu.h"
#include "qemu/error-report.h"


static void gen_intermediate_code_target_init_disas_context(
    DisasContext * restrict dc, CPUArchState * restrict env);

static void gen_intermediate_code_target_init_globals(
    DisasContext * restrict dc, CPUArchState * restrict env);

static void gen_intermediate_code_target_tb_start(
    DisasContext * restrict dc, CPUArchState * restrict env);

static void gen_intermediate_code_target_insn_start(
    DisasContext * restrict dc, CPUArchState * restrict env);

static BreakpointHitType gen_intermediate_code_target_breakpoint_hit(
    DisasContext * restrict dc, CPUArchState * restrict env,
    const CPUBreakpoint * restrict bp);

static target_ulong gen_intermediate_code_target_disas_insn(
    DisasContext * restrict dc, CPUArchState * restrict env);

static DisasJumpType gen_intermediate_code_target_stop_check(
    DisasContext * restrict dc, CPUArchState * restrict env);

static void gen_intermediate_code_target_stop(
    DisasContext * restrict dc, CPUArchState * restrict env);

static int gen_intermediate_code_target_get_disas_flags(
    const DisasContext *dc);


static inline void gen_intermediate_code_tcg_check(const DisasContext *dc)
{
    if (tcg_check_temp_count()) {
        error_report("warning: TCG temporary leaks before "TARGET_FMT_lx,
                     dc->base.pc_next);
    }
}

void gen_intermediate_code(CPUState *cpu, struct TranslationBlock *tb)
{
    CPUArchState *env = cpu->env_ptr;
    DisasContext dc1, *dc = &dc1;
    int max_insns;

    /* Initialize DisasContext */
    dc->base.tb = tb;
    dc->base.singlestep_enabled = cpu->singlestep_enabled;
    dc->base.pc_first = tb->pc;
    dc->base.pc_next = dc->base.pc_first;
    dc->base.jmp_type = DJ_NEXT;
    dc->base.num_insns = 0;
    gen_intermediate_code_target_init_disas_context(dc, env);

    /* Initialize globals */
    gen_intermediate_code_target_init_globals(dc, env);
    tcg_clear_temp_count();

    /* Instruction counting */
    max_insns = dc->base.tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }
    if (dc->base.singlestep_enabled || singlestep) {
        max_insns = 1;
    }

    /* Start translating */
    gen_tb_start(dc->base.tb);
    gen_intermediate_code_target_tb_start(dc, env);

    while (true) {
        CPUBreakpoint *bp;

        dc->base.num_insns++;
        gen_intermediate_code_target_insn_start(dc, env);

        /* Early exit before breakpoint checks */
        if (unlikely(dc->base.jmp_type != DJ_NEXT)) {
            break;
        }

        /* Pass breakpoint hits to target for further processing */
        bp = NULL;
        do {
            bp = cpu_breakpoint_get(cpu, dc->base.pc_next, bp);
            if (unlikely(bp)) {
                BreakpointHitType bh = gen_intermediate_code_target_breakpoint_hit(dc, env, bp);
                if (bh == BH_HIT_INSN) {
                    /* Hit, keep translating */
                    /*
                     * TODO: if we're never going to have more than one BP in a
                     *       single address, we can simply use a bool here.
                     */
                    break;
                } else if (bh == BH_HIT_TB) {
                    goto done_generating;
                }
            }
        } while (bp != NULL);

        /* Accept I/O on last instruction */
        if (dc->base.num_insns == max_insns &&
            (dc->base.tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        /* Disassemble one instruction */
        dc->base.pc_next = gen_intermediate_code_target_disas_insn(dc, env);

        /**************************************************/
        /* Conditions to stop translation                 */
        /**************************************************/

        /* Disassembly already set a stop condition */
        if (dc->base.jmp_type >= DJ_TARGET) {
            break;
        }

        /* Target-specific conditions */
        dc->base.jmp_type = gen_intermediate_code_target_stop_check(dc, env);
        if (dc->base.jmp_type >= DJ_TARGET) {
            break;
        }

        /* Too many instructions */
        if (tcg_op_buf_full() || dc->base.num_insns >= max_insns) {
            dc->base.jmp_type = DJ_TOO_MANY;
            break;
        }

        /*
         * Check if next instruction is on next page, which can cause an
         * exception.
         *
         * NOTE: Target-specific code must check a single instruction does not
         *       cross page boundaries; the first in the TB is always allowed to
         *       cross pages (never goes through this check).
         */
        if ((dc->base.pc_first & TARGET_PAGE_MASK)
            != (dc->base.pc_next & TARGET_PAGE_MASK)) {
            dc->base.jmp_type = DJ_TOO_MANY;
            break;
        }

        gen_intermediate_code_tcg_check(dc);
    }

    gen_intermediate_code_target_stop(dc, env);

    if (dc->base.tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

done_generating:
    gen_tb_end(dc->base.tb, dc->base.num_insns);

    gen_intermediate_code_tcg_check(dc);

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM) &&
        qemu_log_in_addr_range(dc->base.pc_first)) {
        qemu_log_lock();
        qemu_log("----------------\n");
        qemu_log("IN: %s\n", lookup_symbol(dc->base.pc_first));
        log_target_disas(cpu, dc->base.pc_first, dc->base.pc_next - dc->base.pc_first,
                         gen_intermediate_code_target_get_disas_flags(dc));
        qemu_log("\n");
        qemu_log_unlock();
    }
#endif

    dc->base.tb->size = dc->base.pc_next - dc->base.pc_first;
    dc->base.tb->icount = dc->base.num_insns;
}

#endif  /* TRANSLATE_ALL_TEMPLATE_H */
