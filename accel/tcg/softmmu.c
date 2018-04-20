/*
 * Software MMU support
 *
 * Originally this was a set of generated helpers using macro magic.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cputlb.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"

/* Macro to call the above, with local variables from the use context.  */
#define VICTIM_TLB_HIT(TY, ADDR) \
  victim_tlb_hit(env, mmu_idx, index, offsetof(CPUTLBEntry, TY), \
                 (ADDR) & TARGET_PAGE_MASK)

/* For the benefit of TCG generated code, we want to avoid the complication
   of ABI-specific return type promotion and always return a value extended
   to the register size of the host.  This is tcg_target_long, except in the
   case of a 32-bit host and 64-bit data, and for that we always have
   uint64_t.  Don't bother with this widened value for SOFTMMU_CODE_ACCESS.  */
static inline uint8_t io_readb(CPUArchState *env,
                                              size_t mmu_idx, size_t index,
                                              target_ulong addr,
                                              uintptr_t retaddr)
{
    CPUIOTLBEntry *iotlbentry = &env->iotlb[mmu_idx][index];
    return io_readx(env, iotlbentry, mmu_idx, addr, retaddr, 1);
}


tcg_target_ulong helper_ret_ldub_mmu(CPUArchState *env, target_ulong addr,
                            TCGMemOpIdx oi, uintptr_t retaddr)
{
    unsigned mmu_idx = get_mmuidx(oi);
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    target_ulong tlb_addr = env->tlb_table[mmu_idx][index].addr_read;
    unsigned a_bits = get_alignment_bits(get_memop(oi));
    uintptr_t haddr;
    uint8_t res;

    if (addr & ((1 << a_bits) - 1)) {
        cpu_unaligned_access(ENV_GET_CPU(env), addr, MMU_DATA_LOAD,
                             mmu_idx, retaddr);
    }

    /* If the TLB entry is for a different page, reload and try again.  */
    if ((addr & TARGET_PAGE_MASK)
         != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if (!VICTIM_TLB_HIT(addr_read, addr)) {
            tlb_fill(ENV_GET_CPU(env), addr, 1, MMU_DATA_LOAD,
                     mmu_idx, retaddr);
        }
        tlb_addr = env->tlb_table[mmu_idx][index].addr_read;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        if ((addr & (1 - 1)) != 0) {
            goto do_unaligned_access;
        }

        /* ??? Note that the io helpers always read data in the target
           byte ordering.  We should push the LE/BE request down into io.  */
        res = io_readb(env, mmu_idx, index, addr, retaddr);
        res = (res);
        return res;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (1 > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + 1 - 1
                    >= TARGET_PAGE_SIZE)) {
        target_ulong addr1, addr2;
        uint8_t res1, res2;
        unsigned shift;
    do_unaligned_access:
        addr1 = addr & ~(1 - 1);
        addr2 = addr1 + 1;
        res1 = helper_ret_ldub_mmu(env, addr1, oi, retaddr);
        res2 = helper_ret_ldub_mmu(env, addr2, oi, retaddr);
        shift = (addr & (1 - 1)) * 8;

        /* Little-endian combine.  */
        res = (res1 >> shift) | (res2 << ((1 * 8) - shift));
        return res;
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;

    res = ldub_p((uint8_t *)haddr);



    return res;
}
/* Provide signed versions of the load routines as well.  We can of course
   avoid this for 64-bit data, or for 32-bit data on 32-bit host.  */
static inline void io_writeb(CPUArchState *env,
                                          size_t mmu_idx, size_t index,
                                          uint8_t val,
                                          target_ulong addr,
                                          uintptr_t retaddr)
{
    CPUIOTLBEntry *iotlbentry = &env->iotlb[mmu_idx][index];
    return io_writex(env, iotlbentry, mmu_idx, val, addr, retaddr, 1);
}

void helper_ret_stb_mmu(CPUArchState *env, target_ulong addr, uint8_t val,
                       TCGMemOpIdx oi, uintptr_t retaddr)
{
    unsigned mmu_idx = get_mmuidx(oi);
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    target_ulong tlb_addr = env->tlb_table[mmu_idx][index].addr_write;
    unsigned a_bits = get_alignment_bits(get_memop(oi));
    uintptr_t haddr;

    if (addr & ((1 << a_bits) - 1)) {
        cpu_unaligned_access(ENV_GET_CPU(env), addr, MMU_DATA_STORE,
                             mmu_idx, retaddr);
    }

    /* If the TLB entry is for a different page, reload and try again.  */
    if ((addr & TARGET_PAGE_MASK)
        != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if (!VICTIM_TLB_HIT(addr_write, addr)) {
            tlb_fill(ENV_GET_CPU(env), addr, 1, MMU_DATA_STORE,
                     mmu_idx, retaddr);
        }
        tlb_addr = env->tlb_table[mmu_idx][index].addr_write & ~TLB_INVALID_MASK;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        if ((addr & (1 - 1)) != 0) {
            goto do_unaligned_access;
        }

        /* ??? Note that the io helpers always read data in the target
           byte ordering.  We should push the LE/BE request down into io.  */
        val = (val);
        io_writeb(env, mmu_idx, index, val, addr, retaddr);
        return;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (1 > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + 1 - 1
                     >= TARGET_PAGE_SIZE)) {
        int i, index2;
        target_ulong page2, tlb_addr2;
    do_unaligned_access:
        /* Ensure the second page is in the TLB.  Note that the first page
           is already guaranteed to be filled, and that the second page
           cannot evict the first.  */
        page2 = (addr + 1) & TARGET_PAGE_MASK;
        index2 = (page2 >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        tlb_addr2 = env->tlb_table[mmu_idx][index2].addr_write;
        if (page2 != (tlb_addr2 & (TARGET_PAGE_MASK | TLB_INVALID_MASK))
            && !VICTIM_TLB_HIT(addr_write, page2)) {
            tlb_fill(ENV_GET_CPU(env), page2, 1, MMU_DATA_STORE,
                     mmu_idx, retaddr);
        }

        /* XXX: not efficient, but simple.  */
        /* This loop must go in the forward direction to avoid issues
           with self-modifying code in Windows 64-bit.  */
        for (i = 0; i < 1; ++i) {
            /* Little-endian extract.  */
            uint8_t val8 = val >> (i * 8);
            helper_ret_stb_mmu(env, addr + i, val8,
                                            oi, retaddr);
        }
        return;
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;

    stb_p((uint8_t *)haddr, val);



}

/* For the benefit of TCG generated code, we want to avoid the complication
   of ABI-specific return type promotion and always return a value extended
   to the register size of the host.  This is tcg_target_long, except in the
   case of a 32-bit host and 64-bit data, and for that we always have
   uint64_t.  Don't bother with this widened value for SOFTMMU_CODE_ACCESS.  */
uint8_t helper_ret_ldb_cmmu(CPUArchState *env, target_ulong addr,
                            TCGMemOpIdx oi, uintptr_t retaddr)
{
    unsigned mmu_idx = get_mmuidx(oi);
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    target_ulong tlb_addr = env->tlb_table[mmu_idx][index].addr_code;
    unsigned a_bits = get_alignment_bits(get_memop(oi));
    uintptr_t haddr;
    uint8_t res;

    if (addr & ((1 << a_bits) - 1)) {
        cpu_unaligned_access(ENV_GET_CPU(env), addr, MMU_INST_FETCH,
                             mmu_idx, retaddr);
    }

    /* If the TLB entry is for a different page, reload and try again.  */
    if ((addr & TARGET_PAGE_MASK)
         != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if (!VICTIM_TLB_HIT(addr_code, addr)) {
            tlb_fill(ENV_GET_CPU(env), addr, 1, MMU_INST_FETCH,
                     mmu_idx, retaddr);
        }
        tlb_addr = env->tlb_table[mmu_idx][index].addr_code;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        if ((addr & (1 - 1)) != 0) {
            goto do_unaligned_access;
        }

        /* ??? Note that the io helpers always read data in the target
           byte ordering.  We should push the LE/BE request down into io.  */
        res = io_readb(env, mmu_idx, index, addr, retaddr);
        res = (res);
        return res;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (1 > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + 1 - 1
                    >= TARGET_PAGE_SIZE)) {
        target_ulong addr1, addr2;
        uint8_t res1, res2;
        unsigned shift;
    do_unaligned_access:
        addr1 = addr & ~(1 - 1);
        addr2 = addr1 + 1;
        res1 = helper_ret_ldb_cmmu(env, addr1, oi, retaddr);
        res2 = helper_ret_ldb_cmmu(env, addr2, oi, retaddr);
        shift = (addr & (1 - 1)) * 8;

        /* Little-endian combine.  */
        res = (res1 >> shift) | (res2 << ((1 * 8) - shift));
        return res;
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;

    res = ldub_p((uint8_t *)haddr);



    return res;
}
