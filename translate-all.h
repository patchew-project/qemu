/*
 *  Translated block handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TRANSLATE_ALL_H
#define TRANSLATE_ALL_H

#include "exec/exec-all.h"
#include "qemu/typedefs.h"

/**
 * tb_caches_count:
 *
 * Number of TB caches.
 */
static size_t tb_caches_count(void);

/**
 * tb_caches_get:
 *
 * Get the TB cache for the given bitmap index.
 */
static struct qht *tb_caches_get(TBContext *tb_ctx, unsigned long *bitmap);

/**
 * cpu_tb_cache_set_request:
 *
 * Request a physical TB cache switch on this @cpu.
 */
void cpu_tb_cache_set_request(CPUState *cpu);

/**
 * cpu_tb_cache_set_requested:
 *
 * Returns: %true if @cpu requested a physical TB cache switch, %false
 *          otherwise.
 */
bool cpu_tb_cache_set_requested(CPUState *cpu);

/**
 * cput_tb_cache_set_apply:
 *
 * Apply a physical TB cache switch.
 *
 * Precondition: @cpu is not currently executing any TB.
 *
 * Note: Invalidates the jump cache of the given vCPU.
 */
void cpu_tb_cache_set_apply(CPUState *cpu);

/* translate-all.c */
void tb_invalidate_phys_page_fast(tb_page_addr_t start, int len);
void tb_invalidate_phys_page_range(tb_page_addr_t start, tb_page_addr_t end,
                                   int is_cpu_write_access);
void tb_invalidate_phys_range(tb_page_addr_t start, tb_page_addr_t end);
void tb_check_watchpoint(CPUState *cpu);

#ifdef CONFIG_USER_ONLY
int page_unprotect(target_ulong address, uintptr_t pc);
#endif


#include "translate-all.inc.h"

#endif /* TRANSLATE_ALL_H */
