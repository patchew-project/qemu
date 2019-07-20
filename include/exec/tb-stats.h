#ifndef TB_STATS_H

#define TB_STATS_H

#include "exec/cpu-common.h"
#include "exec/tb-context.h"
#include "tcg.h"

#define tb_stats_enabled(tb, JIT_STATS) \
    (tb && tb->tb_stats && (tb->tb_stats->stats_enabled & JIT_STATS))

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

    uint32_t stats_enabled;

    /* Execution stats */
    struct {
        unsigned long total;
        unsigned long atomic;
    } executions;

    struct {
        unsigned num_guest_inst;
        unsigned num_host_inst;
        unsigned num_tcg_ops;
        unsigned num_tcg_ops_opt;
        unsigned spills;
    } code;

    struct {
        unsigned long total;
        unsigned long uncached;
        unsigned long spanning;
    } translations;
};

bool tb_stats_cmp(const void *ap, const void *bp);

#endif
