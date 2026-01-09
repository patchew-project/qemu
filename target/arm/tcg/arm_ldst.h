/*
 * ARM load/store instructions for code (armeb-user support)
 *
 *  Copyright (c) 2012 CodeSourcery, LLC
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

#ifndef ARM_LDST_H
#define ARM_LDST_H

#include "exec/translator.h"
#include "target/arm/internals.h"
#include "qemu/bswap.h"

static inline MemOp arm_memop_endian_swap(bool sctlr_b)
{
    MemOp t = target_big_endian() ? MO_BE : MO_LE;
    bool do_swap = bswap_code(sctlr_b);
    return t ^ (do_swap * MO_BSWAP);
}

/* Load an instruction and return it in the standard little-endian order */
static inline uint32_t arm_ldl_code(CPUARMState *env, DisasContextBase *s,
                                    uint64_t addr, bool sctlr_b)
{
    return translator_ldl_end(env, s, addr, arm_memop_endian_swap(sctlr_b));
}

/* Ditto, for a halfword (Thumb) instruction */
static inline uint16_t arm_lduw_code(CPUARMState *env, DisasContextBase* s,
                                     uint64_t addr, bool sctlr_b)
{
#ifndef CONFIG_USER_ONLY
    /* In big-endian (BE32) mode, adjacent Thumb instructions have been swapped
       within each word.  Undo that now.  */
    if (sctlr_b) {
        addr ^= 2;
    }
#endif
    return translator_lduw_end(env, s, addr, arm_memop_endian_swap(sctlr_b));
}

#endif
