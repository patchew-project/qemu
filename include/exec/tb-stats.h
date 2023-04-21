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
#include "exec/exec-all.h"
#include "tcg/tcg.h"

#include "exec/tb-stats-flags.h"

enum SortBy { SORT_BY_HOTNESS, SORT_BY_HG /* Host/Guest */, SORT_BY_SPILLS };
enum TbstatsCmd { START, PAUSE, STOP, FILTER };

#define tbs_stats_enabled(tbs, JIT_STATS) \
    (tbs && (tbs->stats_enabled & JIT_STATS))

#define tb_stats_enabled(tb, JIT_STATS) \
    (tb && tb->tb_stats && tbs_stats_enabled(tb->tb_stats, JIT_STATS))

#define stat_per_translation(stat, name) \
    (stat->translations.total ? stat->name / stat->translations.total : 0)

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
    target_ulong pc;
    uint32_t     flags;
    /* cs_base isn't included in the hash but we do check for matches */
    target_ulong cs_base;

    /* which stats are enabled for this TBStats */
    uint32_t stats_enabled;

    /* Execution stats */
    struct {
        unsigned long normal;
        unsigned long atomic;
        /* filled only when dumping x% cover set */
        uint16_t coverage;
    } executions;

    /* JIT Stats - protected by lock */
    QemuMutex jit_stats_lock;

    /* Sum of all operations for all translations */
    struct {
        unsigned num_guest_inst;
        unsigned num_tcg_ops;
        unsigned num_tcg_ops_opt;
        unsigned spills;

        /* CONFIG_PROFILE */
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

    /* Recover state from TB */
    uint64_t tb_restore_time;
    uint64_t tb_restore_count;

    struct {
        uint64_t ir;
        uint64_t ir_opt;
        uint64_t la;
        uint64_t code;
    } gen_times;

    /* HMP information - used for referring to previous search */
    int display_id;
};

bool tb_stats_cmp(const void *ap, const void *bp);

void init_tb_stats_htable(void);

void dump_jit_profile_info(TCGProfile *s, GString *buf);
void dump_jit_exec_time_info(uint64_t dev_time);

void set_tbstats_flags(uint32_t flags);

struct TbstatsCommand {
    enum TbstatsCmd cmd;
    uint32_t level;
};

void do_hmp_tbstats_safe(CPUState *cpu, run_on_cpu_data icmd);

/**
 * tbstats_reset_tbs: reset the linked array of TBs
 *
 * Reset the list of tbs for a given array. Should be called from
 * safe work during tb_flush.
 */
void tbstats_reset_tbs(void);

/**
 * dump_tbs_info: report the hottest blocks
 *
 * @count: the limit of hotblocks
 * @sort_by: property in which the dump will be sorted
 * @use_monitor: redirect output to monitor
 *
 * Report the hottest blocks to either the log or monitor
 */
void dump_tbs_info(int count, int sort_by, bool use_monitor);

/**
 * dump_tb_info: dump information about one TB
 *
 * @id: the display id of the block (from previous search)
 * @mask: the temporary logging mask
 * @Use_monitor: redirect output to monitor
 *
 * Re-run a translation of a block at addr for the purposes of debug output
 */
void dump_tb_info(int id, int log_mask, bool use_monitor);

#endif
