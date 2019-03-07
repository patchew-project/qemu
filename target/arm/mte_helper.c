/*
 * ARM v8.5-MemTag Operations
 *
 * Copyright (c) 2019 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"


static uint8_t *allocation_tag_mem(CPUARMState *env, uint64_t ptr,
                                   bool write, uintptr_t ra)
{
    /* Tag storage not implemented.  */
    return NULL;
}

static int get_allocation_tag(CPUARMState *env, uint64_t ptr, uintptr_t ra)
{
    uint8_t *mem = allocation_tag_mem(env, ptr, false, ra);

    if (mem) {
        int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
        return extract32(atomic_read(mem), ofs, 4);
    }
    /* Tag storage not implemented.  */
    return -1;
}

static int allocation_tag_from_addr(uint64_t ptr)
{
    ptr += 1ULL << 55;  /* carry ptr[55] into ptr[59:56].  */
    return extract64(ptr, 56, 4);
}

static int choose_nonexcluded_tag(int tag, int offset, uint16_t exclude)
{
    if (exclude == 0xffff) {
        return 0;
    }
    if (offset == 0) {
        while (exclude & (1 << tag)) {
            tag = (tag + 1) & 15;
        }
    } else {
        do {
            do {
                tag = (tag + 1) & 15;
            } while (exclude & (1 << tag));
        } while (--offset > 0);
    }
    return tag;
}

static uint64_t address_with_allocation_tag(uint64_t ptr, int rtag)
{
    rtag -= extract64(ptr, 55, 1);
    return deposit64(ptr, 56, 4, rtag);
}

/*
 * Perform a checked access for MTE.
 * On arrival, TBI is known to enabled, as is allocation_tag_access_enabled.
 */
static uint64_t do_mte_check(CPUARMState *env, uint64_t dirty_ptr,
                             uint64_t clean_ptr, uint32_t select,
                             uintptr_t ra)
{
    int ptr_tag, mem_tag;

    /*
     * If TCMA is enabled, then physical tag 0 is unchecked.
     * Note the rules R0076 & R0077 are written with logical tags,
     * and we need the physical tag below anyway.
     */
    ptr_tag = allocation_tag_from_addr(dirty_ptr);
    if (ptr_tag == 0) {
        ARMMMUIdx stage1 = arm_stage1_mmu_idx(env);
        ARMVAParameters p = aa64_va_parameters(env, dirty_ptr, stage1, true);
        if (p.tcma) {
            return clean_ptr;
        }
    }

    /*
     * If an access is made to an address that does not provide tag storage,
     * the result is implementation defined (R0006).  We choose to treat the
     * access as unchecked.
     * This is similar to MemAttr != Tagged, which are also unchecked.
     */
    mem_tag = get_allocation_tag(env, clean_ptr, ra);
    if (mem_tag < 0) {
        return clean_ptr;
    }

    /* If the tags do not match, the tag check operation fails.  */
    if (unlikely(ptr_tag != mem_tag)) {
        int tcf, el = arm_current_el(env);

        if (el == 0) {
            /* FIXME: ARMv8.1-VHE S2 translation regime.  */
            tcf = extract64(env->cp15.sctlr_el[1], 38, 2);
        } else {
            tcf = extract64(env->cp15.sctlr_el[el], 40, 2);
        }
        if (tcf == 1) {
            /*
             * Tag check fail causes a synchronous exception.
             *
             * In restore_state_to_opc, we set the exception syndrome
             * for the load or store operation.  Do that first so we
             * may overwrite that with the syndrome for the tag check.
             */
            cpu_restore_state(ENV_GET_CPU(env), ra, true);
            env->exception.vaddress = dirty_ptr;
            raise_exception(env, EXCP_DATA_ABORT,
                            syn_data_abort_no_iss(el != 0, 0, 0, 0, 0, 0x11),
                            exception_target_el(env));
        } else if (tcf == 2) {
            /* Tag check fail causes asynchronous flag set.  */
            env->cp15.tfsr_el[el] |= 1 << select;
        }
    }

    return clean_ptr;
}

/*
 * Perform check in translation regime w/single IA range.
 * It is known that TBI is enabled on entry.
 */
uint64_t HELPER(mte_check1)(CPUARMState *env, uint64_t dirty_ptr)
{
    uint64_t clean_ptr = extract64(dirty_ptr, 0, 56);
    return do_mte_check(env, dirty_ptr, clean_ptr, 0, GETPC());
}

/*
 * Perform check in translation regime w/two IA ranges.
 * The TBI argument is the concatenation of TBI1:TBI0.  We have filtered
 * TBI==0, but still need to check the IA range being referenced.
 */
uint64_t HELPER(mte_check2)(CPUARMState *env, uint64_t dirty_ptr, uint32_t tbi)
{
    uint32_t select = extract64(dirty_ptr, 55, 1);

    if ((tbi >> select) & 1) {
        uint64_t clean_ptr = sextract64(dirty_ptr, 0, 56);
        return do_mte_check(env, dirty_ptr, clean_ptr, select, GETPC());
    } else {
        /* TBI is disabled; the access is unchecked.  */
        return dirty_ptr;
    }
}

uint64_t HELPER(irg)(CPUARMState *env, uint64_t rn, uint64_t rm)
{
    int el = arm_current_el(env);
    uint64_t sctlr = arm_sctlr(env, el);
    int rtag = 0;

    if (allocation_tag_access_enabled(env, el, sctlr)) {
        /*
         * Our IMPDEF choice for GCR_EL1.RRND==1 is to behave as if
         * GCR_EL1.RRND==0, always producing deterministic results.
         */
        uint16_t exclude = extract32(rm | env->cp15.gcr_el1, 0, 16);
        int start = extract32(env->cp15.rgsr_el1, 0, 4);
        int seed = extract32(env->cp15.rgsr_el1, 8, 16);
        int offset, i;

        /* RandomTag */
        for (i = offset = 0; i < 4; ++i) {
            /* NextRandomTagBit */
            int top = (extract32(seed, 5, 1) ^ extract32(seed, 3, 1) ^
                       extract32(seed, 2, 1) ^ extract32(seed, 0, 1));
            seed = (top << 15) | (seed >> 1);
            offset |= top << i;
        }
        rtag = choose_nonexcluded_tag(start, offset, exclude);

        env->cp15.rgsr_el1 = rtag | (seed << 8);
    }

    return address_with_allocation_tag(rn, rtag);
}

uint64_t HELPER(addg)(CPUARMState *env, uint64_t ptr,
                      uint32_t offset, uint32_t tag_offset)
{
    int el = arm_current_el(env);
    uint64_t sctlr = arm_sctlr(env, el);
    int rtag = 0;

    if (allocation_tag_access_enabled(env, el, sctlr)) {
        int start_tag = allocation_tag_from_addr(ptr);
        uint16_t exclude = extract32(env->cp15.gcr_el1, 0, 16);
        rtag = choose_nonexcluded_tag(start_tag, tag_offset, exclude);
    }

    return address_with_allocation_tag(ptr + offset, rtag);
}

uint64_t HELPER(subg)(CPUARMState *env, uint64_t ptr,
                      uint32_t offset, uint32_t tag_offset)
{
    int el = arm_current_el(env);
    uint64_t sctlr = arm_sctlr(env, el);
    int rtag = 0;

    if (allocation_tag_access_enabled(env, el, sctlr)) {
        int start_tag = allocation_tag_from_addr(ptr);
        uint16_t exclude = extract32(env->cp15.gcr_el1, 0, 16);
        rtag = choose_nonexcluded_tag(start_tag, tag_offset, exclude);
    }

    return address_with_allocation_tag(ptr - offset, rtag);
}

uint64_t HELPER(gmi)(uint64_t ptr, uint64_t mask)
{
    int tag = allocation_tag_from_addr(ptr);
    return mask | (1ULL << tag);
}

uint64_t HELPER(ldg)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    int el;
    uint64_t sctlr;
    int rtag;

    /* Trap if accessing an invalid page.  */
    rtag = get_allocation_tag(env, ptr, GETPC());

    /*
     * The tag is squashed to zero if the page does not support tags,
     * or if the OS is denying access to the tags.
     */
    el = arm_current_el(env);
    sctlr = arm_sctlr(env, el);
    if (rtag < 0 || !allocation_tag_access_enabled(env, el, sctlr)) {
        rtag = 0;
    }

    return address_with_allocation_tag(xt, rtag);
}

static void check_tag_aligned(CPUARMState *env, uint64_t ptr, uintptr_t ra)
{
    if (unlikely(!QEMU_IS_ALIGNED(ptr, TAG_GRANULE))) {
        arm_cpu_do_unaligned_access(ENV_GET_CPU(env), ptr, MMU_DATA_STORE,
                                    cpu_mmu_index(env, false), ra);
        g_assert_not_reached();
    }
}

/* For use in a non-parallel context, store to the given nibble.  */
static void store_tag1(uint64_t ptr, uint8_t *mem, int tag)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    uint8_t old = atomic_read(mem);
    uint8_t new = deposit32(old, ofs, 4, tag);

    atomic_set(mem, new);
}

/* For use in a parallel context, atomically store to the given nibble.  */
static void store_tag1_parallel(uint64_t ptr, uint8_t *mem, int tag)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    uint8_t old = atomic_read(mem);

    while (1) {
        uint8_t new = deposit32(old, ofs, 4, tag);
        uint8_t cmp = atomic_cmpxchg(mem, old, new);
        if (likely(cmp == old)) {
            return;
        }
        old = cmp;
    }
}

typedef void stg_store1(uint64_t, uint8_t *, int);

static void do_stg(CPUARMState *env, uint64_t ptr, uint64_t xt,
                   uintptr_t ra, stg_store1 store1)
{
    int el;
    uint64_t sctlr;
    uint8_t *mem;

    check_tag_aligned(env, ptr, ra);

    /* Trap if accessing an invalid page.  */
    mem = allocation_tag_mem(env, ptr, true, ra);

    /* Store if page supports tags and access is enabled.  */
    el = arm_current_el(env);
    sctlr = arm_sctlr(env, el);
    if (mem && allocation_tag_access_enabled(env, el, sctlr)) {
        store1(ptr, mem, allocation_tag_from_addr(xt));
    }
}

void HELPER(stg)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_stg(env, ptr, xt, GETPC(), store_tag1);
}

void HELPER(stg_parallel)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_stg(env, ptr, xt, GETPC(), store_tag1_parallel);
}

static void do_st2g(CPUARMState *env, uint64_t ptr1, uint64_t xt,
                    uintptr_t ra, stg_store1 store1)
{
    int el;
    uint64_t ptr2, sctlr;
    uint8_t *mem1, *mem2;

    check_tag_aligned(env, ptr1, ra);
    ptr2 = ptr1 + TAG_GRANULE;

    /* Trap if accessing an invalid page(s).  */
    mem1 = mem2 = allocation_tag_mem(env, ptr1, true, ra);
    if (unlikely((ptr1 ^ ptr2) & TARGET_PAGE_MASK)) {
        /* The two stores are across two pages.  */
        mem2 = allocation_tag_mem(env, ptr2, true, ra);
    }

    /* Store if page supports tags and access is enabled.  */
    el = arm_current_el(env);
    sctlr = arm_sctlr(env, el);
    if ((mem1 || mem2) && allocation_tag_access_enabled(env, el, sctlr)) {
        int tag = allocation_tag_from_addr(xt);

        if (likely(mem1 == mem2)) {
            /* The two stores are aligned 32, and modify one byte.  */
            tag |= tag << 4;
            atomic_set(mem1, tag);
        } else {
            /* The two stores are unaligned and modify two bytes.  */
            if (mem1) {
                store1(ptr1, mem1, tag);
            }
            if (mem2) {
                store1(ptr2, mem2, tag);
            }
        }
    }
}

void HELPER(st2g)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_st2g(env, ptr, xt, GETPC(), store_tag1);
}

void HELPER(st2g_parallel)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_st2g(env, ptr, xt, GETPC(), store_tag1_parallel);
}

uint64_t HELPER(ldgm)(CPUARMState *env, uint64_t ptr)
{
    const int size = 4 << GMID_EL1_BS;
    int el;
    uint64_t sctlr;
    void *mem;

    ptr = QEMU_ALIGN_DOWN(ptr, size);

    /* Trap if accessing an invalid page(s).  */
    mem = allocation_tag_mem(env, ptr, false, GETPC());

    /*
     * The tag is squashed to zero if the page does not support tags,
     * or if the OS is denying access to the tags.
     */
    el = arm_current_el(env);
    sctlr = arm_sctlr(env, el);
    if (!mem || !allocation_tag_access_enabled(env, el, sctlr)) {
        return 0;
    }

#if GMID_EL1_BS != 6
# error "Fill in the blanks for other sizes"
#endif
    /*
     * We are loading 64-bits worth of tags.  The ordering of elements
     * within the word corresponds to a 64-bit little-endian operation.
     */
    return ldq_le_p(mem);
}

static uint64_t do_stgm(CPUARMState *env, uint64_t ptr,
                        uint64_t val, uintptr_t ra)
{
    const int size = 4 << GMID_EL1_BS;
    int el;
    uint64_t sctlr;
    void *mem;

    ptr = QEMU_ALIGN_DOWN(ptr, size);

    /* Trap if accessing an invalid page(s).  */
    mem = allocation_tag_mem(env, ptr, true, ra);

    /*
     * No action if the page does not support tags,
     * or if the OS is denying access to the tags.
     */
    el = arm_current_el(env);
    sctlr = arm_sctlr(env, el);
    if (!mem || !allocation_tag_access_enabled(env, el, sctlr)) {
        return ptr;
    }

#if GMID_EL1_BS != 6
# error "Fill in the blanks for other sizes"
#endif
    /*
     * We are storing 64-bits worth of tags.  The ordering of elements
     * within the word corresponds to a 64-bit little-endian operation.
     */
    stq_le_p(mem, val);

    return ptr;
}

void HELPER(stgm)(CPUARMState *env, uint64_t ptr, uint64_t val)
{
    do_stgm(env, ptr, val, GETPC());
}

void HELPER(stzgm)(CPUARMState *env, uint64_t ptr, uint64_t val)
{
    int i, mmu_idx, size = 4 << GMID_EL1_BS;
    uintptr_t ra = GETPC();
    void *mem;

    ptr = do_stgm(env, ptr, val, ra);

    /*
     * We will have just probed this virtual address in do_stgm.
     * If the tlb_vaddr_to_host fails, then the memory is not ram,
     * or is monitored in some other way.  Fall back to stores.
     */
    mmu_idx = cpu_mmu_index(env, false);
    mem = tlb_vaddr_to_host(env, ptr, MMU_DATA_STORE, mmu_idx);
    if (mem) {
        memset(mem, 0, size);
    } else {
        for (i = 0; i < size; i += 8) {
            cpu_stq_data_ra(env, ptr + i, 0, ra);
        }
    }
}
