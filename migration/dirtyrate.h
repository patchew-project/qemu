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
#define DIRTYRATE_SAMPLE_PAGE_SIZE      4096
#define DIRTYRATE_PAGE_SIZE_SHIFT       12
#define BLOCK_INFO_MAX_LEN              256
#define PAGE_SIZE_SHIFT                 20

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

struct dirtyrate_statistics {
    unsigned int total_dirty_samples;
    unsigned int total_sample_count;
    unsigned long total_block_mem_MB;
    int64_t dirty_rate;
};


/* 
 * Store dirtypage info for each block.
 */
struct block_dirty_info {
    char idstr[BLOCK_INFO_MAX_LEN];
    uint8_t *block_addr;
    unsigned long block_pages;
    unsigned long *sample_page_vfn;
    unsigned int sample_pages_count;
    unsigned int sample_dirty_count;
    uint8_t *hash_result;
};

void *get_dirtyrate_thread(void *arg);
#endif

