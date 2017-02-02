/*
 *  Software MMU support
 *
 * Generate helpers used by TCG for qemu_ld/st vector ops and code
 * load functions.
 *
 * Included from target op helpers and exec.c.
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
#include "qemu/timer.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"

#define DATA_SIZE (1 << SHIFT)

#if DATA_SIZE == 16
#define SUFFIX v128
#else
#error unsupported data size
#endif


#ifdef SOFTMMU_CODE_ACCESS
#define READ_ACCESS_TYPE MMU_INST_FETCH
#define ADDR_READ addr_code
#else
#define READ_ACCESS_TYPE MMU_DATA_LOAD
#define ADDR_READ addr_read
#endif

#define helper_te_ld_name  glue(glue(helper_te_ld, SUFFIX), MMUSUFFIX)
#define helper_te_st_name  glue(glue(helper_te_st, SUFFIX), MMUSUFFIX)

#ifndef SOFTMMU_CODE_ACCESS
static inline void glue(io_read, SUFFIX)(CPUArchState *env,
                                         CPUIOTLBEntry *iotlbentry,
                                         target_ulong addr,
                                         uintptr_t retaddr,
                                         uint8_t *res)
{
    CPUState *cpu = ENV_GET_CPU(env);
    hwaddr physaddr = iotlbentry->addr;
    MemoryRegion *mr = iotlb_to_region(cpu, physaddr, iotlbentry->attrs);
    int i;

    assert(0); /* Needs testing */

    physaddr = (physaddr & TARGET_PAGE_MASK) + addr;
    cpu->mem_io_pc = retaddr;
    if (mr != &io_mem_rom && mr != &io_mem_notdirty && !cpu->can_do_io) {
        cpu_io_recompile(cpu, retaddr);
    }

    cpu->mem_io_vaddr = addr;
    for (i = 0; i < (1 << SHIFT); i += 8) {
        memory_region_dispatch_read(mr, physaddr + i, (uint64_t *)(res + i),
                                    8, iotlbentry->attrs);
    }
}
#endif

void helper_te_ld_name(CPUArchState *env, target_ulong addr,
                       TCGMemOpIdx oi, uintptr_t retaddr, uint8_t *res)
{
    unsigned mmu_idx = get_mmuidx(oi);
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    target_ulong tlb_addr = env->tlb_table[mmu_idx][index].ADDR_READ;
    uintptr_t haddr;
    int i;

    /* Adjust the given return address.  */
    retaddr -= GETPC_ADJ;

    /* If the TLB entry is for a different page, reload and try again.  */
    if ((addr & TARGET_PAGE_MASK)
         != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if ((addr & (DATA_SIZE - 1)) != 0
            && (get_memop(oi) & MO_AMASK) == MO_ALIGN) {
            cpu_unaligned_access(ENV_GET_CPU(env), addr, READ_ACCESS_TYPE,
                                 mmu_idx, retaddr);
        }
        if (!VICTIM_TLB_HIT(ADDR_READ, addr)) {
            tlb_fill(ENV_GET_CPU(env), addr, READ_ACCESS_TYPE,
                     mmu_idx, retaddr);
        }
        tlb_addr = env->tlb_table[mmu_idx][index].ADDR_READ;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        CPUIOTLBEntry *iotlbentry;
        if ((addr & (DATA_SIZE - 1)) != 0) {
            goto do_unaligned_access;
        }
        iotlbentry = &env->iotlb[mmu_idx][index];

        /* ??? Note that the io helpers always read data in the target
           byte ordering.  We should push the LE/BE request down into io.  */
        glue(io_read, SUFFIX)(env, iotlbentry, addr, retaddr, res);
        return ;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (DATA_SIZE > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + DATA_SIZE - 1
                    >= TARGET_PAGE_SIZE)) {
        target_ulong addr1, addr2;
        uint8_t res1[DATA_SIZE * 2];
        unsigned shift;
    do_unaligned_access:
        if ((get_memop(oi) & MO_AMASK) == MO_ALIGN) {
            cpu_unaligned_access(ENV_GET_CPU(env), addr, READ_ACCESS_TYPE,
                                 mmu_idx, retaddr);
        }
        addr1 = addr & ~(DATA_SIZE - 1);
        addr2 = addr1 + DATA_SIZE;
        /* Note the adjustment at the beginning of the function.
           Undo that for the recursion.  */
        helper_te_ld_name(env, addr1, oi, retaddr + GETPC_ADJ, res1);
        helper_te_ld_name(env, addr2, oi, retaddr + GETPC_ADJ,
                          res1 + DATA_SIZE);
        shift = addr & (DATA_SIZE - 1);

        for (i = 0; i < DATA_SIZE; i++) {
            res[i] = res1[i + shift];
        }
        return;
    }

    /* Handle aligned access or unaligned access in the same page.  */
    if ((addr & (DATA_SIZE - 1)) != 0
        && (get_memop(oi) & MO_AMASK) == MO_ALIGN) {
        cpu_unaligned_access(ENV_GET_CPU(env), addr, READ_ACCESS_TYPE,
                             mmu_idx, retaddr);
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;
    for (i = 0; i < DATA_SIZE; i++) {
        res[i] = ((uint8_t *)haddr)[i];
    }
}

#ifndef SOFTMMU_CODE_ACCESS

static inline void glue(io_write, SUFFIX)(CPUArchState *env,
                                          CPUIOTLBEntry *iotlbentry,
                                          uint8_t *val,
                                          target_ulong addr,
                                          uintptr_t retaddr)
{
    CPUState *cpu = ENV_GET_CPU(env);
    hwaddr physaddr = iotlbentry->addr;
    MemoryRegion *mr = iotlb_to_region(cpu, physaddr, iotlbentry->attrs);
    int i;

    assert(0); /* Needs testing */

    physaddr = (physaddr & TARGET_PAGE_MASK) + addr;
    if (mr != &io_mem_rom && mr != &io_mem_notdirty && !cpu->can_do_io) {
        cpu_io_recompile(cpu, retaddr);
    }

    cpu->mem_io_vaddr = addr;
    cpu->mem_io_pc = retaddr;
    for (i = 0; i < (1 << SHIFT); i += 8) {
        memory_region_dispatch_write(mr, physaddr + i, *(uint64_t *)(val + i),
                                     8, iotlbentry->attrs);
    }
}

void helper_te_st_name(CPUArchState *env, target_ulong addr, uint8_t *val,
                       TCGMemOpIdx oi, uintptr_t retaddr)
{
    unsigned mmu_idx = get_mmuidx(oi);
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    target_ulong tlb_addr = env->tlb_table[mmu_idx][index].addr_write;
    uintptr_t haddr;
    int i;

    /* Adjust the given return address.  */
    retaddr -= GETPC_ADJ;

    /* If the TLB entry is for a different page, reload and try again.  */
    if ((addr & TARGET_PAGE_MASK)
        != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if ((addr & (DATA_SIZE - 1)) != 0
            && (get_memop(oi) & MO_AMASK) == MO_ALIGN) {
            cpu_unaligned_access(ENV_GET_CPU(env), addr, MMU_DATA_STORE,
                                 mmu_idx, retaddr);
        }
        if (!VICTIM_TLB_HIT(addr_write, addr)) {
            tlb_fill(ENV_GET_CPU(env), addr, MMU_DATA_STORE, mmu_idx, retaddr);
        }
        tlb_addr = env->tlb_table[mmu_idx][index].addr_write;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        CPUIOTLBEntry *iotlbentry;
        if ((addr & (DATA_SIZE - 1)) != 0) {
            goto do_unaligned_access;
        }
        iotlbentry = &env->iotlb[mmu_idx][index];

        /* ??? Note that the io helpers always read data in the target
           byte ordering.  We should push the LE/BE request down into io.  */
        glue(io_write, SUFFIX)(env, iotlbentry, val, addr, retaddr);
        return;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (DATA_SIZE > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + DATA_SIZE - 1
                     >= TARGET_PAGE_SIZE)) {
        int i;
    do_unaligned_access:
        if ((get_memop(oi) & MO_AMASK) == MO_ALIGN) {
            cpu_unaligned_access(ENV_GET_CPU(env), addr, MMU_DATA_STORE,
                                 mmu_idx, retaddr);
        }
        /* XXX: not efficient, but simple */
        /* Note: relies on the fact that tlb_fill() does not remove the
         * previous page from the TLB cache.  */
        for (i = DATA_SIZE - 1; i >= 0; i--) {
            /* Note the adjustment at the beginning of the function.
               Undo that for the recursion.  */
            glue(helper_ret_stb, MMUSUFFIX)(env, addr + i, val[i],
                                            oi, retaddr + GETPC_ADJ);
        }
        return;
    }

    /* Handle aligned access or unaligned access in the same page.  */
    if ((addr & (DATA_SIZE - 1)) != 0
        && (get_memop(oi) & MO_AMASK) == MO_ALIGN) {
        cpu_unaligned_access(ENV_GET_CPU(env), addr, MMU_DATA_STORE,
                             mmu_idx, retaddr);
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;
    for (i = 0; i < DATA_SIZE; i++) {
        ((uint8_t *)haddr)[i] = val[i];
    }
}

#endif /* !defined(SOFTMMU_CODE_ACCESS) */

#undef READ_ACCESS_TYPE
#undef SHIFT
#undef SUFFIX
#undef DATA_SIZE
#undef ADDR_READ
#undef helper_te_ld_name
#undef helper_te_st_name
