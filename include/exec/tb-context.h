/*
 * Internal structs that QEMU exports to TCG
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

#ifndef QEMU_TB_CONTEXT_H
#define QEMU_TB_CONTEXT_H

#include "qemu/thread.h"
#include "qemu/qht.h"

/* Page tracking code uses ram addresses in system mode, and virtual
   addresses in userspace mode.  Define tb_page_addr_t to be an appropriate
   type.  */
#if defined(CONFIG_USER_ONLY)
typedef abi_ulong tb_page_addr_t;
#define TB_PAGE_ADDR_FMT TARGET_ABI_FMT_lx
#else
typedef ram_addr_t tb_page_addr_t;
#define TB_PAGE_ADDR_FMT RAM_ADDR_FMT
#endif

#define CODE_GEN_HTABLE_BITS     15
#define CODE_GEN_HTABLE_SIZE     (1 << CODE_GEN_HTABLE_BITS)

typedef struct TranslationBlock TranslationBlock;
typedef struct TBContext TBContext;

struct TBContext {

    struct qht htable;

    /* statistics */
    unsigned tb_flush_count;
    struct qht tb_stats;
};

extern TBContext tb_ctx;

#endif
