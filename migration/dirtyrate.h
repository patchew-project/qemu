/*
 *  Dirtyrate common functions
 *
 *  Copyright (c) 2020 HUAWEI TECHNOLOGIES CO., LTD.
 *
 *  Authors:
 *  Chuan Zheng <zhengchuan@huawei.com>
 *
 *  This work is licensed under the terms of the GNU GPL, version 2 or later.
 *  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_DIRTYRATE_H
#define QEMU_MIGRATION_DIRTYRATE_H

/*
 * Sample 256 pages per GB as default.
 * TODO: Make it configurable.
 */
#define DIRTYRATE_DEFAULT_SAMPLE_PAGES            256

/* Take 1s as default for calculation duration */
#define DEFAULT_FETCH_DIRTYRATE_TIME_SEC          1

struct DirtyRateConfig {
    uint64_t sample_pages_per_gigabytes; /* sample pages per GB */
    int64_t sample_period_seconds; /* time duration between two sampling */
};

/*
 *  To record calculate dirty_rate status:
 *  0: initial status, calculating thread is not be created here.
 *  1: calculating thread is created.
 *  2: calculating thread is end, we can get result.
 */
typedef enum {
    CAL_DIRTY_RATE_INIT = 0,
    CAL_DIRTY_RATE_ACTIVE,
    CAL_DIRTY_RATE_END,
} CalculatingDirtyRateState;

void *get_dirtyrate_thread(void *arg);
#endif

