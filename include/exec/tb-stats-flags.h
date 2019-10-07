/*
 * QEMU System Emulator, Code Quality Monitor System
 *
 * We define the flags and control bits here to avoid complications of
 * including TCG/CPU information in common code.
 *
 * Copyright (c) 2019 Vanderson M. do Rosario <vandersonmr2@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TB_STATS_FLAGS
#define TB_STATS_FLAGS

#define TB_NOTHING    (1 << 0)
#define TB_EXEC_STATS (1 << 1)
#define TB_JIT_STATS  (1 << 2)
#define TB_JIT_TIME   (1 << 3)
#define TB_PAUSED     (1 << 4)

/* TBStatistic collection controls */
void enable_collect_tb_stats(void);
void disable_collect_tb_stats(void);
void pause_collect_tb_stats(void);
bool tb_stats_collection_enabled(void);
bool tb_stats_collection_paused(void);

uint32_t get_default_tbstats_flag(void);
void set_default_tbstats_flag(uint32_t);

#endif
