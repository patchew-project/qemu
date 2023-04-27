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

#include "sysemu/dirtyrate.h"

/*
 * Sample 512 pages per GB as default.
 */
#define DIRTYRATE_DEFAULT_SAMPLE_PAGES            512

/*
 * Record ramblock idstr
 */
#define RAMBLOCK_INFO_MAX_LEN                     256

/*
 * Minimum RAMBlock size to sample, in megabytes.
 */
#define MIN_RAMBLOCK_SIZE                         128

/*
 * Take 1s as minimum time for calculation duration
 */
#define MIN_FETCH_DIRTYRATE_TIME_SEC              1
#define MAX_FETCH_DIRTYRATE_TIME_SEC              60

/*
 * Take 1/16 pages in 1G as the maxmum sample page count
 */
#define MIN_SAMPLE_PAGE_COUNT                     128
#define MAX_SAMPLE_PAGE_COUNT                     16384

/*
 * Initial sampling period expressed in milliseconds
 */
#define INITIAL_PERIOD_MS 125

/*
 * Upper bound on the number of DirtyReadings calculcated based on
 * INITIAL_PERIOD_MS, MAX_FETCH_DIRTYRATE_TIME_SEC and increase_period()
 */
#define MAX_DIRTY_READINGS 32


struct DirtyRateConfig {
    uint64_t sample_pages_per_gigabytes; /* sample pages per GB */
    int64_t sample_period_seconds; /* time duration between two sampling */
    DirtyRateMeasureMode mode; /* mode of dirtyrate measurement */
};

/*
 * Store dirtypage info for each ramblock.
 */
struct RamblockDirtyInfo {
    char idstr[RAMBLOCK_INFO_MAX_LEN]; /* idstr for each ramblock */
    uint8_t *ramblock_addr; /* base address of ramblock we measure */
    uint64_t ramblock_pages; /* ramblock size in TARGET_PAGE_SIZE */
    uint64_t *sample_page_vfn; /* relative offset address for sampled page */
    uint64_t sample_pages_count; /* count of sampled pages */
    uint32_t *hash_result; /* array of hash result for sampled pages */
};

typedef struct DirtyReading {
    int64_t period; /* time period in milliseconds */
    int64_t n_dirty_pages; /* number of observed dirty pages */
} DirtyReading;

typedef struct SampleVMStat {
    int64_t n_total_pages; /* total number of pages */
    int64_t n_sampled_pages; /* number of sampled pages */
    int64_t n_readings;
    DirtyReading *readings;
} SampleVMStat;

/*
 * Store calculation statistics for each measure.
 */
struct DirtyRateStat {
    int64_t dirty_rate; /* dirty rate in MB/s */
    int64_t start_time; /* calculation start time in units of second */
    int64_t calc_time; /* time duration of two sampling in units of second */
    uint64_t sample_pages; /* sample pages per GB */
    union {
        SampleVMStat page_sampling;
        VcpuStat dirty_ring;
    };
};

void *get_dirtyrate_thread(void *arg);
#endif
