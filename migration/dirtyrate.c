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
#include "ram.h"
#include "dirtyrate.h"

CalculatingDirtyRateState CalculatingState = CAL_DIRTY_RATE_INIT;
static unsigned long int qcrypto_hash_len = QCRYPTO_HASH_LEN;
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

/*
 * get hash result for the sampled memory with length of 4K byte in ramblock,
 * which starts from ramblock base address.
 */
static int get_ramblock_vfn_hash(struct RamblockDirtyInfo *info,
                                 unsigned long vfn, uint8_t **md)
{
    struct iovec iov_array;
    int ret = 0;
    int nkey = 1;
    size_t hash_len = qcrypto_hash_len;

    iov_array.iov_base = info->ramblock_addr +
                         vfn * DIRTYRATE_SAMPLE_PAGE_SIZE;
    iov_array.iov_len = DIRTYRATE_SAMPLE_PAGE_SIZE;

    if (qcrypto_hash_bytesv(QCRYPTO_HASH_ALG_MD5,
                            &iov_array, nkey,
                            md, &hash_len, NULL) < 0) {
        ret = -1;
    }

    return ret;
}

static int save_ramblock_hash(struct RamblockDirtyInfo *info)
{
    unsigned int sample_pages_count;
    uint8_t *md = NULL;
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
                                        sizeof(uint8_t) * qcrypto_hash_len);
    if (!info->hash_result) {
        ret = -1;
        goto out;
    }

    info->sample_page_vfn = g_try_malloc0_n(sample_pages_count,
                                            sizeof(unsigned long));
    if (!info->sample_page_vfn) {
        g_free(info->hash_result);
        ret = -1;
        goto out;
    }

    for (i = 0; i < sample_pages_count; i++) {
        md = info->hash_result + i * qcrypto_hash_len;
        info->sample_page_vfn[i] = g_rand_int_range(rand, 0,
                                                    info->ramblock_pages - 1);
        ret = get_ramblock_vfn_hash(info, info->sample_page_vfn[i], &md);
        if (ret < 0) {
            goto out;
        }
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
    info->sample_pages_count = (qemu_ram_get_used_length(block)
                                * sample_pages_per_gigabytes) >> 30;

    /* Right shift 12 bits to calc page count in 4KB */
    info->ramblock_pages = qemu_ram_get_used_length(block) >> 12;
    info->ramblock_addr = qemu_ram_get_host_addr(block);
    strcpy(info->idstr, qemu_ram_get_idstr(block));
}

static struct RamblockDirtyInfo *
alloc_ramblock_dirty_info(int *block_index,
                          struct RamblockDirtyInfo *block_dinfo)
{
    struct RamblockDirtyInfo *info = NULL;
    int index = *block_index;

    if (!block_dinfo) {
        block_dinfo = g_try_new(struct RamblockDirtyInfo, 1);
        index = 0;
    } else {
        index++;
        block_dinfo = g_try_realloc(block_dinfo, (index + 1) *
                                    sizeof(struct RamblockDirtyInfo));
    }
    if (!block_dinfo) {
        return NULL;
    }

    info = &block_dinfo[index];
    memset(info, 0, sizeof(struct RamblockDirtyInfo));

    *block_index = index;
    return block_dinfo;
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
    uint8_t *md = NULL;
    int i;
    int ret = 0;

    md = g_try_new0(uint8_t, qcrypto_hash_len);
    if (!md) {
        return -1;
    }

    for (i = 0; i < info->sample_pages_count; i++) {
        ret = get_ramblock_vfn_hash(info, info->sample_page_vfn[i], &md);
        if (ret < 0) {
            goto out;
        }

        if (memcmp(md, info->hash_result + i * qcrypto_hash_len,
                   qcrypto_hash_len) != 0) {
            info->sample_dirty_count++;
        }
    }

out:
    g_free(md);
    return ret;
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
            (qemu_ram_get_used_length(block) >> 12)) {
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
        block_dinfo = NULL;
        if (!find_page_matched(block, info, block_index + 1, &block_dinfo)) {
            continue;
        }
        if (calc_page_dirty_rate(block_dinfo) < 0) {
            return -1;
        }
        update_dirtyrate_stat(block_dinfo);
    }
    if (!dirty_stat.total_sample_count) {
        return -1;
    }

    return 0;
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
