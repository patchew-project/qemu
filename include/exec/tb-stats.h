#ifndef TB_STATS_H

#define TB_STATS_H

#include "exec/cpu-common.h"
#include "exec/tb-context.h"
#include "tcg.h"

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
    /* current TB linked to this TBStatistics */
    TranslationBlock *tb;
};

bool tb_stats_cmp(const void *ap, const void *bp);

void init_tb_stats_htable_if_not(void);

/* TBStatistic collection controls */
enum TBStatsStatus { TB_STATS_RUNNING, TB_STATS_PAUSED, TB_STATS_STOPPED };
extern int tcg_collect_tb_stats;

void enable_collect_tb_stats(void);
void disable_collect_tb_stats(void);
void pause_collect_tb_stats(void);
bool tb_stats_collection_enabled(void);
bool tb_stats_collection_paused(void);

#endif
