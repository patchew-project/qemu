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


static int get_allocation_tag(CPUARMState *env, uint64_t ptr, uintptr_t ra)
{
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
