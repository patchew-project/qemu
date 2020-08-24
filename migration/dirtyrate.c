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

#include <zlib.h>
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
#include "ram.h"
#include "dirtyrate.h"

static int CalculatingState = DIRTY_RATE_STATUS_UNSTARTED;
static struct DirtyRateStat DirtyStat;

static int64_t set_sample_page_period(int64_t msec, int64_t initial_time)
{
    int64_t current_time;

    current_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    if ((current_time - initial_time) >= msec) {
        msec = current_time - initial_time;
    } else {
        g_usleep((msec + initial_time - current_time) * 1000);
    }

    return msec;
}

static int64_t get_sample_page_period(int64_t sec)
{
    if (sec <= MIN_FETCH_DIRTYRATE_TIME_SEC ||
        sec > MAX_FETCH_DIRTYRATE_TIME_SEC) {
        sec = DEFAULT_FETCH_DIRTYRATE_TIME_SEC;
    }

    return sec;
}

static int dirtyrate_set_state(int *state, int old_state, int new_state)
{
    assert(new_state < DIRTY_RATE_STATUS__MAX);
    if (atomic_cmpxchg(state, old_state, new_state) == old_state) {
        return 0;
    } else {
        return -1;
    }
}

static void reset_dirtyrate_stat(void)
{
    DirtyStat.total_dirty_samples = 0;
    DirtyStat.total_sample_count = 0;
    DirtyStat.total_block_mem_MB = 0;
    DirtyStat.dirty_rate = 0;
}

static void update_dirtyrate_stat(struct RamblockDirtyInfo *info)
{
    DirtyStat.total_dirty_samples += info->sample_dirty_count;
    DirtyStat.total_sample_count += info->sample_pages_count;
    /* size of 4K pages in MB */
    DirtyStat.total_block_mem_MB += info->ramblock_pages / 256;
}

static void update_dirtyrate(uint64_t msec)
{
    uint64_t dirtyrate;
    uint64_t total_dirty_samples = DirtyStat.total_dirty_samples;
    uint64_t total_sample_count = DirtyStat.total_sample_count;
    uint64_t total_block_mem_MB = DirtyStat.total_block_mem_MB;

    dirtyrate = total_dirty_samples * total_block_mem_MB *
                 1000 / (total_sample_count * msec);

    DirtyStat.dirty_rate = dirtyrate;
}

/*
 * get hash result for the sampled memory with length of 4K byte in ramblock,
 * which starts from ramblock base address.
 */
static uint32_t get_ramblock_vfn_hash(struct RamblockDirtyInfo *info,
                                      uint64_t vfn)
{
    struct iovec iov_array;
    uint32_t crc;

    iov_array.iov_base = info->ramblock_addr +
                         vfn * DIRTYRATE_SAMPLE_PAGE_SIZE;
    iov_array.iov_len = DIRTYRATE_SAMPLE_PAGE_SIZE;

    crc = crc32(0, iov_array.iov_base, iov_array.iov_len);

    return crc;
}

static int save_ramblock_hash(struct RamblockDirtyInfo *info)
{
    unsigned int sample_pages_count;
    int i;
    int ret = -1;
    GRand *rand = g_rand_new();

    sample_pages_count = info->sample_pages_count;

    /* ramblock size less than one page, return success to skip this ramblock */
    if (unlikely(info->ramblock_pages == 0 || sample_pages_count == 0)) {
        ret = 0;
        goto out;
    }

    info->hash_result = g_try_malloc0_n(sample_pages_count,
                                        sizeof(uint32_t));
    if (!info->hash_result) {
        ret = -1;
        goto out;
    }

    info->sample_page_vfn = g_try_malloc0_n(sample_pages_count,
                                            sizeof(uint64_t));
    if (!info->sample_page_vfn) {
        g_free(info->hash_result);
        ret = -1;
        goto out;
    }

    for (i = 0; i < sample_pages_count; i++) {
        info->sample_page_vfn[i] = g_rand_int_range(rand, 0,
                                                    info->ramblock_pages - 1);
        info->hash_result[i] = get_ramblock_vfn_hash(info,
                                                     info->sample_page_vfn[i]);
    }
    ret = 0;

out:
    g_rand_free(rand);
    return ret;
}

static void get_ramblock_dirty_info(RAMBlock *block,
                                    struct RamblockDirtyInfo *info,
                                    struct DirtyRateConfig *config)
{
    uint64_t sample_pages_per_gigabytes = config->sample_pages_per_gigabytes;

    /* Right shift 30 bits to calc block size in GB */
    info->sample_pages_count = (qemu_ram_get_used_length(block) *
                                sample_pages_per_gigabytes) >>
                                DIRTYRATE_PAGE_SHIFT_GB;

    /* Right shift 12 bits to calc page count in 4KB */
    info->ramblock_pages = qemu_ram_get_used_length(block) >>
                           DIRTYRATE_PAGE_SHIFT_KB;
    info->ramblock_addr = qemu_ram_get_host_addr(block);
    strcpy(info->idstr, qemu_ram_get_idstr(block));
}

static void free_ramblock_dirty_info(struct RamblockDirtyInfo *infos, int count)
{
    int i;

    if (!infos) {
        return;
    }

    for (i = 0; i < count; i++) {
        g_free(infos[i].sample_page_vfn);
        g_free(infos[i].hash_result);
    }
    g_free(infos);
}

static struct RamblockDirtyInfo *
alloc_ramblock_dirty_info(int *block_index,
                          struct RamblockDirtyInfo *block_dinfo)
{
    struct RamblockDirtyInfo *info = NULL;
    int index = *block_index;

    if (!block_dinfo) {
        index = 0;
        block_dinfo = g_try_new(struct RamblockDirtyInfo, 1);
    } else {
        index++;
        block_dinfo = g_try_realloc(block_dinfo, (index + 1) *
                                    sizeof(struct RamblockDirtyInfo));
    }
    if (!block_dinfo) {
        return NULL;
    }

    info = &block_dinfo[index];
    *block_index = index;
    memset(info, 0, sizeof(struct RamblockDirtyInfo));

    return block_dinfo;
}

static int skip_sample_ramblock(RAMBlock *block)
{
    int64_t ramblock_size;

    /* ramblock size in MB */
    ramblock_size = qemu_ram_get_used_length(block) >> DIRTYRATE_PAGE_SHIFT_MB;

    /*
     * Consider ramblock with size larger than 128M is what we
     * want to sample.
     */
    if (ramblock_size < MIN_RAMBLOCK_SIZE) {
        return -1;
    }

    return 0;
}

static int record_ramblock_hash_info(struct RamblockDirtyInfo **block_dinfo,
                                     struct DirtyRateConfig config,
                                     int *block_index)
{
    struct RamblockDirtyInfo *info = NULL;
    struct RamblockDirtyInfo *dinfo = NULL;
    RAMBlock *block = NULL;
    int index = 0;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        if (skip_sample_ramblock(block) < 0) {
            continue;
        }
        dinfo = alloc_ramblock_dirty_info(&index, dinfo);
        if (dinfo == NULL) {
            return -1;
        }
        info = &dinfo[index];
        get_ramblock_dirty_info(block, info, &config);
        if (save_ramblock_hash(info) < 0) {
            *block_dinfo = dinfo;
            *block_index = index;
            return -1;
        }
    }

    *block_dinfo = dinfo;
    *block_index = index;

    return 0;
}

static int calc_page_dirty_rate(struct RamblockDirtyInfo *info)
{
    uint32_t crc;
    int i;

    for (i = 0; i < info->sample_pages_count; i++) {
        crc = get_ramblock_vfn_hash(info, info->sample_page_vfn[i]);
        if (crc != info->hash_result[i]) {
            info->sample_dirty_count++;
        }
    }

    return 0;
}

static bool find_page_matched(RAMBlock *block, struct RamblockDirtyInfo *infos,
                              int count, struct RamblockDirtyInfo **matched)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!strcmp(infos[i].idstr, qemu_ram_get_idstr(block))) {
            break;
        }
    }

    if (i == count) {
        return false;
    }

    if (infos[i].ramblock_addr != qemu_ram_get_host_addr(block) ||
        infos[i].ramblock_pages !=
            (qemu_ram_get_used_length(block) >> DIRTYRATE_PAGE_SHIFT_KB)) {
        return false;
    }

    *matched = &infos[i];
    return true;
}

static int compare_page_hash_info(struct RamblockDirtyInfo *info,
                                  int block_index)
{
    struct RamblockDirtyInfo *block_dinfo = NULL;
    RAMBlock *block = NULL;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        if (skip_sample_ramblock(block) < 0) {
            continue;
        }
        block_dinfo = NULL;
        if (!find_page_matched(block, info, block_index + 1, &block_dinfo)) {
            continue;
        }
        if (calc_page_dirty_rate(block_dinfo) < 0) {
            return -1;
        }
        update_dirtyrate_stat(block_dinfo);
    }

    if (!DirtyStat.total_sample_count) {
        return -1;
    }

    return 0;
}

static void calculate_dirtyrate(struct DirtyRateConfig config)
{
    struct RamblockDirtyInfo *block_dinfo = NULL;
    int block_index = 0;
    int64_t msec = 0;
    int64_t initial_time;

    rcu_register_thread();
    reset_dirtyrate_stat();
    initial_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    rcu_read_lock();
    if (record_ramblock_hash_info(&block_dinfo, config, &block_index) < 0) {
        goto out;
    }
    rcu_read_unlock();

    msec = config.sample_period_seconds * 1000;
    msec = set_sample_page_period(msec, initial_time);

    rcu_read_lock();
    if (compare_page_hash_info(block_dinfo, block_index) < 0) {
        goto out;
    }

    update_dirtyrate(msec);

out:
    rcu_read_unlock();
    free_ramblock_dirty_info(block_dinfo, block_index + 1);
    rcu_unregister_thread();
}

void *get_dirtyrate_thread(void *arg)
{
    struct DirtyRateConfig config = *(struct DirtyRateConfig *)arg;
    int ret;

    ret = dirtyrate_set_state(&CalculatingState, DIRTY_RATE_STATUS_UNSTARTED,
                              DIRTY_RATE_STATUS_MEASURING);
    if (ret == -1) {
        return NULL;
    }

    calculate_dirtyrate(config);

    ret = dirtyrate_set_state(&CalculatingState, DIRTY_RATE_STATUS_MEASURING,
                              DIRTY_RATE_STATUS_MEASURED);
    return NULL;
}
