/*
 * QEMU System Emulator, Code Quality Monitor System
 *
 * Copyright (c) 2019 Vanderson M. do Rosario <vandersonmr2@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/xxhash.h"
#include "tcg/tb-stats.h"
#include "tb-context.h"

uint32_t tb_stats_enabled;

static bool tb_stats_cmp(const void *ap, const void *bp)
{
    const TBStatistics *a = ap;
    const TBStatistics *b = bp;

    return a->phys_pc == b->phys_pc &&
           a->pc == b->pc &&
           a->flags == b->flags &&
           a->flags2 == b->flags2;

}

static void tb_stats_free(void *p, uint32_t hash, void *userp)
{
    TBStatistics *s = p;

    qemu_mutex_destroy(&s->jit_stats_lock);
    g_ptr_array_free(s->tbs, true);
    g_free(s);
}

void tb_stats_init(uint32_t flags)
{
    tb_stats_enabled = flags;
    if (flags) {
        if (!tb_ctx.stats.map) {
            qht_init(&tb_ctx.stats, tb_stats_cmp,
                     CODE_GEN_HTABLE_SIZE, QHT_MODE_AUTO_RESIZE);
        }
    } else {
        qht_iter(&tb_ctx.stats, tb_stats_free, NULL);
        qht_destroy(&tb_ctx.stats);
    }
}

static void tb_stats_reset(void *p, uint32_t hash, void *userp)
{
    TBStatistics *s = p;
    g_ptr_array_set_size(s->tbs, 0);
}

void tb_stats_reset_tbs(void)
{
    if (tb_ctx.stats.map) {
        qht_iter(&tb_ctx.stats, tb_stats_reset, NULL);
    }
}

TBStatistics *tb_stats_lookup(tb_page_addr_t phys_pc, vaddr pc,
                              uint32_t flags, uint64_t flags2)
{
    TBStatistics *s;
    uint32_t h;
    void *existing;

    s = g_new0(TBStatistics, 1);
    s->phys_pc = phys_pc;
    s->pc = pc;
    s->flags = flags;
    s->flags2 = flags2;
    s->tbs = g_ptr_array_new();
    qemu_mutex_init(&s->jit_stats_lock);

    h = qemu_xxhash7(phys_pc, pc, flags2, flags);
    if (!qht_insert(&tb_ctx.stats, s, h, &existing)) {
        tb_stats_free(s, 0, NULL);
        return existing;
    }
    return s;
}
