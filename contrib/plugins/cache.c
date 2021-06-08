/*
 * Copyright (C) 2021, Mahmoud Mandour <ma.mandourr@gmail.com>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

static GRand *rng;
static GHashTable *miss_ht;

static GMutex mtx;

static int limit;
static bool sys;

static uint64_t dmem_accesses;
static uint64_t dmisses;

static uint64_t imem_accesses;
static uint64_t imisses;

enum EvictionPolicy {
    LRU,
    FIFO,
    RAND,
};

enum EvictionPolicy policy;

/*
 * A CacheSet is a set of cache blocks. A memory block that maps to a set can be
 * put in any of the blocks inside the set. The number of block per set is
 * called the associativity (assoc).
 *
 * Each block contains the the stored tag and a valid bit. Since this is not
 * a functional simulator, the data itself is not stored. We only identify
 * whether a block is in the cache or not by searching for its tag.
 *
 * In order to search for memory data in the cache, the set identifier and tag
 * are extracted from the address and the set is probed to see whether a tag
 * match occur.
 *
 * An address is logically divided into three portions: The block offset,
 * the set number, and the tag.
 *
 * The set number is used to identify the set in which the block may exist.
 * The tag is compared against all the tags of a set to search for a match. If a
 * match is found, then the access is a hit.
 *
 * The CacheSet also contains bookkeaping information about eviction details.
 */

struct CacheBlock {
    uint64_t tag;
    bool valid;
};

struct CacheSet {
    struct CacheBlock *blocks;
    uint64_t *lru_priorities;
    uint64_t lru_gen_counter;
    GQueue *fifo_queue;
};

struct Cache {
    struct CacheSet *sets;
    int num_sets;
    int cachesize;
    int assoc;
    int blksize_shift;
    uint64_t set_mask;
    uint64_t tag_mask;
};

struct InsnData {
    char *disas_str;
    const char *symbol;
    uint64_t addr;
    uint64_t dmisses;
    uint64_t imisses;
};

void (*update_hit)(struct Cache *cache, int set, int blk);
void (*update_miss)(struct Cache *cache, int set, int blk);

void (*metadata_init)(struct Cache *cache);
void (*metadata_destroy)(struct Cache *cache);

struct Cache *dcache, *icache;

static int pow_of_two(int num)
{
    g_assert((num & (num - 1)) == 0);
    int ret = 0;
    while (num /= 2) {
        ret++;
    }
    return ret;
}

static inline uint64_t extract_tag(struct Cache *cache, uint64_t addr)
{
    return addr & cache->tag_mask;
}

static inline uint64_t extract_set(struct Cache *cache, uint64_t addr)
{
    return (addr & cache->set_mask) >> cache->blksize_shift;
}

/*
 * LRU evection policy: For each set, a generation counter is maintained
 * alongside a priority array.
 *
 * On each set access, the generation counter is incremented.
 *
 * On a cache hit: The hit-block is assigned the current generation counter,
 * indicating that it is the most recently used block.
 *
 * On a cache miss: The block with the least priority is searched and replaced
 * with the newly-cached block, of which the priority is set to the current
 * generation number.
 */

static void lru_priorities_init(struct Cache *cache)
{
    int i, assoc;

    assoc = cache->assoc;
    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].lru_priorities = g_new0(uint64_t, assoc);
    }
}

static void lru_update_blk(struct Cache *cache, int set_idx, int blk_idx)
{
    struct CacheSet *set = &cache->sets[set_idx];
    set->lru_priorities[blk_idx] = cache->sets[set_idx].lru_gen_counter;
    set->lru_gen_counter++;
}

static int lru_get_lru_block(struct Cache *cache, int set_idx)
{
    int i, min_idx, min_priority;

    min_priority = cache->sets[set_idx].lru_priorities[0];
    min_idx = 0;

    for (i = 1; i < cache->assoc; i++) {
        if (cache->sets[set_idx].lru_priorities[i] < min_priority) {
            min_priority = cache->sets[set_idx].lru_priorities[i];
            min_idx = i;
        }
    }
    return min_idx;
}

static void lru_priorities_destroy(struct Cache *cache)
{
    int i;

    for (i = 0; i < cache->num_sets; i++) {
        g_free(cache->sets[i].lru_priorities);
    }
}

/*
 * FIFO eviction policy: a FIFO queue is maintained for each CacheSet that
 * stores accesses to the cache.
 *
 * On a compulsory miss: The block index is enqueued to the fifo_queue to
 * indicate that it's the latest cached block.
 *
 * On a conflict miss: The first-in block is removed from the cache and the new
 * block is put in its place and enqueued to the FIFO queue.
 */

static void fifo_init(struct Cache *cache)
{
    int i;

    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].fifo_queue = g_queue_new();
    }
}

static int fifo_get_first_block(struct Cache *cache, int set)
{
    GQueue *q = cache->sets[set].fifo_queue;
    return GPOINTER_TO_INT(g_queue_pop_tail(q));
}

static void fifo_update_on_miss(struct Cache *cache, int set, int blk_idx)
{
    GQueue *q = cache->sets[set].fifo_queue;
    g_queue_push_head(q, GINT_TO_POINTER(blk_idx));
}

static void fifo_destroy(struct Cache *cache)
{
    int i;

    for (i = 0; i < cache->assoc; i++) {
        g_queue_free(cache->sets[i].fifo_queue);
    }
}

static bool bad_cache_params(int blksize, int assoc, int cachesize)
{
    return (cachesize % blksize) != 0 || (cachesize % (blksize * assoc) != 0);
}

static struct Cache *cache_init(int blksize, int assoc, int cachesize)
{
    if (bad_cache_params(blksize, assoc, cachesize)) {
        return NULL;
    }

    struct Cache *cache;
    int i;
    uint64_t blk_mask;

    cache = g_new(struct Cache, 1);
    cache->assoc = assoc;
    cache->cachesize = cachesize;
    cache->num_sets = cachesize / (blksize * assoc);
    cache->sets = g_new(struct CacheSet, cache->num_sets);
    cache->blksize_shift = pow_of_two(blksize);

    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].blocks = g_new0(struct CacheBlock, assoc);
        cache->sets[i].lru_gen_counter = 0;
    }

    blk_mask = blksize - 1;
    cache->set_mask = ((cache->num_sets - 1) << cache->blksize_shift);
    cache->tag_mask = ~(cache->set_mask | blk_mask);

    if (metadata_init) {
        metadata_init(cache);
    }

    return cache;
}

static int get_invalid_block(struct Cache *cache, uint64_t set)
{
    int i;

    for (i = 0; i < cache->assoc; i++) {
        if (!cache->sets[set].blocks[i].valid) {
            return i;
        }
    }

    return -1;
}

static int get_replaced_block(struct Cache *cache, int set)
{
    switch (policy) {
    case RAND:
        return g_rand_int_range(rng, 0, cache->assoc);
    case LRU:
        return lru_get_lru_block(cache, set);
    case FIFO:
        return fifo_get_first_block(cache, set);
    defalut:
        g_assert_not_reached();
    }
}

static int in_cache(struct Cache *cache, uint64_t addr)
{
    int i;
    uint64_t tag, set;

    tag = extract_tag(cache, addr);
    set = extract_set(cache, addr);

    for (i = 0; i < cache->assoc; i++) {
        if (cache->sets[set].blocks[i].tag == tag &&
                cache->sets[set].blocks[i].valid) {
            return i;
        }
    }

    return -1;
}

/**
 * access_cache(): Simulate a cache access
 * @cache: The cache under simulation
 * @addr: The address of the requested memory location
 *
 * Returns true if the requsted data is hit in the cache and false when missed.
 * The cache is then updated for subsequent accesses.
 */
static bool access_cache(struct Cache *cache, uint64_t addr)
{
    int hit_blk, replaced_blk;
    uint64_t tag, set;

    tag = extract_tag(cache, addr);
    set = extract_set(cache, addr);

    hit_blk = in_cache(cache, addr);
    if (hit_blk != -1) {
        if (update_hit) {
            update_hit(cache, set, hit_blk);
        }
        return true;
    }

    replaced_blk = get_invalid_block(cache, set);

    if (replaced_blk == -1) {
        replaced_blk = get_replaced_block(cache, set);
    }

    if (update_miss) {
        update_miss(cache, set, replaced_blk);
    }

    cache->sets[set].blocks[replaced_blk].tag = tag;
    cache->sets[set].blocks[replaced_blk].valid = true;

    return false;
}

static void vcpu_mem_access(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                            uint64_t vaddr, void *userdata)
{
    uint64_t insn_addr;
    uint64_t effective_addr;
    struct qemu_plugin_hwaddr *hwaddr;
    struct InsnData *insn;

    g_mutex_lock(&mtx);
    hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    if (hwaddr && qemu_plugin_hwaddr_is_io(hwaddr)) {
        g_mutex_unlock(&mtx);
        return;
    }

    insn_addr = ((struct InsnData *) userdata)->addr;
    effective_addr = hwaddr ? qemu_plugin_hwaddr_phys_addr(hwaddr) : vaddr;

    if (!access_cache(dcache, effective_addr)) {
        insn = (struct InsnData *) userdata;
        insn->dmisses++;
        dmisses++;
    }
    dmem_accesses++;
    g_mutex_unlock(&mtx);
}

static void vcpu_insn_exec(unsigned int vcpu_index, void *userdata)
{
    uint64_t insn_addr;
    struct InsnData *insn;

    g_mutex_lock(&mtx);
    insn_addr = ((struct InsnData *) userdata)->addr;

    if (!access_cache(icache, insn_addr)) {
        insn = (struct InsnData *) userdata;
        insn->imisses++;
        imisses++;
    }
    imem_accesses++;
    g_mutex_unlock(&mtx);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns;
    size_t i;
    struct InsnData *data;

    n_insns = qemu_plugin_tb_n_insns(tb);
    for (i = 0; i < n_insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t effective_addr;

        if (sys) {
            effective_addr = (uint64_t) qemu_plugin_insn_haddr(insn);
        } else {
            effective_addr = (uint64_t) qemu_plugin_insn_vaddr(insn);
        }

        /*
         * Instructions might get translated multiple times, we do not create
         * new entries for those instructions. Instead, we fetch the same
         * entry from the hash table and register it for the callback again.
         */
        data = g_hash_table_lookup(miss_ht, GUINT_TO_POINTER(effective_addr));
        if (data == NULL) {
            data = g_new0(struct InsnData, 1);
            data->disas_str = qemu_plugin_insn_disas(insn);
            data->symbol = qemu_plugin_insn_symbol(insn);
            data->addr = effective_addr;
            g_hash_table_insert(miss_ht, GUINT_TO_POINTER(effective_addr),
                               (gpointer) data);
        }

        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem_access,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         rw, data);

        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_NO_REGS, data);
    }
}

static void free_insn(gpointer data)
{
    struct InsnData *insn = (struct InsnData *) data;
    g_free(insn->disas_str);
    g_free(insn);
}

static void free_cache(struct Cache *cache)
{
    for (int i = 0; i < cache->num_sets; i++) {
        g_free(cache->sets[i].blocks);
    }

    if (metadata_destroy) {
        metadata_destroy(cache);
    }

    g_free(cache->sets);
    g_free(cache);
}

static int dcmp(gconstpointer a, gconstpointer b)
{
    struct InsnData *insn_a = (struct InsnData *) a;
    struct InsnData *insn_b = (struct InsnData *) b;

    return insn_a->dmisses < insn_b->dmisses ? 1 : -1;
}

static int icmp(gconstpointer a, gconstpointer b)
{
    struct InsnData *insn_a = (struct InsnData *) a;
    struct InsnData *insn_b = (struct InsnData *) b;

    return insn_a->imisses < insn_b->imisses ? 1 : -1;
}

static void log_stats()
{
    g_autoptr(GString) rep = g_string_new("");
    g_string_append_printf(rep,
        "Data accesses: %lu, Misses: %lu\nMiss rate: %lf%%\n\n",
        dmem_accesses,
        dmisses,
        ((double) dmisses / dmem_accesses) * 100.0);

    g_string_append_printf(rep,
        "Instruction accesses: %lu, Misses: %lu\nMiss rate: %lf%%\n\n",
        imem_accesses,
        imisses,
        ((double) imisses / imem_accesses) * 100.0);

    qemu_plugin_outs(rep->str);
}

static void plugin_exit()
{
    GList *curr, *miss_insns;
    int i;
    struct InsnData *insn;

    g_mutex_lock(&mtx);

    log_stats();

    miss_insns = g_hash_table_get_values(miss_ht);
    miss_insns = g_list_sort(miss_insns, dcmp);
    g_autoptr(GString) rep = g_string_new("");
    g_string_append_printf(rep, "%s", "address, data misses, instruction\n");

    for (curr = miss_insns, i = 0; curr && i < limit; i++, curr = curr->next) {
        insn = (struct InsnData *) curr->data;
        g_string_append_printf(rep, "0x%" PRIx64, insn->addr);
        if (insn->symbol) {
            g_string_append_printf(rep, " (%s)", insn->symbol);
        }
        g_string_append_printf(rep, ", %ld, %s\n", insn->dmisses,
                               insn->disas_str);
    }

    miss_insns = g_list_sort(miss_insns, icmp);
    g_string_append_printf(rep, "%s", "\naddress, fetch misses, instruction\n");

    for (curr = miss_insns, i = 0; curr && i < limit; i++, curr = curr->next) {
        insn = (struct InsnData *) curr->data;
        g_string_append_printf(rep, "0x%" PRIx64, insn->addr);
        if (insn->symbol) {
            g_string_append_printf(rep, " (%s)", insn->symbol);
        }
        g_string_append_printf(rep, ", %ld, %s\n", insn->imisses,
                               insn->disas_str);
    }

    qemu_plugin_outs(rep->str);

    free_cache(dcache);
    free_cache(icache);

    g_list_free(miss_insns);

    g_hash_table_destroy(miss_ht);
    g_mutex_unlock(&mtx);
}

static void policy_init()
{
    switch (policy) {
    case LRU:
        update_hit = lru_update_blk;
        update_miss = lru_update_blk;
        metadata_init = lru_priorities_init;
        metadata_destroy = lru_priorities_destroy;
        break;
    case FIFO:
        update_miss = fifo_update_on_miss;
        metadata_init = fifo_init;
        metadata_destroy = fifo_destroy;
        break;
    case RAND:
        rng = g_rand_new();
        break;
    default:
        g_assert_not_reached();
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    int i;
    int iassoc, iblksize, icachesize;
    int dassoc, dblksize, dcachesize;

    limit = 32;
    sys = info->system_emulation;

    dassoc = 8;
    dblksize = 64;
    dcachesize = dblksize * dassoc * 32;

    iassoc = 8;
    iblksize = 64;
    icachesize = iblksize * iassoc * 32;

    policy = LRU;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        if (g_str_has_prefix(opt, "I=")) {
            gchar **toks = g_strsplit(opt + 2, " ", -1);
            if (g_strv_length(toks) != 3) {
                g_strfreev(toks);
                fprintf(stderr, "option parsing failed: %s\n", opt);
                return -1;
            }
            icachesize = g_ascii_strtoull(toks[0], NULL, 10);
            iassoc = g_ascii_strtoull(toks[1], NULL, 10);
            iblksize = g_ascii_strtoull(toks[2], NULL, 10);
            g_strfreev(toks);
        } else if (g_str_has_prefix(opt, "D=")) {
            gchar **toks = g_strsplit(opt + 2, " ", -1);
            if (g_strv_length(toks) != 3) {
                g_strfreev(toks);
                fprintf(stderr, "option parsing failed: %s\n", opt);
                return -1;
            }
            dcachesize = g_ascii_strtoull(toks[0], NULL, 10);
            dassoc = g_ascii_strtoull(toks[1], NULL, 10);
            dblksize = g_ascii_strtoull(toks[2], NULL, 10);
            g_strfreev(toks);
        } else if (g_str_has_prefix(opt, "limit=")) {
            limit = g_ascii_strtoll(opt + 6, NULL, 10);
        } else if (g_str_has_prefix(opt, "evict=")) {
            gchar *p = opt + 6;
            if (g_strcmp0(p, "rand") == 0) {
                policy = RAND;
            } else if (g_strcmp0(p, "lru") == 0) {
                policy = LRU;
            } else if (g_strcmp0(p, "fifo") == 0) {
                policy = FIFO;
            } else {
                fprintf(stderr, "invalid eviction policy: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    policy_init();

    dcache = cache_init(dblksize, dassoc, dcachesize);
    if (!dcache) {
        fprintf(stderr, "dcache cannot be constructed from given parameters\n");
        return -1;
    }

    icache = cache_init(iblksize, iassoc, icachesize);
    if (!icache) {
        fprintf(stderr, "icache cannot be constructed from given parameters\n");
        return -1;
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    miss_ht = g_hash_table_new_full(NULL, g_direct_equal, NULL, free_insn);

    return 0;
}
