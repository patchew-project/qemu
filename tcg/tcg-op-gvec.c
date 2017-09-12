/*
 *  Generic vector operation expansion
 *
 *  Copyright (c) 2017 Linaro
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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "tcg.h"
#include "tcg-op.h"
#include "tcg-op-gvec.h"
#include "tcg-gvec-desc.h"


/* Create a descriptor from components.  */
uint32_t simd_desc(uint32_t oprsz, uint32_t maxsz, int32_t data)
{
    uint32_t desc = 0;

    assert(oprsz % 8 == 0 && oprsz <= (8 << SIMD_OPRSZ_BITS));
    assert(maxsz % 8 == 0 && maxsz <= (8 << SIMD_MAXSZ_BITS));
    assert(data == sextract32(data, 0, SIMD_DATA_BITS));

    oprsz = (oprsz / 8) - 1;
    maxsz = (maxsz / 8) - 1;
    desc = deposit32(desc, SIMD_OPRSZ_SHIFT, SIMD_OPRSZ_BITS, oprsz);
    desc = deposit32(desc, SIMD_MAXSZ_SHIFT, SIMD_MAXSZ_BITS, maxsz);
    desc = deposit32(desc, SIMD_DATA_SHIFT, SIMD_DATA_BITS, data);

    return desc;
}

/* Generate a call to a gvec-style helper with three vector operands.  */
void tcg_gen_gvec_3_ool(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                        uint32_t oprsz, uint32_t maxsz, uint32_t data,
                        gen_helper_gvec_3 *fn)
{
    TCGv_ptr a0, a1, a2;
    TCGv_i32 desc = tcg_const_i32(simd_desc(oprsz, maxsz, data));

    a0 = tcg_temp_new_ptr();
    a1 = tcg_temp_new_ptr();
    a2 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(a0, tcg_ctx.tcg_env, dofs);
    tcg_gen_addi_ptr(a1, tcg_ctx.tcg_env, aofs);
    tcg_gen_addi_ptr(a2, tcg_ctx.tcg_env, bofs);

    fn(a0, a1, a2, desc);

    tcg_temp_free_ptr(a0);
    tcg_temp_free_ptr(a1);
    tcg_temp_free_ptr(a2);
    tcg_temp_free_i32(desc);
}

/* Generate a call to a gvec-style helper with three vector operands
   and an extra pointer operand.  */
void tcg_gen_gvec_3_ptr(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                        TCGv_ptr ptr, uint32_t oprsz, uint32_t maxsz,
                        uint32_t data, gen_helper_gvec_3_ptr *fn)
{
    TCGv_ptr a0, a1, a2;
    TCGv_i32 desc = tcg_const_i32(simd_desc(oprsz, maxsz, data));

    a0 = tcg_temp_new_ptr();
    a1 = tcg_temp_new_ptr();
    a2 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(a0, tcg_ctx.tcg_env, dofs);
    tcg_gen_addi_ptr(a1, tcg_ctx.tcg_env, aofs);
    tcg_gen_addi_ptr(a2, tcg_ctx.tcg_env, bofs);

    fn(a0, a1, a2, ptr, desc);

    tcg_temp_free_ptr(a0);
    tcg_temp_free_ptr(a1);
    tcg_temp_free_ptr(a2);
    tcg_temp_free_i32(desc);
}
