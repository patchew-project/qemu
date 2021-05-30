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

static bool fifo, lru, rnd;

static GRand *rng;
static GHashTable *dmiss_ht;
static GHashTable *imiss_ht;

static GMutex dmtx, imtx, fmtx;

static int limit;
static bool sys;

static uint64_t dmem_accesses;
static uint64_t dmisses;

static uint64_t imem_accesses;
static uint64_t imisses;

FILE *tracefile;

static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

enum AccessResult {
    HIT = 0,
    MISS = 1
};

struct InsnData {
    char *disas_str;
    uint64_t addr;
    uint64_t misses;
};

struct CacheBlock {
    uint64_t tag;
    bool valid;
};

struct CacheSet {
    struct CacheBlock *blocks;
    uint16_t *priorities;
    GQueue *evict_queue;
};

struct Cache {
    struct CacheSet *sets;
    int num_sets;

    int cachesize;
    int blksize;
    int assoc;

    uint64_t blk_mask;
    uint64_t set_mask;
    uint64_t tag_mask;
};

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
    return (addr & cache->tag_mask) >>
        (pow_of_two(cache->num_sets) + pow_of_two(cache->blksize));
}

static inline uint64_t extract_set(struct Cache *cache, uint64_t addr)
{
    return (addr & cache->set_mask) >> (pow_of_two(cache->blksize));
}

static void lru_priorities_init(struct Cache *cache)
{
    int i, j;

    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].priorities = g_new(uint16_t, cache->assoc);
        for (j = 0; j < cache->assoc; j++) {
            cache->sets[i].priorities[j] = cache->assoc - j - 1;
        }
    }
}

static void lru_update_on_miss(struct Cache *cache,
                                      int set_idx,
                                      int blk_idx)
{
    int i;

    for (i = 0; i < cache->assoc; i++) {
        cache->sets[set_idx].priorities[i]++;
    }

    cache->sets[set_idx].priorities[blk_idx] = 0;
}

static void lru_update_on_hit(struct Cache *cache,
                                         int set_idx,
                                         int blk_idx)
{
    uint16_t blk_priority;
    int i;

    blk_priority = cache->sets[set_idx].priorities[blk_idx];
    for (i = 0; i < cache->assoc; i++) {
        if (cache->sets[set_idx].priorities[i] < blk_priority) {
            cache->sets[set_idx].priorities[i]++;
        }
    }
    cache->sets[set_idx].priorities[blk_idx] = 0;
}

static int lru_get_lru_block(struct Cache *cache, int set_idx)
{
    int i;

    for (i = 0; i < cache->assoc; i++) {
        if (cache->sets[set_idx].priorities[i] == cache->assoc - 1) {
            return i;
        }
    }

    g_assert_not_reached();
}

static void fifo_init(struct Cache *cache)
{
    int i;

    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].evict_queue = g_queue_new();
    }
}

static int fifo_get_first_in_block(struct Cache *cache, int set)
{
    GQueue *q = cache->sets[set].evict_queue;
    return GPOINTER_TO_INT(g_queue_pop_tail(q));
}

static void fifo_update_on_miss(struct Cache *cache,
                                int set,
                                int blk_idx)
{
    GQueue *q = cache->sets[set].evict_queue;
    g_queue_push_head(q, GINT_TO_POINTER(blk_idx));
}


static struct Cache *cache_init(int blksize, int assoc, int cachesize)
{
    struct Cache *cache;
    int i;

    cache = g_new(struct Cache, 1);
    cache->blksize = blksize;
    cache->assoc = assoc;
    cache->cachesize = cachesize;
    cache->num_sets = cachesize / (blksize * assoc);
    cache->sets = g_new(struct CacheSet, cache->num_sets);

    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].blocks = g_new0(struct CacheBlock, assoc);
    }

    cache->blk_mask = blksize - 1;
    cache->set_mask = ((cache->num_sets - 1) << (pow_of_two(cache->blksize)));
    cache->tag_mask = ~(cache->set_mask | cache->blk_mask);

    if (lru) {
        lru_priorities_init(cache);
    } else if (fifo) {
        fifo_init(cache);
    }

    return cache;
}

static int get_invalid_block(struct Cache *cache, uint64_t set)
{
    int i;

    for (i = 0; i < cache->assoc; i++) {
        if (!cache->sets[set].blocks[i].valid) {
            /* conflict miss */
            return i;
        }
    }

    /* compulsary miss */
    return -1;
}

static int get_replaced_block(struct Cache *cache, int set)
{
    if (rnd) {
        return g_rand_int_range(rng, 0, cache->assoc);
    } else if (lru) {
        return lru_get_lru_block(cache, set);
    } else if (fifo) {
        return fifo_get_first_in_block(cache, set);
    }

    g_assert_not_reached();
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

static enum AccessResult access_cache(struct Cache *cache, uint64_t addr)
{
    uint64_t tag, set;
    int hit_blk, replaced_blk;

    tag = extract_tag(cache, addr);
    set = extract_set(cache, addr);
    hit_blk = in_cache(cache, addr);

    if (hit_blk != -1) {
        if (lru) {
            lru_update_on_hit(cache, set, hit_blk);
        }
        return HIT;
    }

    replaced_blk = get_invalid_block(cache, set);

    if (replaced_blk == -1) {
        replaced_blk = get_replaced_block(cache, set);
    }

    if (lru) {
        lru_update_on_miss(cache, set, replaced_blk);
    } else if (fifo) {
        fifo_update_on_miss(cache, set, replaced_blk);
    }

    cache->sets[set].blocks[replaced_blk].tag = tag;
    cache->sets[set].blocks[replaced_blk].valid = true;

    return MISS;
}

struct InsnData *get_or_create(GHashTable *ht, struct InsnData *insn_data,
                               uint64_t addr)
{
    struct InsnData *insn = g_hash_table_lookup(ht, GUINT_TO_POINTER(addr));
    if (!insn) {
        g_hash_table_insert(ht, GUINT_TO_POINTER(addr), (gpointer) insn_data);
        insn = insn_data;
    }

    return insn;
}

static void vcpu_mem_access(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                            uint64_t vaddr, void *userdata)
{
    uint64_t insn_addr;
    uint64_t effective_addr;
    struct qemu_plugin_hwaddr *hwaddr;

    g_mutex_lock(&dmtx);
    hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    if (hwaddr && qemu_plugin_hwaddr_is_io(hwaddr)) {
        g_mutex_unlock(&dmtx);
        return;
    }

    insn_addr = ((struct InsnData *) userdata)->addr;
    effective_addr = hwaddr ? qemu_plugin_hwaddr_phys_addr(hwaddr) : vaddr;

    if (tracefile) {
        g_mutex_lock(&fmtx);
        g_autoptr(GString) rep = g_string_new("");
        bool is_store = qemu_plugin_mem_is_store(info);
        g_string_append_printf(rep, "%c: 0x%" PRIx64,
                is_store ? 'S' : 'L', effective_addr);
        fprintf(tracefile, "%s\n", rep->str);
        g_mutex_unlock(&fmtx);
    }

    if (access_cache(dcache, effective_addr) == MISS) {
        struct InsnData *insn = get_or_create(dmiss_ht, userdata, insn_addr);
        insn->misses++;
        dmisses++;
    }
    dmem_accesses++;
    g_mutex_unlock(&dmtx);
}

static void vcpu_insn_exec(unsigned int vcpu_index, void *userdata)
{
    uint64_t addr;

    g_mutex_lock(&imtx);
    addr = ((struct InsnData *) userdata)->addr;

    if (tracefile) {
        g_mutex_lock(&fmtx);
        g_autoptr(GString) rep = g_string_new("");
        g_string_append_printf(rep, "I: 0x%" PRIx64, addr);
        fprintf(tracefile, "%s\n", rep->str);
        g_mutex_unlock(&fmtx);
    }

    if (access_cache(icache, addr) == MISS) {
        struct InsnData *insn = get_or_create(imiss_ht, userdata, addr);
        insn->misses++;
        imisses++;
    }

    imem_accesses++;
    g_mutex_unlock(&imtx);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns;
    size_t i;

    n_insns = qemu_plugin_tb_n_insns(tb);
    for (i = 0; i < n_insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t effective_addr;

        if (sys) {
            effective_addr = (uint64_t) qemu_plugin_insn_haddr(insn);
        } else {
            effective_addr = (uint64_t) qemu_plugin_insn_vaddr(insn);
        }

        struct InsnData *ddata = g_new(struct InsnData, 1);
        struct InsnData *idata = g_new(struct InsnData, 1);

        ddata->disas_str = qemu_plugin_insn_disas(insn);
        ddata->misses = 0;
        ddata->addr = effective_addr;

        idata->disas_str = g_strdup(ddata->disas_str);
        idata->misses = 0;
        idata->addr = effective_addr;

        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem_access,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         rw, ddata);

        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_NO_REGS, idata);
    }
}

static void print_entry(gpointer data)
{
    struct InsnData *insn = (struct InsnData *) data;
    g_autoptr(GString) xx = g_string_new("");
    g_string_append_printf(xx, "0x%" PRIx64 ": %s - misses: %lu\n",
            insn->addr, insn->disas_str, insn->misses);
    qemu_plugin_outs(xx->str);
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
        if (lru) {
            g_free(cache->sets[i].priorities);
        } else if (fifo) {
            g_queue_free(cache->sets[i].evict_queue);
        }
    }

    g_free(cache->sets);
}

static int cmp(gconstpointer a, gconstpointer b)
{
    struct InsnData *insn_a = (struct InsnData *) a;
    struct InsnData *insn_b = (struct InsnData *) b;

    return insn_a->misses < insn_b->misses ? 1 : -1;
}

static void print_stats()
{
    g_autoptr(GString) rep = g_string_new("");
    g_string_append_printf(rep,
            "Data accesses: %lu, Misses: %lu\nMiss rate: %lf%%\n\n",
            dmem_accesses,
            dmisses,
            ((double)dmisses / dmem_accesses) * 100.0);

    g_string_append_printf(rep,
            "Instruction accesses: %lu, Misses: %lu\nMiss rate: %lf%%\n\n",
            imem_accesses,
            imisses,
            ((double)imisses / imem_accesses) * 100.0);

    qemu_plugin_outs(rep->str);
}

static void plugin_exit()
{
    GList *curr;
    int i;

    g_mutex_lock(&imtx);
    g_mutex_lock(&dmtx);
    GList *dmiss_insns = g_hash_table_get_values(dmiss_ht);
    GList *imiss_insns = g_hash_table_get_values(imiss_ht);
    dmiss_insns = g_list_sort(dmiss_insns, cmp);
    imiss_insns = g_list_sort(imiss_insns, cmp);

    print_stats();

    qemu_plugin_outs("Most data-missing instructions\n");
    for (curr = dmiss_insns, i = 0; curr && i < limit; i++, curr = curr->next) {
        print_entry(curr->data);
    }

    qemu_plugin_outs("\nMost fetch-missing instructions\n");
    for (curr = imiss_insns, i = 0; curr && i < limit; i++, curr = curr->next) {
        print_entry(curr->data);
    }

    free_cache(dcache);
    free_cache(icache);

    g_list_free(dmiss_insns);
    g_list_free(imiss_insns);

    g_hash_table_destroy(dmiss_ht);
    g_hash_table_destroy(imiss_ht);

    g_mutex_unlock(&dmtx);
    g_mutex_unlock(&imtx);

    if (tracefile) {
        fclose(tracefile);
    }
}

static bool bad_cache_params(int blksize, int assoc, int cachesize)
{
    return (cachesize % blksize) != 0 || (cachesize % (blksize * assoc) != 0);
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

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        if (g_str_has_prefix(opt, "I=")) {
            gchar **toks = g_strsplit(opt + 2, " ", -1);
            if (g_strv_length(toks) != 3) {
                fprintf(stderr, "option parsing failed: %s\n", opt);
                return -1;
            }
            icachesize = g_ascii_strtoull(toks[0], NULL, 10);
            iassoc = g_ascii_strtoull(toks[1], NULL, 10);
            iblksize = g_ascii_strtoull(toks[2], NULL, 10);
        } else if (g_str_has_prefix(opt, "D=")) {
            gchar **toks = g_strsplit(opt + 2, " ", -1);
            if (g_strv_length(toks) != 3) {
                fprintf(stderr, "option parsing failed: %s\n", opt);
                return -1;
            }
            dcachesize = g_ascii_strtoull(toks[0], NULL, 10);
            dassoc = g_ascii_strtoull(toks[1], NULL, 10);
            dblksize = g_ascii_strtoull(toks[2], NULL, 10);
        } else if (g_str_has_prefix(opt, "limit=")) {
            limit = g_ascii_strtoull(opt + 6, NULL, 10);
        } else if (g_str_has_prefix(opt, "tracefile=")) {
            char *file_name = opt + 10;
            tracefile = fopen(file_name, "w");
            if (!tracefile) {
                fprintf(stderr, "could not open: %s for writing\n", file_name);
            }
        } else if (g_str_has_prefix(opt, "evict=")) {
            if (lru || rnd || fifo) {
                fprintf(stderr, "eviction policy specified more than once\n");
                return -1;
            }
            gchar *policy = opt + 6;
            if (g_strcmp0(policy, "rand") == 0) {
                rnd = true;
            } else if (g_strcmp0(policy, "lru") == 0) {
                lru = true;
            } else if (g_strcmp0(policy, "fifo") == 0) {
                fifo = true;
            } else {
                fprintf(stderr, "invalid eviction policy: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    if (bad_cache_params(iblksize, iassoc, icachesize)) {
        fprintf(stderr, "icache cannot be constructed from given parameters\n");
        return -1;
    }

    if (bad_cache_params(dblksize, dassoc, dcachesize)) {
        fprintf(stderr, "dcache cannot be constructed from given parameters\n");
        return -1;
    }

    if (!rnd && !lru && !fifo) {
        lru = true;
    }

    if (rnd) {
        rng = g_rand_new();
    }

    dcache = cache_init(dblksize, dassoc, dcachesize);
    icache = cache_init(iblksize, iassoc, icachesize);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    dmiss_ht = g_hash_table_new_full(NULL, g_direct_equal, NULL, free_insn);
    imiss_ht = g_hash_table_new_full(NULL, g_direct_equal, NULL, free_insn);

    return 0;
}
