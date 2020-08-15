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

/*
 * Sample page size 4K as default.
 */
#define DIRTYRATE_SAMPLE_PAGE_SIZE                4096

/*
 * Record ramblock idstr
 */
#define RAMBLOCK_INFO_MAX_LEN                     256

#define QCRYPTO_HASH_LEN                          16

/*
 * minimum ramblock size to sampled
 */
#define MIN_RAMBLOCK_SIZE                        128

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

/*
 * Store dirtypage info for each ramblock.
 */
struct RamblockDirtyInfo {
    char idstr[RAMBLOCK_INFO_MAX_LEN]; /* idstr for each ramblock */
    uint8_t *ramblock_addr; /* base address of ramblock we measure */
    size_t ramblock_pages; /* sum of dividation by 4K pages for ramblock */
    size_t *sample_page_vfn; /* relative offset address for sampled page */
    unsigned int sample_pages_count; /* sum of sampled pages */
    unsigned int sample_dirty_count; /* sum of dirty pages we measure */
    uint8_t *hash_result; /* array of hash result for sampled pages */
};

/*
 * Store calculate statistics for each measure.
 */
struct DirtyRateStat {
    unsigned int total_dirty_samples; /* total dirty pages for this measure */
    unsigned int total_sample_count; /* total sampled pages for this measure */
    size_t total_block_mem_MB; /* size of sampled pages in MB */
    int64_t dirty_rate; /* dirty rate for this measure */
};

void *get_dirtyrate_thread(void *arg);
#endif

