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


/*
 * Load Helpers
 *
 * We support two different access types. SOFTMMU_CODE_ACCESS is
 * specifically for reading instructions from system memory. It is
 * called by the translation loop and in some helpers where the code
 * is disassembled. It shouldn't be called directly by guest code.
 */

static inline uint8_t io_readb(CPUArchState *env, size_t mmu_idx, size_t index,
                               target_ulong addr, uintptr_t retaddr)
{
    CPUIOTLBEntry *iotlbentry = &env->iotlb[mmu_idx][index];
    return io_readx(env, iotlbentry, mmu_idx, addr, retaddr, 1);
}

static inline uint16_t io_readw(CPUArchState *env, size_t mmu_idx, size_t index,
                                target_ulong addr, uintptr_t retaddr)
{
    CPUIOTLBEntry *iotlbentry = &env->iotlb[mmu_idx][index];
    return io_readx(env, iotlbentry, mmu_idx, addr, retaddr, 2);
}

static tcg_target_ulong load_helper(CPUArchState *env, target_ulong addr,
                                    size_t size, bool big_endian,
                                    bool code_read, TCGMemOpIdx oi,
                                    uintptr_t retaddr)
{
    unsigned mmu_idx = get_mmuidx(oi);
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    target_ulong tlb_addr;
    unsigned a_bits = get_alignment_bits(get_memop(oi));
    uintptr_t haddr;
    tcg_target_ulong res;

    if (code_read) {
        tlb_addr = env->tlb_table[mmu_idx][index].addr_code;
    } else {
        tlb_addr = env->tlb_table[mmu_idx][index].addr_read;
    }

    /* Handle unaligned */
    if (addr & ((1 << a_bits) - 1)) {
        if (code_read) {
            cpu_unaligned_access(ENV_GET_CPU(env), addr, MMU_INST_FETCH,
                                 mmu_idx, retaddr);
        } else {
            cpu_unaligned_access(ENV_GET_CPU(env), addr, MMU_DATA_LOAD,
                                 mmu_idx, retaddr);
        }
    }

    /* If the TLB entry is for a different page, reload and try again.  */
    if ((addr & TARGET_PAGE_MASK)
         != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if (code_read) {
            if (!VICTIM_TLB_HIT(addr_code, addr)) {
                tlb_fill(ENV_GET_CPU(env), addr, size, MMU_INST_FETCH,
                         mmu_idx, retaddr);
            }
            tlb_addr = env->tlb_table[mmu_idx][index].addr_code;
        } else {
            if (!VICTIM_TLB_HIT(addr_read, addr)) {
                tlb_fill(ENV_GET_CPU(env), addr, size, MMU_DATA_LOAD,
                         mmu_idx, retaddr);
            }
            tlb_addr = env->tlb_table[mmu_idx][index].addr_read;
        }
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        if ((addr & (size - 1)) != 0) {
            goto do_unaligned_access;
        }

        /* ??? Note that the io helpers always read data in the target
           byte ordering.  We should push the LE/BE request down into io.  */
        switch (size) {
        case 1:
        {
            uint8_t rv = io_readb(env, mmu_idx, index, addr, retaddr);
            res = rv;
            break;
        }
        case 2:
        {
            uint16_t rv = io_readw(env, mmu_idx, index, addr, retaddr);
            if (big_endian) {
                res = bswap16(rv);
            } else {
                res = rv;
            }
            break;
        }
        default:
            g_assert_not_reached();
            break;
        }

        return res;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (size > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + size - 1
                    >= TARGET_PAGE_SIZE)) {
        target_ulong addr1, addr2;
        tcg_target_ulong res1, res2;
        unsigned shift;
    do_unaligned_access:
        addr1 = addr & ~(size - 1);
        addr2 = addr1 + size;
        res1 = load_helper(env, addr1, size, big_endian, code_read, oi, retaddr);
        res2 = load_helper(env, addr2, size, big_endian, code_read, oi, retaddr);
        shift = (addr & (size - 1)) * 8;

        if (big_endian) {
            /* Big-endian combine.  */
            res = (res1 << shift) | (res2 >> ((size * 8) - shift));
        } else {
            /* Little-endian combine.  */
            res = (res1 >> shift) | (res2 << ((size * 8) - shift));
        }
        return res;
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;

    switch (size) {
    case 1:
        res = ldub_p((uint8_t *)haddr);
        break;
    case 2:
        if (big_endian) {
            res = lduw_be_p((uint8_t *)haddr);
        } else {
            res = lduw_le_p((uint8_t *)haddr);
        }
        break;
    default:
        g_assert_not_reached();
        break;
    }

    return res;
}

/*
 * For the benefit of TCG generated code, we want to avoid the
 * complication of ABI-specific return type promotion and always
 * return a value extended to the register size of the host. This is
 * tcg_target_long, except in the case of a 32-bit host and 64-bit
 * data, and for that we always have uint64_t.
 *
 * We don't bother with this widened value for SOFTMMU_CODE_ACCESS.
 */

tcg_target_ulong __attribute__((flatten)) helper_ret_ldub_mmu(CPUArchState *env,
                                                              target_ulong addr,
                                                              TCGMemOpIdx oi,
                                                              uintptr_t retaddr)
{
    return load_helper(env, addr, 1, false, false, oi, retaddr);
}



tcg_target_ulong __attribute__((flatten)) helper_le_lduw_mmu(CPUArchState *env,
                                                             target_ulong addr,
                                                             TCGMemOpIdx oi,
                                                             uintptr_t retaddr)
{
    return load_helper(env, addr, 2, false, false, oi, retaddr);
}


tcg_target_ulong __attribute__((flatten)) helper_be_lduw_mmu(CPUArchState *env,
                                                             target_ulong addr,
                                                             TCGMemOpIdx oi,
                                                             uintptr_t retaddr)
{
    return load_helper(env, addr, 2, true, false, oi, retaddr);
}

uint8_t __attribute__((flatten)) helper_ret_ldb_cmmu (CPUArchState *env,
                                                      target_ulong addr,
                                                      TCGMemOpIdx oi,
                                                      uintptr_t retaddr)
{
    return load_helper(env, addr, 1, false, true, oi, retaddr);
}

uint16_t __attribute__((flatten)) helper_le_ldw_cmmu(CPUArchState *env,
                                                     target_ulong addr,
                                                     TCGMemOpIdx oi,
                                                     uintptr_t retaddr)
{
    return load_helper(env, addr, 2, false, true, oi, retaddr);
}

uint16_t __attribute__((flatten)) helper_be_ldw_cmmu(CPUArchState *env,
                                                     target_ulong addr,
                                                     TCGMemOpIdx oi,
                                                     uintptr_t retaddr)
{
    return load_helper(env, addr, 2, true, true, oi, retaddr);
}

/* Provide signed versions of the load routines as well.  We can of course
   avoid this for 64-bit data, or for 32-bit data on 32-bit host.  */

tcg_target_ulong __attribute__((flatten)) helper_le_ldsw_mmu(CPUArchState *env,
                                                             target_ulong addr,
                                                             TCGMemOpIdx oi,
                                                             uintptr_t retaddr)
{
    return (int16_t)helper_le_lduw_mmu(env, addr, oi, retaddr);
}


tcg_target_ulong __attribute__((flatten)) helper_be_ldsw_mmu(CPUArchState *env,
                                                             target_ulong addr,
                                                             TCGMemOpIdx oi,
                                                             uintptr_t retaddr)
{
    return (int16_t)helper_be_lduw_mmu(env, addr, oi, retaddr);
}

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



/* Provide signed versions of the load routines as well.  We can of course
   avoid this for 64-bit data, or for 32-bit data on 32-bit host.  */
static inline void io_writew(CPUArchState *env,
                                          size_t mmu_idx, size_t index,
                                          uint16_t val,
                                          target_ulong addr,
                                          uintptr_t retaddr)
{
    CPUIOTLBEntry *iotlbentry = &env->iotlb[mmu_idx][index];
    return io_writex(env, iotlbentry, mmu_idx, val, addr, retaddr, 2);
}

void helper_le_stw_mmu(CPUArchState *env, target_ulong addr, uint16_t val,
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
            tlb_fill(ENV_GET_CPU(env), addr, 2, MMU_DATA_STORE,
                     mmu_idx, retaddr);
        }
        tlb_addr = env->tlb_table[mmu_idx][index].addr_write & ~TLB_INVALID_MASK;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        if ((addr & (2 - 1)) != 0) {
            goto do_unaligned_access;
        }

        /* ??? Note that the io helpers always read data in the target
           byte ordering.  We should push the LE/BE request down into io.  */
        val = (val);
        io_writew(env, mmu_idx, index, val, addr, retaddr);
        return;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (2 > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + 2 - 1
                     >= TARGET_PAGE_SIZE)) {
        int i, index2;
        target_ulong page2, tlb_addr2;
    do_unaligned_access:
        /* Ensure the second page is in the TLB.  Note that the first page
           is already guaranteed to be filled, and that the second page
           cannot evict the first.  */
        page2 = (addr + 2) & TARGET_PAGE_MASK;
        index2 = (page2 >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        tlb_addr2 = env->tlb_table[mmu_idx][index2].addr_write;
        if (page2 != (tlb_addr2 & (TARGET_PAGE_MASK | TLB_INVALID_MASK))
            && !VICTIM_TLB_HIT(addr_write, page2)) {
            tlb_fill(ENV_GET_CPU(env), page2, 2, MMU_DATA_STORE,
                     mmu_idx, retaddr);
        }

        /* XXX: not efficient, but simple.  */
        /* This loop must go in the forward direction to avoid issues
           with self-modifying code in Windows 64-bit.  */
        for (i = 0; i < 2; ++i) {
            /* Little-endian extract.  */
            uint8_t val8 = val >> (i * 8);
            helper_ret_stb_mmu(env, addr + i, val8,
                                            oi, retaddr);
        }
        return;
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;



    stw_le_p((uint8_t *)haddr, val);

}


void helper_be_stw_mmu(CPUArchState *env, target_ulong addr, uint16_t val,
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
            tlb_fill(ENV_GET_CPU(env), addr, 2, MMU_DATA_STORE,
                     mmu_idx, retaddr);
        }
        tlb_addr = env->tlb_table[mmu_idx][index].addr_write & ~TLB_INVALID_MASK;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        if ((addr & (2 - 1)) != 0) {
            goto do_unaligned_access;
        }

        /* ??? Note that the io helpers always read data in the target
           byte ordering.  We should push the LE/BE request down into io.  */
        val = bswap16(val);
        io_writew(env, mmu_idx, index, val, addr, retaddr);
        return;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (2 > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + 2 - 1
                     >= TARGET_PAGE_SIZE)) {
        int i, index2;
        target_ulong page2, tlb_addr2;
    do_unaligned_access:
        /* Ensure the second page is in the TLB.  Note that the first page
           is already guaranteed to be filled, and that the second page
           cannot evict the first.  */
        page2 = (addr + 2) & TARGET_PAGE_MASK;
        index2 = (page2 >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        tlb_addr2 = env->tlb_table[mmu_idx][index2].addr_write;
        if (page2 != (tlb_addr2 & (TARGET_PAGE_MASK | TLB_INVALID_MASK))
            && !VICTIM_TLB_HIT(addr_write, page2)) {
            tlb_fill(ENV_GET_CPU(env), page2, 2, MMU_DATA_STORE,
                     mmu_idx, retaddr);
        }

        /* XXX: not efficient, but simple */
        /* This loop must go in the forward direction to avoid issues
           with self-modifying code.  */
        for (i = 0; i < 2; ++i) {
            /* Big-endian extract.  */
            uint8_t val8 = val >> (((2 - 1) * 8) - (i * 8));
            helper_ret_stb_mmu(env, addr + i, val8,
                                            oi, retaddr);
        }
        return;
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;
    stw_be_p((uint8_t *)haddr, val);
}
