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
    TB_STATS_DISABLED = 0,
    TB_STATS_RUNNING,
    TB_STATS_PAUSED,
    TB_STATS_STOPPED
};

static enum TBStatsStatus tcg_collect_tb_stats;

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
    tcg_collect_tb_stats = TB_STATS_PAUSED;
}

void pause_collect_tb_stats(void)
{
    tcg_collect_tb_stats = TB_STATS_STOPPED;
}

bool tb_stats_collection_enabled(void)
{
    return tcg_collect_tb_stats == TB_STATS_RUNNING;
}

bool tb_stats_collection_paused(void)
{
    return tcg_collect_tb_stats == TB_STATS_PAUSED;
}
