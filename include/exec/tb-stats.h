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

#ifndef TB_STATS_H

#define TB_STATS_H

#include "exec/cpu-common.h"
#include "exec/translation-block.h"
#include "exec/tb-stats-flags.h"
#include "tcg/tcg.h"

typedef struct TBStatistics TBStatistics;

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
    /* cs_base isn't included in the hash but we do check for matches */
    uint64_t cs_base;

    /* Execution stats */
    struct {
        unsigned long normal;
        unsigned long atomic;
    } executions;

    /* JIT Stats - protected by lock */
    QemuMutex jit_stats_lock;

    /* Sum of all operations for all translations */
    struct {
        unsigned num_guest_inst;
        unsigned num_tcg_ops;
        unsigned num_tcg_ops_opt;
        unsigned spills;

        unsigned temps;
        unsigned deleted_ops;
        unsigned in_len;
        unsigned out_len;
        unsigned search_out_len;
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

bool tb_stats_cmp(const void *ap, const void *bp);

void init_tb_stats_htable(void);
bool tb_stats_enabled(TranslationBlock *tb, uint32_t flag);

void dump_jit_profile_info(GString *buf);

#endif
