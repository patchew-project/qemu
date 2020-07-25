/*
 * Dirtyrate implement code
 *
 * Copyright (c) 2017-2020 HUAWEI TECHNOLOGIES CO.,LTD.
 *
 * Authors:
 *  Chuan Zheng <zhengchuan@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "dirtyrate.h"

static uint64_t sample_pages_per_gigabytes = DIRTYRATE_DEFAULT_SAMPLE_PAGES;
static struct dirtyrate_statistics dirty_stat;
CalculatingDirtyRateStage calculating_dirty_rate_stage = CAL_DIRTY_RATE_INIT;

static void reset_dirtyrate_stat(void)
{
    dirty_stat.total_dirty_samples = 0;
    dirty_stat.total_sample_count = 0;
    dirty_stat.total_block_mem_MB = 0;
    dirty_stat.dirty_rate = 0;
}

static void update_dirtyrate_stat(struct block_dirty_info *info)
{
    dirty_stat.total_dirty_samples += info->sample_dirty_count;
    dirty_stat.total_sample_count += info->sample_pages_count;
    dirty_stat.total_block_mem_MB += (info->block_pages << DIRTYRATE_PAGE_SIZE_SHIFT) >> PAGE_SIZE_SHIFT;
}

static void update_dirtyrate(int64_t msec)
{
    uint64_t dirty_rate;
    unsigned int total_dirty_samples = dirty_stat.total_dirty_samples;
    unsigned int total_sample_count = dirty_stat.total_sample_count;
    unsigned long total_block_mem_MB = dirty_stat.total_block_mem_MB;

    dirty_rate = total_dirty_samples * total_block_mem_MB *
                 1000 / (total_sample_count * msec);

    dirty_stat.dirty_rate = dirty_rate;
}


static void calculate_dirtyrate(struct dirtyrate_config config, int64_t time)
{
    /* todo */
}

/*
 * There are multithread will write/read *calculating_dirty_rate_stage*,
 * we can protect only one thread write/read it by libvirt api.
 * So we don't add mutex_lock to protect it here, but we must calculate
 * dirty_rate by libvirt api.
 */
static void set_dirty_rate_stage(CalculatingDirtyRateStage ratestage)
{
    calculating_dirty_rate_stage = ratestage;
}

void *get_dirtyrate_thread(void *arg)
{
    struct dirtyrate_config config = *(struct dirtyrate_config *)arg;
    int64_t msec = 0;
 
    set_dirty_rate_stage(CAL_DIRTY_RATE_ING);

    calculate_dirtyrate(config, msec);

    set_dirty_rate_stage(CAL_DIRTY_RATE_END);

    return NULL;
}
