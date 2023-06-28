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

#define TB_NONE_STATS (0)  /* no stats */
#define TB_EXEC_STATS (1 << 0)
#define TB_JIT_STATS  (1 << 1)
#define TB_ALL_STATS  (TB_EXEC_STATS | TB_JIT_STATS)

/* TBStatistic collection controls */
void enable_collect_tb_stats(void);
void disable_collect_tb_stats(void);
bool tb_stats_collection_enabled(void);
bool tb_stats_collection_disabled(void);

uint32_t get_tbstats_flag(void);
void set_tbstats_flag(uint32_t flag);

#endif
