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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/hash.h"
#include "crypto/random.h"
#include "qemu/config-file.h"
#include "exec/memory.h"
#include "exec/ramblock.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "qapi/qapi-commands-migration.h"
#include "migration.h"
#include "dirtyrate.h"

CalculatingDirtyRateState CalculatingState = CAL_DIRTY_RATE_INIT;
static struct DirtyRateStat dirty_stat;

static int dirty_rate_set_state(int new_state)
{
    int old_state = CalculatingState;

    if (new_state == old_state) {
        return -1;
    }

    if (atomic_cmpxchg(&CalculatingState, old_state, new_state) != old_state) {
        return -1;
    }

    return 0;
}

static void reset_dirtyrate_stat(void)
{
    dirty_stat.total_dirty_samples = 0;
    dirty_stat.total_sample_count = 0;
    dirty_stat.total_block_mem_MB = 0;
    dirty_stat.dirty_rate = 0;
}

static void update_dirtyrate_stat(struct RamblockDirtyInfo *info)
{
    dirty_stat.total_dirty_samples += info->sample_dirty_count;
    dirty_stat.total_sample_count += info->sample_pages_count;
    /* size of 4K pages in MB */
    dirty_stat.total_block_mem_MB += info->ramblock_pages / 256;
}

static void update_dirtyrate(uint64_t msec)
{
    uint64_t dirty_rate;
    unsigned int total_dirty_samples = dirty_stat.total_dirty_samples;
    unsigned int total_sample_count = dirty_stat.total_sample_count;
    size_t total_block_mem_MB = dirty_stat.total_block_mem_MB;

    dirty_rate = total_dirty_samples * total_block_mem_MB *
                 1000 / (total_sample_count * msec);

    dirty_stat.dirty_rate = dirty_rate;
}

static void calculate_dirtyrate(struct DirtyRateConfig config)
{
    /* todo */
    return;
}

void *get_dirtyrate_thread(void *arg)
{
    struct DirtyRateConfig config = *(struct DirtyRateConfig *)arg;
    int ret;

    ret = dirty_rate_set_state(CAL_DIRTY_RATE_ACTIVE);
    if (ret == -1) {
        return NULL;
    }

    calculate_dirtyrate(config);

    ret = dirty_rate_set_state(CAL_DIRTY_RATE_END);

    return NULL;
}
