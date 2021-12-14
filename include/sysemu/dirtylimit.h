/*
 * dirty limit helper functions
 *
 * Copyright (c) 2021 CHINA TELECOM CO.,LTD.
 *
 * Authors:
 *  Hyman Huang(黄勇) <huangy81@chinatelecom.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_DIRTYRLIMIT_H
#define QEMU_DIRTYRLIMIT_H

#define DIRTYLIMIT_CALC_TIME_MS         1000    /* 1000ms */

/**
 * dirtylimit_calc_current
 *
 * get current dirty page rate for specified virtual CPU.
 */
int64_t dirtylimit_calc_current(int cpu_index);

/**
 * dirtylimit_calc_start
 *
 * start dirty page rate calculation thread.
 */
void dirtylimit_calc_start(void);

/**
 * dirtylimit_calc_quit
 *
 * quit dirty page rate calculation thread.
 */
void dirtylimit_calc_quit(void);

/**
 * dirtylimit_calc_state_init
 *
 * initialize dirty page rate calculation state.
 */
void dirtylimit_calc_state_init(int max_cpus);

/**
 * dirtylimit_calc_state_finalize
 *
 * finalize dirty page rate calculation state.
 */
void dirtylimit_calc_state_finalize(void);
#endif
