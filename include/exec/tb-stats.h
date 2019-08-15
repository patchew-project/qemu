#ifndef TB_STATS_H

#define TB_STATS_H

#include "exec/cpu-common.h"
#include "exec/tb-context.h"
#include "tcg.h"

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

    uint32_t stats_enabled;

    /* Execution stats */
    struct {
        unsigned long normal;
        unsigned long atomic;
    } executions;

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

    struct {
        uint64_t restore;
        uint64_t restore_count;
        uint64_t interm;
        uint64_t code;
        uint64_t opt;
        uint64_t la;
    } time;

    /* HMP information - used for referring to previous search */
    int display_id;

    /* current TB linked to this TBStatistics */
    TranslationBlock *tb;
};

bool tb_stats_cmp(const void *ap, const void *bp);

void dump_jit_exec_time_info(uint64_t dev_time);

void set_tbstats_flags(uint32_t flags);
void init_tb_stats_htable_if_not(void);

void dump_jit_profile_info(TCGProfile *s);

struct TbstatsCommand {
    enum TbstatsCmd cmd;
    uint32_t level;
};

void do_hmp_tbstats_safe(CPUState *cpu, run_on_cpu_data icmd);

/**
 * dump_coverset_info: report the hottest blocks to cover n% of execution
 *
 * @percentage: cover set percentage
 * @use_monitor: redirect output to monitor
 *
 * Report the hottest blocks to either the log or monitor
 */
void dump_coverset_info(int percentage, bool use_monitor);


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

void dump_tb_cfg(int id, int depth, int log_flags);

/* TBStatistic collection controls */
void enable_collect_tb_stats(void);
void disable_collect_tb_stats(void);
void pause_collect_tb_stats(void);
bool tb_stats_collection_enabled(void);
bool tb_stats_collection_paused(void);

void set_default_tbstats_flag(uint32_t flag);
uint32_t get_default_tbstats_flag(void);

#endif
