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
#include "exec/exec-all.h"
#include "exec/gen-icount.h"
#include "exec/log.h"
#include "exec/translator.h"


static inline void translate_block_tcg_check(const DisasContextBase *db)
{
    if (tcg_check_temp_count()) {
        error_report("warning: TCG temporary leaks before "TARGET_FMT_lx,
                     db->pc_next);
    }
}

void translate_block(const TranslatorOps *ops, DisasContextBase *db,
                     CPUState *cpu, TranslationBlock *tb)
{
    int max_insns;

    /* Initialize DisasContext */
    db->tb = tb;
    db->pc_first = tb->pc;
    db->pc_next = db->pc_first;
    db->is_jmp = DISAS_NEXT;
    db->num_insns = 0;
    db->singlestep_enabled = cpu->singlestep_enabled;
    ops->init_disas_context(db, cpu);

    /* Initialize globals */
    ops->init_globals(db, cpu);
    tcg_clear_temp_count();

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

    /* Start translating */
    gen_tb_start(db->tb);
    ops->tb_start(db, cpu);

    while (true) {
        db->num_insns++;
        ops->insn_start(db, cpu);

        /* Early exit before breakpoint checks */
        if (unlikely(db->is_jmp != DISAS_NEXT)) {
            break;
        }

        /* Pass breakpoint hits to target for further processing */
        if (unlikely(!QTAILQ_EMPTY(&cpu->breakpoints))) {
            CPUBreakpoint *bp;
            QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
                if (bp->pc == db->pc_next) {
                    BreakpointCheckType bp_check =
                        ops->breakpoint_check(db, cpu, bp);
                    switch (bp_check) {
                    case BC_MISS:
                        /* Target ignored this breakpoint, go to next */
                        break;
                    case BC_HIT_INSN:
                        /* Hit, keep translating */
                        /*
                         * TODO: if we're never going to have more than one
                         *       BP in a single address, we can simply use a
                         *       bool here.
                         */
                        goto done_breakpoints;
                    case BC_HIT_TB:
                        /* Hit, end TB */
                        goto done_generating;
                    default:
                        g_assert_not_reached();
                    }
                }
            }
        }
    done_breakpoints:

        /* Accept I/O on last instruction */
        if (db->num_insns == max_insns && (db->tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        /* Disassemble one instruction */
        db->pc_next = ops->translate_insn(db, cpu);

        /**************************************************/
        /* Conditions to stop translation                 */
        /**************************************************/

        /* Target-specific conditions set by disassembly */
        if (db->is_jmp != DISAS_NEXT) {
            break;
        }

        /* Too many instructions */
        if (tcg_op_buf_full() || db->num_insns >= max_insns) {
            db->is_jmp = DISAS_TOO_MANY;
            break;
        }

        translate_block_tcg_check(db);
    }

    ops->tb_stop(db, cpu);

    if (db->tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

done_generating:
    gen_tb_end(db->tb, db->num_insns);

    translate_block_tcg_check(db);

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

    db->tb->size = db->pc_next - db->pc_first;
    db->tb->icount = db->num_insns;
}
