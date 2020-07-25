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

/* take 256 pages per GB for cal dirty rate */
#define DIRTYRATE_DEFAULT_SAMPLE_PAGES    256

struct dirtyrate_config {
    uint64_t sample_pages_per_gigabytes;
    int64_t sample_period_seconds;
};

/*
 *  To record calculate dirty_rate status:
 *  0: initial status, calculating thread is not be created here.
 *  1: calculating thread is created.
 *  2: calculating thread is end, we can get result.
 */
typedef enum {
    CAL_DIRTY_RATE_INIT  = 0,
    CAL_DIRTY_RATE_ING   = 1,
    CAL_DIRTY_RATE_END   = 2,
} CalculatingDirtyRateStage;

void *get_dirtyrate_thread(void *arg);
#endif

