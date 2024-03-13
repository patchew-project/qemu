/*
 * Internal execution defines for qemu (target specific)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_INTERNAL_TARGET_H
#define ACCEL_TCG_INTERNAL_TARGET_H

#include "exec/exec-all.h"
#include "exec/translate-all.h"

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_TCG)
#define CPU_TLB_DYN_MIN_BITS 6
#define CPU_TLB_DYN_DEFAULT_BITS 8

# if HOST_LONG_BITS == 32
/* Make sure we do not require a double-word shift for the TLB load */
#  define CPU_TLB_DYN_MAX_BITS (32 - TARGET_PAGE_BITS)
# else /* HOST_LONG_BITS == 64 */
/*
 * Assuming TARGET_PAGE_BITS==12, with 2**22 entries we can cover 2**(22+12) ==
 * 2**34 == 16G of address space. This is roughly what one would expect a
 * TLB to cover in a modern (as of 2018) x86_64 CPU. For instance, Intel
 * Skylake's Level-2 STLB has 16 1G entries.
 * Also, make sure we do not size the TLB past the guest's address space.
 */
#  ifdef TARGET_PAGE_BITS_VARY
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  else
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN_CONST(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  endif
# endif

#endif /* CONFIG_SOFTMMU && CONFIG_TCG */

/*
 * Access to the various translations structures need to be serialised
 * via locks for consistency.  In user-mode emulation access to the
 * memory related structures are protected with mmap_lock.
 * In !user-mode we use per-page locks.
 */
#ifdef CONFIG_USER_ONLY
#define assert_memory_lock() tcg_debug_assert(have_mmap_lock())
#else
#define assert_memory_lock()
#endif

#if !defined(CONFIG_USER_ONLY) && defined(CONFIG_DEBUG_TCG)
void assert_no_pages_locked(void);
#else
static inline void assert_no_pages_locked(void) { }
#endif

#ifdef CONFIG_USER_ONLY
static inline void page_table_config_init(void) { }
#else
void page_table_config_init(void);
#endif

#ifdef CONFIG_USER_ONLY
/*
 * For user-only, page_protect sets the page read-only.
 * Since most execution is already on read-only pages, and we'd need to
 * account for other TBs on the same page, defer undoing any page protection
 * until we receive the write fault.
 */
static inline void tb_lock_page0(tb_page_addr_t p0)
{
    page_protect(p0);
}

static inline void tb_lock_page1(tb_page_addr_t p0, tb_page_addr_t p1)
{
    page_protect(p1);
}

static inline void tb_unlock_page1(tb_page_addr_t p0, tb_page_addr_t p1) { }
static inline void tb_unlock_pages(TranslationBlock *tb) { }
#else
void tb_lock_page0(tb_page_addr_t);
void tb_lock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_pages(TranslationBlock *);
#endif

#ifndef CONFIG_USER_ONLY
void tb_invalidate_phys_range_fast(ram_addr_t ram_addr,
                                   unsigned size,
                                   uintptr_t retaddr);
G_NORETURN void cpu_io_recompile(CPUState *cpu, uintptr_t retaddr);
#endif /* !CONFIG_USER_ONLY */

TranslationBlock *tb_gen_code(CPUState *cpu, vaddr pc,
                              uint64_t cs_base, uint32_t flags,
                              int cflags);
void page_init(void);
void tb_htable_init(void);
void tb_reset_jump(TranslationBlock *tb, int n);
TranslationBlock *tb_link_page(TranslationBlock *tb);
bool tb_invalidate_phys_page_unwind(tb_page_addr_t addr, uintptr_t pc);
void cpu_restore_state_from_tb(CPUState *cpu, TranslationBlock *tb,
                               uintptr_t host_pc);

bool tcg_exec_realizefn(CPUState *cpu, Error **errp);
void tcg_exec_unrealizefn(CPUState *cpu);

/* Return the current PC from CPU, which may be cached in TB. */
static inline vaddr log_pc(CPUState *cpu, const TranslationBlock *tb)
{
    if (tb_cflags(tb) & CF_PCREL) {
        return cpu->cc->get_pc(cpu);
    } else {
        return tb->pc;
    }
}

extern bool one_insn_per_tb;

/**
 * tcg_req_mo:
 * @type: TCGBar
 *
 * Filter @type to the barrier that is required for the guest
 * memory ordering vs the host memory ordering.  A non-zero
 * result indicates that some barrier is required.
 *
 * If TCG_GUEST_DEFAULT_MO is not defined, assume that the
 * guest requires strict ordering.
 *
 * This is a macro so that it's constant even without optimization.
 */
#ifdef TCG_GUEST_DEFAULT_MO
# define tcg_req_mo(type) \
    ((type) & TCG_GUEST_DEFAULT_MO & ~TCG_TARGET_DEFAULT_MO)
#else
# define tcg_req_mo(type) ((type) & ~TCG_TARGET_DEFAULT_MO)
#endif

/**
 * cpu_req_mo:
 * @type: TCGBar
 *
 * If tcg_req_mo indicates a barrier for @type is required
 * for the guest memory model, issue a host memory barrier.
 */
#define cpu_req_mo(type)          \
    do {                          \
        if (tcg_req_mo(type)) {   \
            smp_mb();             \
        }                         \
    } while (0)

#endif /* ACCEL_TCG_INTERNAL_H */
