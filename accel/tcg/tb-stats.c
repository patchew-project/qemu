/*
 * QEMU System Emulator, Code Quality Monitor System
 *
 * Copyright (c) 2019 Vanderson M. do Rosario <vandersonmr2@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "disas/disas.h"

#include "exec/tb-stats.h"
#include "tb-context.h"

/* TBStatistic collection controls */
enum TBStatsStatus {
    TB_STATS_STOPPED = 0,
    TB_STATS_RUNNING
};

static enum TBStatsStatus tcg_collect_tb_stats;
static uint32_t tbstats_flag;

void init_tb_stats_htable(void)
{
    if (!tb_ctx.tb_stats.map && tb_stats_collection_enabled()) {
        qht_init(&tb_ctx.tb_stats, tb_stats_cmp,
                CODE_GEN_HTABLE_SIZE, QHT_MODE_AUTO_RESIZE);
    }
}

void enable_collect_tb_stats(void)
{
    tcg_collect_tb_stats = TB_STATS_RUNNING;
    init_tb_stats_htable();
}

void disable_collect_tb_stats(void)
{
    tcg_collect_tb_stats = TB_STATS_STOPPED;
}

bool tb_stats_collection_enabled(void)
{
    return tcg_collect_tb_stats == TB_STATS_RUNNING;
}

uint32_t get_tbstats_flag(void)
{
    return tbstats_flag;
}

void set_tbstats_flag(uint32_t flag)
{
    tbstats_flag = flag;
}

bool tb_stats_enabled(TranslationBlock *tb, uint32_t flag)
{
    return tb_stats_collection_enabled() &&
           tb->tb_stats &&
           (tbstats_flag & flag);
}
