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

static uint64_t sample_pages_per_gigabytes = DIRTYRATE_DEFAULT_SAMPLE_PAGES;
static struct dirtyrate_statistics dirty_stat;
CalculatingDirtyRateStage calculating_dirty_rate_stage = CAL_DIRTY_RATE_INIT;

#define RAMBLOCK_FOREACH_MIGRATABLE(block)             \
        INTERNAL_RAMBLOCK_FOREACH(block)                   \
        if (!qemu_ram_is_migratable(block)) {} else

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

static int get_block_vfn_hash(struct block_dirty_info *info, unsigned long vfn,
                              uint8_t **md, size_t *hash_len)
{
    struct iovec iov_array;
    int ret = 0;
    int nkey = 1;

    iov_array.iov_base = info->block_addr +
                         vfn * DIRTYRATE_SAMPLE_PAGE_SIZE;
    iov_array.iov_len = DIRTYRATE_SAMPLE_PAGE_SIZE;

    if (qcrypto_hash_bytesv(QCRYPTO_HASH_ALG_MD5,
                            &iov_array, nkey,
                            md, hash_len, NULL) < 0) {
        ret = -1;
    }

    return ret;
}

static int save_block_hash(struct block_dirty_info *info)
{
    unsigned long *rand_buf = NULL;
    unsigned int sample_pages_count;
    uint8_t *md = NULL;
    size_t hash_len;
    int i;
    int ret = -1;

    sample_pages_count = info->sample_pages_count;
    /* block size less than one page, return success to skip this block */
    if (unlikely(info->block_pages == 0 || sample_pages_count == 0)) {
        ret = 0;
        goto out;
    }

    /* use random bytes to pick sample page vfn */
    rand_buf = g_malloc0_n(sample_pages_count, sizeof(unsigned long));
    /* DEFAULT_READ_RANDOM_MAX_LIMIT 32M,
     * can support 4T vm 1024 sample_pages_per_gigabytes
     */
    ret = qcrypto_random_bytes((unsigned char *)rand_buf,
                               sample_pages_count * sizeof(unsigned long),
                               NULL);
    if (ret) {
        ret = -1;
        goto out;
    }

    hash_len = qcrypto_hash_digest_len(QCRYPTO_HASH_ALG_MD5);
    info->hash_result = g_malloc0_n(sample_pages_count, sizeof(uint8_t) * hash_len);
    info->sample_page_vfn = g_malloc0_n(sample_pages_count, sizeof(unsigned long));

    for (i = 0; i < sample_pages_count; i++) {
        md = info->hash_result + i * hash_len;
        info->sample_page_vfn[i] = rand_buf[i] % info->block_pages;
        ret = get_block_vfn_hash(info, info->sample_page_vfn[i], &md, &hash_len);
        if (ret < 0) {
            goto out;
        }
    }
    ret = 0;
out:
    g_free(rand_buf);
    return ret;
}

static void get_block_dirty_info(RAMBlock *block, struct block_dirty_info *info,
                                 struct dirtyrate_config *config)
{
    uint64_t sample_pages_per_gigabytes = config->sample_pages_per_gigabytes;

    /* Right shift 30 bits to calc block size in GB */
    info->sample_pages_count = (qemu_ram_get_used_length(block) * sample_pages_per_gigabytes) >> 30;

    info->block_pages = qemu_ram_get_used_length(block) >> DIRTYRATE_PAGE_SIZE_SHIFT;
    info->block_addr = qemu_ram_get_host_addr(block);
    strcpy(info->idstr, qemu_ram_get_idstr(block));
}

static struct block_dirty_info *
alloc_block_dirty_info(int *block_index,
                       struct block_dirty_info *block_dinfo)
{
    struct block_dirty_info *info = NULL;
    int index = *block_index;

    if (!block_dinfo) {
        block_dinfo = g_new(struct block_dirty_info, 1);
        index = 0;
    } else {
        block_dinfo = g_realloc(block_dinfo, (index + 1) *
                                sizeof(struct block_dirty_info));
        index++;
    }
    info = &block_dinfo[index];
    memset(info, 0, sizeof(struct block_dirty_info));

    *block_index = index;
    return block_dinfo;
}

static int ram_block_skip(RAMBlock *block)
{
    if (!strstr(qemu_ram_get_idstr(block), "ram-node") &&
        !strstr(qemu_ram_get_idstr(block), "memdimm")) {
        if (strcmp(qemu_ram_get_idstr(block), "mach-virt.ram") ||
            strcmp(block->idstr, "pc.ram")) {
            return -1;
        }
    }

    return 0;
}

static int record_block_hash_info(struct dirtyrate_config config,
                                  struct block_dirty_info **block_dinfo, int *block_index)
{
    struct block_dirty_info *info = NULL;
    struct block_dirty_info *dinfo = NULL;
    RAMBlock *block = NULL;
    int index = 0;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        if (ram_block_skip(block) < 0) {
            continue;
        }
        dinfo = alloc_block_dirty_info(&index, dinfo);
        info = &dinfo[index];
        get_block_dirty_info(block, info, &config);
        if (save_block_hash(info) < 0) {
            *block_dinfo = dinfo;
            *block_index = index;
            return -1;
        }
    }

    *block_dinfo = dinfo;
    *block_index = index;

    return 0;
}

static int cal_block_dirty_rate(struct block_dirty_info *info)
{
    uint8_t *md = NULL;
    size_t hash_len;
    int i;
    int ret = 0;

    hash_len = qcrypto_hash_digest_len(QCRYPTO_HASH_ALG_MD5);
    md = g_new0(uint8_t, hash_len);

    for (i = 0; i < info->sample_pages_count; i++) {
        ret = get_block_vfn_hash(info, info->sample_page_vfn[i], &md, &hash_len);
        if (ret < 0) {
            goto out;
        }

        if (memcmp(md, info->hash_result + i * hash_len, hash_len) != 0) {
            info->sample_dirty_count++;
        }
    }

out:
    g_free(md);
    return ret;
}

static bool find_block_matched(RAMBlock *block, struct block_dirty_info *infos,
                               int count, struct block_dirty_info **matched)
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

    if (infos[i].block_addr != qemu_ram_get_host_addr(block) ||
        infos[i].block_pages !=
            (qemu_ram_get_used_length(block) >> DIRTYRATE_PAGE_SIZE_SHIFT)) {
        return false;
    }

    *matched = &infos[i];
    return true;
}

static int compare_block_hash_info(struct block_dirty_info *info, int block_index)
{
    struct block_dirty_info *block_dinfo = NULL;
    RAMBlock *block = NULL;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        if (ram_block_skip(block) < 0) {
            continue;
        }
        block_dinfo = NULL;
        if (!find_block_matched(block, info, block_index + 1, &block_dinfo)) {
            continue;
        }
        if (cal_block_dirty_rate(block_dinfo) < 0) {
            return -1;
        }
        update_dirtyrate_stat(block_dinfo);
    }
    if (!dirty_stat.total_sample_count) {
        return -1;
    }

    return 0;
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
