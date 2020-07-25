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
static uint64_t dirty_rate; /* MB/s */
CalculatingDirtyRateStage calculating_dirty_rate_stage = CAL_DIRTY_RATE_INIT;

static bool calculate_dirtyrate(struct dirtyrate_config config,
                        uint64_t *dirty_rate, int64_t time)
{
    /* todo */
    return true;
}

static void set_dirty_rate(uint64_t drate)
{
    dirty_rate = drate;
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
    uint64_t dirty_rate;
    uint64_t hash_dirty_rate;
    bool query_succ;
    int64_t msec = 0;
 
    set_dirty_rate_stage(CAL_DIRTY_RATE_ING);

    query_succ = calculate_dirtyrate(config, &hash_dirty_rate, msec);
    if (!query_succ) {
        dirty_rate = 0;
    } else {
        dirty_rate = hash_dirty_rate;
    }

    set_dirty_rate(dirty_rate);
    set_dirty_rate_stage(CAL_DIRTY_RATE_END);

    return NULL;
}
