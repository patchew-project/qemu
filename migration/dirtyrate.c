/*
 * Dirtyrate implement code
 *
 * Copyright (c) 2020 HUAWEI TECHNOLOGIES CO.,LTD.
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
#include "cpu.h"
#include "qemu/config-file.h"
#include "exec/memory.h"
#include "exec/ramblock.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "qapi/qapi-commands-migration.h"
#include "migration.h"
#include "ram.h"
#include "trace.h"
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

static bool get_sample_page_period(int64_t sec)
{
    if (sec < MIN_FETCH_DIRTYRATE_TIME_SEC ||
        sec > MAX_FETCH_DIRTYRATE_TIME_SEC) {
        return false;
    }

    return true;
}

static int dirtyrate_set_state(int *state, int old_state, int new_state)
{
    assert(new_state < DIRTY_RATE_STATUS__MAX);
    trace_dirtyrate_set_state(DirtyRateStatus_str(new_state));
    if (atomic_cmpxchg(state, old_state, new_state) == old_state) {
        return 0;
    } else {
        return -1;
    }
}

static struct DirtyRateInfo *query_dirty_rate_info(void)
{
    int64_t dirty_rate = DirtyStat.dirty_rate;
    struct DirtyRateInfo *info = g_malloc0(sizeof(DirtyRateInfo));

    if (CalculatingState == DIRTY_RATE_STATUS_MEASURED) {
        info->dirty_rate = dirty_rate;
    } else {
        info->dirty_rate = -1;
    }

    info->status = CalculatingState;
    info->start_time = DirtyStat.start_time;
    info->calc_time = DirtyStat.calc_time;

    trace_query_dirty_rate_info(DirtyRateStatus_str(CalculatingState));

    return info;
}

static void reset_dirtyrate_stat(void)
{
    DirtyStat.total_dirty_samples = 0;
    DirtyStat.total_sample_count = 0;
    DirtyStat.total_block_mem_MB = 0;
    DirtyStat.dirty_rate = 0;
    DirtyStat.start_time = 0;
    DirtyStat.calc_time = 0;
}

static void update_dirtyrate_stat(struct RamblockDirtyInfo *info)
{
    DirtyStat.total_dirty_samples += info->sample_dirty_count;
    DirtyStat.total_sample_count += info->sample_pages_count;
    /* size of total pages in MB */
    DirtyStat.total_block_mem_MB += (info->ramblock_pages *
                                     TARGET_PAGE_SIZE) >> 20;
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
 * get hash result for the sampled memory with length of TARGET_PAGE_SIZE
 * in ramblock, which starts from ramblock base address.
 */
static uint32_t get_ramblock_vfn_hash(struct RamblockDirtyInfo *info,
                                      uint64_t vfn)
{
    uint32_t crc;

    crc = crc32(0, (info->ramblock_addr +
                vfn * TARGET_PAGE_SIZE), TARGET_PAGE_SIZE);

    trace_get_ramblock_vfn_hash(info->idstr, vfn, crc);
    return crc;
}

static int save_ramblock_hash(struct RamblockDirtyInfo *info)
{
    unsigned int sample_pages_count;
    int i;
    GRand *rand;

    sample_pages_count = info->sample_pages_count;

    /* ramblock size less than one page, return success to skip this ramblock */
    if (unlikely(info->ramblock_pages == 0 || sample_pages_count == 0)) {
        return 0;
    }

    info->hash_result = g_try_malloc0_n(sample_pages_count,
                                        sizeof(uint32_t));
    if (!info->hash_result) {
        return -1;
    }

    info->sample_page_vfn = g_try_malloc0_n(sample_pages_count,
                                            sizeof(uint64_t));
    if (!info->sample_page_vfn) {
        g_free(info->hash_result);
        return -1;
    }

    rand  = g_rand_new();
    for (i = 0; i < sample_pages_count; i++) {
        info->sample_page_vfn[i] = g_rand_int_range(rand, 0,
                                                    info->ramblock_pages - 1);
        info->hash_result[i] = get_ramblock_vfn_hash(info,
                                                     info->sample_page_vfn[i]);
    }
    g_rand_free(rand);

    return 0;
}

static void get_ramblock_dirty_info(RAMBlock *block,
                                    struct RamblockDirtyInfo *info,
                                    struct DirtyRateConfig *config)
{
    uint64_t sample_pages_per_gigabytes = config->sample_pages_per_gigabytes;

    /* Right shift 30 bits to calc ramblock size in GB */
    info->sample_pages_count = (qemu_ram_get_used_length(block) *
                                sample_pages_per_gigabytes) >> 30;
    /* Right shift TARGET_PAGE_BITS to calc page count */
    info->ramblock_pages = qemu_ram_get_used_length(block) >>
                           TARGET_PAGE_BITS;
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

static bool skip_sample_ramblock(RAMBlock *block)
{
    /*
     * Sample only blocks larger than MIN_RAMBLOCK_SIZE.
     */
    if (qemu_ram_get_used_length(block) < (MIN_RAMBLOCK_SIZE << 10)) {
        trace_skip_sample_ramblock(block->idstr,
                                   qemu_ram_get_used_length(block));
        return true;
    }

    return false;
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
        if (skip_sample_ramblock(block)) {
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

static void calc_page_dirty_rate(struct RamblockDirtyInfo *info)
{
    uint32_t crc;
    int i;

    for (i = 0; i < info->sample_pages_count; i++) {
        crc = get_ramblock_vfn_hash(info, info->sample_page_vfn[i]);
        if (crc != info->hash_result[i]) {
            trace_calc_page_dirty_rate(info->idstr, crc, info->hash_result[i]);
            info->sample_dirty_count++;
        }
    }
}

static struct RamblockDirtyInfo *
find_page_matched(RAMBlock *block, int count,
                  struct RamblockDirtyInfo *infos)
{
    int i;
    struct RamblockDirtyInfo *matched;

    for (i = 0; i < count; i++) {
        if (!strcmp(infos[i].idstr, qemu_ram_get_idstr(block))) {
            break;
        }
    }

    if (i == count) {
        return NULL;
    }

    if (infos[i].ramblock_addr != qemu_ram_get_host_addr(block) ||
        infos[i].ramblock_pages !=
            (qemu_ram_get_used_length(block) >> TARGET_PAGE_BITS)) {
        trace_find_page_matched(block->idstr);
        return NULL;
    }

    matched = &infos[i];

    return matched;
}

static int compare_page_hash_info(struct RamblockDirtyInfo *info,
                                  int block_index)
{
    struct RamblockDirtyInfo *block_dinfo = NULL;
    RAMBlock *block = NULL;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        if (skip_sample_ramblock(block)) {
            continue;
        }
        block_dinfo = find_page_matched(block, block_index + 1, info);
        if (block_dinfo == NULL) {
            continue;
        }
        calc_page_dirty_rate(block_dinfo);
        update_dirtyrate_stat(block_dinfo);
    }

    if (DirtyStat.total_sample_count == 0) {
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
    rcu_read_lock();
    initial_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    if (record_ramblock_hash_info(&block_dinfo, config, &block_index) < 0) {
        goto out;
    }
    rcu_read_unlock();

    msec = config.sample_period_seconds * 1000;
    msec = set_sample_page_period(msec, initial_time);
    DirtyStat.start_time = initial_time / 1000;
    DirtyStat.calc_time = msec / 1000;

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

void qmp_calc_dirty_rate(int64_t calc_time, Error **errp)
{
    static struct DirtyRateConfig config;
    QemuThread thread;
    int ret;

    /*
     * If the dirty rate is already being measured, don't attempt to start.
     */
    if (CalculatingState == DIRTY_RATE_STATUS_MEASURING) {
        error_setg(errp, "the dirty rate is already being measured.");
        return;
    }

    if (!get_sample_page_period(calc_time)) {
        error_setg(errp, "calc-time is out of range[%d, %d].",
                         MIN_FETCH_DIRTYRATE_TIME_SEC,
                         MAX_FETCH_DIRTYRATE_TIME_SEC);
        return;
    }

    /*
     * Init calculation state as unstarted.
     */
    ret = dirtyrate_set_state(&CalculatingState, CalculatingState,
                              DIRTY_RATE_STATUS_UNSTARTED);
    if (ret == -1) {
        error_setg(errp, "init dirty rate calculation state failed.");
        return;
    }

    config.sample_period_seconds = calc_time;
    config.sample_pages_per_gigabytes = DIRTYRATE_DEFAULT_SAMPLE_PAGES;
    qemu_thread_create(&thread, "get_dirtyrate", get_dirtyrate_thread,
                       (void *)&config, QEMU_THREAD_DETACHED);
}

struct DirtyRateInfo *qmp_query_dirty_rate(Error **errp)
{
    return query_dirty_rate_info();
}
