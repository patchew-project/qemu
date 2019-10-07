/*
 * TB Stats common dump functions across sysemu/linux-user
 *
 * Copyright (c) 2019 Linaro
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _TB_STATS_DUMP_H_
#define _TB_STATS_DUMP_H_

/**
 * tb_stats_dump: dump final tb_stats at end of execution
 */
#ifdef CONFIG_TCG
void tb_stats_dump(void);
#else
static inline void tb_stats_dump(void) { /* do nothing */ };
#endif

#endif /* _TB_STATS_DUMP_H_ */
