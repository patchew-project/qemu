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

#ifdef TARGET_WORDS_BIGENDIAN
#define NEED_BE_BSWAP 0
#define NEED_LE_BSWAP 1
#else
#define NEED_BE_BSWAP 1
#define NEED_LE_BSWAP 0
#endif

/*
 * Byte Swap Helper
 *
 * This should all dead code away depending on the build host and
 * access type.
 */

static inline uint64_t handle_bswap(uint64_t val, int size, bool big_endian)
{
    if ((big_endian && NEED_BE_BSWAP) || (!big_endian && NEED_LE_BSWAP)) {
        switch (size) {
        case 1: return val;
        case 2: return bswap16(val);
        case 4: return bswap32(val);
        case 8: return bswap64(val);
        default:
            g_assert_not_reached();
        }
    } else {
        return val;
    }
}

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
        CPUIOTLBEntry *iotlbentry = &env->iotlb[mmu_idx][index];
        uint64_t tmp;

        if ((addr & (size - 1)) != 0) {
            goto do_unaligned_access;
        }

        tmp = io_readx(env, iotlbentry, mmu_idx, addr, retaddr, size);
        return handle_bswap(tmp, size, big_endian);
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
    case 4:
        if (big_endian) {
            res = ldl_be_p((uint8_t *)haddr);
        } else {
            res = ldl_le_p((uint8_t *)haddr);
        }
        break;
    case 8:
        if (big_endian) {
            res = ldq_be_p((uint8_t *)haddr);
        } else {
            res = ldq_le_p((uint8_t *)haddr);
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

tcg_target_ulong __attribute__((flatten)) helper_le_ldul_mmu(CPUArchState *env,
                                                             target_ulong addr,
                                                             TCGMemOpIdx oi,
                                                             uintptr_t retaddr)
{
    return load_helper(env, addr, 4, false, false, oi, retaddr);
}

tcg_target_ulong __attribute__((flatten)) helper_be_ldul_mmu(CPUArchState *env,
                                                             target_ulong addr,
                                                             TCGMemOpIdx oi,
                                                             uintptr_t retaddr)
{
    return load_helper(env, addr, 4, true, false, oi, retaddr);
}

tcg_target_ulong __attribute__((flatten)) helper_le_ldq_mmu(CPUArchState *env,
                                                            target_ulong addr,
                                                            TCGMemOpIdx oi,
                                                            uintptr_t retaddr)
{
    return load_helper(env, addr, 8, false, false, oi, retaddr);
}

tcg_target_ulong __attribute__((flatten)) helper_be_ldq_mmu(CPUArchState *env,
                                                             target_ulong addr,
                                                             TCGMemOpIdx oi,
                                                             uintptr_t retaddr)
{
    return load_helper(env, addr, 8, true, false, oi, retaddr);
}

/*
 * Code Access
 */

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

uint32_t __attribute__((flatten)) helper_le_ldl_cmmu(CPUArchState *env,
                                                     target_ulong addr,
                                                     TCGMemOpIdx oi,
                                                     uintptr_t retaddr)
{
    return load_helper(env, addr, 4, false, true, oi, retaddr);
}

uint32_t __attribute__((flatten)) helper_be_ldl_cmmu(CPUArchState *env,
                                                     target_ulong addr,
                                                     TCGMemOpIdx oi,
                                                     uintptr_t retaddr)
{
    return load_helper(env, addr, 4, true, true, oi, retaddr);
}

uint64_t __attribute__((flatten)) helper_le_ldq_cmmu(CPUArchState *env,
                                                     target_ulong addr,
                                                     TCGMemOpIdx oi,
                                                     uintptr_t retaddr)
{
    return load_helper(env, addr, 8, false, true, oi, retaddr);
}

uint64_t __attribute__((flatten)) helper_be_ldq_cmmu(CPUArchState *env,
                                                     target_ulong addr,
                                                     TCGMemOpIdx oi,
                                                     uintptr_t retaddr)
{
    return load_helper(env, addr, 8, true, true, oi, retaddr);
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

/*
 * Store Helpers
 */

static void store_helper(CPUArchState *env, target_ulong addr, uint64_t val,
                         size_t size, bool big_endian, TCGMemOpIdx oi,
                         uintptr_t retaddr)
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
            tlb_fill(ENV_GET_CPU(env), addr, size, MMU_DATA_STORE,
                     mmu_idx, retaddr);
        }
        tlb_addr = env->tlb_table[mmu_idx][index].addr_write & ~TLB_INVALID_MASK;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        CPUIOTLBEntry *iotlbentry = &env->iotlb[mmu_idx][index];

        if ((addr & (size - 1)) != 0) {
            goto do_unaligned_access;
        }

        io_writex(env, iotlbentry, mmu_idx,
                  handle_bswap(val, size, big_endian),
                  addr, retaddr, size);
        return;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (size > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + size - 1
                     >= TARGET_PAGE_SIZE)) {
        int i, index2;
        target_ulong page2, tlb_addr2;
    do_unaligned_access:
        /* Ensure the second page is in the TLB.  Note that the first page
           is already guaranteed to be filled, and that the second page
           cannot evict the first.  */
        page2 = (addr + size) & TARGET_PAGE_MASK;
        index2 = (page2 >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        tlb_addr2 = env->tlb_table[mmu_idx][index2].addr_write;
        if (page2 != (tlb_addr2 & (TARGET_PAGE_MASK | TLB_INVALID_MASK))
            && !VICTIM_TLB_HIT(addr_write, page2)) {
            tlb_fill(ENV_GET_CPU(env), page2, size, MMU_DATA_STORE,
                     mmu_idx, retaddr);
        }

        /* XXX: not efficient, but simple.  */
        /* This loop must go in the forward direction to avoid issues
           with self-modifying code in Windows 64-bit.  */
        for (i = 0; i < size; ++i) {
            uint8_t val8;
            if (big_endian) {
                /* Big-endian extract.  */
                val8 = val >> (((size - 1) * 8) - (i * 8));
            } else {
                /* Little-endian extract.  */
                val8 = val >> (i * 8);
            }
            store_helper(env, addr + i, val8, 1, big_endian, oi, retaddr);
        }
        return;
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;

    switch(size) {
    case 1:
        stb_p((uint8_t *)haddr, val);
        break;
    case 2:
        if (big_endian) {
            stw_be_p((uint8_t *)haddr, val);
        } else {
            stw_le_p((uint8_t *)haddr, val);
        }
        break;
    case 4:
        if (big_endian) {
            stl_be_p((uint8_t *)haddr, val);
        } else {
            stl_le_p((uint8_t *)haddr, val);
        }
        break;
    case 8:
        if (big_endian) {
            stq_be_p((uint8_t *)haddr, val);
        } else {
            stq_le_p((uint8_t *)haddr, val);
        }
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

void __attribute__((flatten)) helper_ret_stb_mmu(CPUArchState *env,
                                                 target_ulong addr, uint8_t val,
                                                 TCGMemOpIdx oi,
                                                 uintptr_t retaddr)
{
    store_helper(env, addr, val, 1, false, oi, retaddr);
}


void __attribute__((flatten)) helper_le_stw_mmu(CPUArchState *env,
                                                target_ulong addr, uint16_t val,
                                                TCGMemOpIdx oi,
                                                uintptr_t retaddr)
{
    store_helper(env, addr, val, 2, false, oi, retaddr);
}


void __attribute__((flatten)) helper_be_stw_mmu(CPUArchState *env,
                                                target_ulong addr, uint16_t val,
                                                TCGMemOpIdx oi,
                                                uintptr_t retaddr)
{
    store_helper(env, addr, val, 2, true, oi, retaddr);
}

void __attribute__((flatten)) helper_le_stl_mmu(CPUArchState *env,
                                                target_ulong addr, uint32_t val,
                                                TCGMemOpIdx oi,
                                                uintptr_t retaddr)
{
    store_helper(env, addr, val, 4, false, oi, retaddr);
}


void __attribute__((flatten)) helper_be_stl_mmu(CPUArchState *env,
                                                target_ulong addr, uint32_t val,
                                                TCGMemOpIdx oi,
                                                uintptr_t retaddr)
{
    store_helper(env, addr, val, 4, true, oi, retaddr);
}

void __attribute__((flatten)) helper_le_stq_mmu(CPUArchState *env,
                                                target_ulong addr, uint64_t val,
                                                TCGMemOpIdx oi,
                                                uintptr_t retaddr)
{
    store_helper(env, addr, val, 8, false, oi, retaddr);
}


void __attribute__((flatten)) helper_be_stq_mmu(CPUArchState *env,
                                                target_ulong addr, uint64_t val,
                                                TCGMemOpIdx oi,
                                                uintptr_t retaddr)
{
    store_helper(env, addr, val, 8, true, oi, retaddr);
}
