/*
 * QEMU System Emulator, Code Quality Monitor System
 *
 * Copyright (c) 2019 Vanderson M. do Rosario <vandersonmr2@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef TCG_TB_STATS_H
#define TCG_TB_STATS_H 1

#include "qemu/thread.h"
#include "exec/translation-block.h"

enum {
    TB_STATS_EXEC = 1u << 0,
    TB_STATS_JIT  = 1u << 1,

    TB_STATS_NONE = 0,
    TB_STATS_ALL  = TB_STATS_EXEC | TB_STATS_JIT,
};

extern uint32_t tb_stats_enabled;

/**
 * tb_stats_init:
 * @flags: TB_STATS_* flags to enable.
 *
 * Initialize translation block statistics, enabling @flags.
 * If @flags is 0, disable all statistics.
 */
void tb_stats_init(uint32_t flags);

/*
 * This struct stores statistics such as execution count of the
 * TranslationBlocks. Each sets of TBs for a given phys_pc/pc/flags
 * has its own TBStatistics which will persist over tb_flush.
 *
 * We include additional counters to track number of translations as
 * well as variants for compile flags.
 */
struct TBStatistics {
    tb_page_addr_t phys_pc;
    vaddr pc;
    uint32_t flags;
    uint64_t flags2;

    /* Execution stats */
    struct {
        unsigned long normal;
        unsigned long atomic;
        /* filled only when dumping x% cover set */
        double coverage;
    } executions;

    /* JIT Stats - protected by lock */
    QemuMutex jit_stats_lock;

    /* Sum of all operations for all translations */
    struct {
        unsigned long num_guest_inst;
        unsigned long num_tcg_ops;
        unsigned long num_tcg_ops_opt;
        unsigned long spills;

        unsigned long temps;
        unsigned long deleted_ops;
        unsigned long in_len;
        unsigned long out_len;
        unsigned long search_out_len;
    } code;

    struct {
        unsigned long total;
        unsigned long spanning;
    } translations;

    /*
     * All persistent (cached) TranslationBlocks using
     * this TBStats structure. Has to be reset on a tb_flush.
     */
    GPtrArray *tbs;
};

/**
 * tb_stats_enabled:
 * @tb: TranslationBlock
 * @f: flag to check
 *
 * Return true if any stats are enabled for @tb and
 * if @f is enabled globally.
 */
static inline bool tb_stats_enabled_for_tb(TranslationBlock *tb, uint32_t f)
{
    return unlikely(tb_stats_enabled & f) && tb->tb_stats;
}

/**
 * tb_stats_reset_tbs: reset the linked array of TBs
 *
 * Reset the list of tbs for a given array. Should be called from
 * safe work during tb_flush.
 */
void tb_stats_reset_tbs(void);

/**
 * tb_stats_lookup:
 *
 * If any tb_stats are enabled, return a new or existing struct
 * for the tuple (phys_pc, pc, flags, flags2).  To be used when
 * building a new TranslationBlock.
 */
TBStatistics *tb_stats_lookup(tb_page_addr_t phys_pc, vaddr pc,
                              uint32_t flags, uint64_t flags2);

#endif /* TCG_TB_STATS_H */
