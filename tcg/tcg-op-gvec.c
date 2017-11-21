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

#define REP8(x)    ((x) * 0x0101010101010101ull)
#define REP16(x)   ((x) * 0x0001000100010001ull)

#define MAX_UNROLL  4

/* Verify vector size and alignment rules.  OFS should be the OR of all
   of the operand offsets so that we can check them all at once.  */
static void check_size_align(uint32_t oprsz, uint32_t maxsz, uint32_t ofs)
{
    uint32_t align = maxsz > 16 || oprsz >= 16 ? 15 : 7;
    tcg_debug_assert(oprsz > 0);
    tcg_debug_assert(oprsz <= maxsz);
    tcg_debug_assert((oprsz & align) == 0);
    tcg_debug_assert((maxsz & align) == 0);
    tcg_debug_assert((ofs & align) == 0);
}

/* Verify vector overlap rules for two operands.  */
static void check_overlap_2(uint32_t d, uint32_t a, uint32_t s)
{
    tcg_debug_assert(d == a || d + s <= a || a + s <= d);
}

/* Verify vector overlap rules for three operands.  */
static void check_overlap_3(uint32_t d, uint32_t a, uint32_t b, uint32_t s)
{
    check_overlap_2(d, a, s);
    check_overlap_2(d, b, s);
    check_overlap_2(a, b, s);
}

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

/* Generate a call to a gvec-style helper with two vector operands.  */
void tcg_gen_gvec_2_ool(uint32_t dofs, uint32_t aofs,
                        uint32_t oprsz, uint32_t maxsz, int32_t data,
                        gen_helper_gvec_2 *fn)
{
    TCGv_ptr a0, a1;
    TCGv_i32 desc = tcg_const_i32(simd_desc(oprsz, maxsz, data));

    a0 = tcg_temp_new_ptr();
    a1 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(a0, cpu_env, dofs);
    tcg_gen_addi_ptr(a1, cpu_env, aofs);

    fn(a0, a1, desc);

    tcg_temp_free_ptr(a0);
    tcg_temp_free_ptr(a1);
    tcg_temp_free_i32(desc);
}

/* Generate a call to a gvec-style helper with three vector operands.  */
void tcg_gen_gvec_3_ool(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                        uint32_t oprsz, uint32_t maxsz, int32_t data,
                        gen_helper_gvec_3 *fn)
{
    TCGv_ptr a0, a1, a2;
    TCGv_i32 desc = tcg_const_i32(simd_desc(oprsz, maxsz, data));

    a0 = tcg_temp_new_ptr();
    a1 = tcg_temp_new_ptr();
    a2 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(a0, cpu_env, dofs);
    tcg_gen_addi_ptr(a1, cpu_env, aofs);
    tcg_gen_addi_ptr(a2, cpu_env, bofs);

    fn(a0, a1, a2, desc);

    tcg_temp_free_ptr(a0);
    tcg_temp_free_ptr(a1);
    tcg_temp_free_ptr(a2);
    tcg_temp_free_i32(desc);
}

/* Generate a call to a gvec-style helper with three vector operands
   and an extra pointer operand.  */
void tcg_gen_gvec_2_ptr(uint32_t dofs, uint32_t aofs,
                        TCGv_ptr ptr, uint32_t oprsz, uint32_t maxsz,
                        int32_t data, gen_helper_gvec_2_ptr *fn)
{
    TCGv_ptr a0, a1;
    TCGv_i32 desc = tcg_const_i32(simd_desc(oprsz, maxsz, data));

    a0 = tcg_temp_new_ptr();
    a1 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(a0, cpu_env, dofs);
    tcg_gen_addi_ptr(a1, cpu_env, aofs);

    fn(a0, a1, ptr, desc);

    tcg_temp_free_ptr(a0);
    tcg_temp_free_ptr(a1);
    tcg_temp_free_i32(desc);
}

/* Generate a call to a gvec-style helper with three vector operands
   and an extra pointer operand.  */
void tcg_gen_gvec_3_ptr(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                        TCGv_ptr ptr, uint32_t oprsz, uint32_t maxsz,
                        int32_t data, gen_helper_gvec_3_ptr *fn)
{
    TCGv_ptr a0, a1, a2;
    TCGv_i32 desc = tcg_const_i32(simd_desc(oprsz, maxsz, data));

    a0 = tcg_temp_new_ptr();
    a1 = tcg_temp_new_ptr();
    a2 = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(a0, cpu_env, dofs);
    tcg_gen_addi_ptr(a1, cpu_env, aofs);
    tcg_gen_addi_ptr(a2, cpu_env, bofs);

    fn(a0, a1, a2, ptr, desc);

    tcg_temp_free_ptr(a0);
    tcg_temp_free_ptr(a1);
    tcg_temp_free_ptr(a2);
    tcg_temp_free_i32(desc);
}

/* Return true if we want to implement something of OPRSZ bytes
   in units of LNSZ.  This limits the expansion of inline code.  */
static inline bool check_size_impl(uint32_t oprsz, uint32_t lnsz)
{
    uint32_t lnct = oprsz / lnsz;
    return lnct >= 1 && lnct <= MAX_UNROLL;
}

static void expand_clr(uint32_t dofs, uint32_t maxsz);

/* Set OPRSZ bytes at DOFS to replications of IN or IN_C.  */
static void do_dup_i32(unsigned vece, uint32_t dofs, uint32_t oprsz,
                       uint32_t maxsz, TCGv_i32 in, uint32_t in_c,
                       void (*ool)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_vec t_vec;
    uint32_t i;

    if (TCG_TARGET_HAS_v256 && check_size_impl(oprsz, 32)) {
        t_vec = tcg_temp_new_vec(TCG_TYPE_V256);
    } else if (TCG_TARGET_HAS_v128 && check_size_impl(oprsz, 16)) {
        t_vec = tcg_temp_new_vec(TCG_TYPE_V128);
    } else if (TCG_TARGET_HAS_v64 && check_size_impl(oprsz, 8)) {
        t_vec = tcg_temp_new_vec(TCG_TYPE_V64);
    } else  {
        TCGv_i32 t_i32 = in ? in : tcg_const_i32(in_c);

        if (check_size_impl(oprsz, 4)) {
            for (i = 0; i < oprsz; i += 4) {
                tcg_gen_st_i32(t_i32, cpu_env, dofs + i);
            }
            if (in == NULL) {
                tcg_temp_free_i32(t_i32);
            }
            goto done;
        } else {
            TCGv_ptr a0 = tcg_temp_new_ptr();
            TCGv_i32 desc = tcg_const_i32(simd_desc(oprsz, maxsz, 0));

            tcg_gen_addi_ptr(a0, cpu_env, dofs);
            ool(a0, desc, t_i32);

            tcg_temp_free_ptr(a0);
            tcg_temp_free_i32(desc);
            if (in == NULL) {
                tcg_temp_free_i32(t_i32);
            }
            return;
        }
    }

    if (in) {
        tcg_gen_dup_i32_vec(vece, t_vec, in);
    } else {
        tcg_gen_dup32i_vec(t_vec, in_c);
    }

    i = 0;
    if (TCG_TARGET_HAS_v256) {
        for (; i + 32 <= oprsz; i += 32) {
            tcg_gen_stl_vec(t_vec, cpu_env, dofs + i, TCG_TYPE_V256);
        }
    }
    if (TCG_TARGET_HAS_v128) {
        for (; i + 16 <= oprsz; i += 16) {
            tcg_gen_stl_vec(t_vec, cpu_env, dofs + i, TCG_TYPE_V128);
        }
    }
    if (TCG_TARGET_HAS_v64) {
        for (; i < oprsz; i += 8) {
            tcg_gen_stl_vec(t_vec, cpu_env, dofs + i, TCG_TYPE_V64);
        }
    }

 done:
    tcg_debug_assert(i == oprsz);
    if (i < maxsz) {
        expand_clr(dofs + i, maxsz - i);
    }
}

/* Likewise, but with 64-bit quantities.  */
static void do_dup_i64(unsigned vece, uint32_t dofs, uint32_t oprsz,
                       uint32_t maxsz, TCGv_i64 in, uint64_t in_c)
{
    TCGv_vec t_vec;
    uint32_t i;

    if (TCG_TARGET_HAS_v256 && check_size_impl(oprsz, 32)) {
        t_vec = tcg_temp_new_vec(TCG_TYPE_V256);
    } else if (TCG_TARGET_HAS_v128 && check_size_impl(oprsz, 16)) {
        t_vec = tcg_temp_new_vec(TCG_TYPE_V128);
    } else if (TCG_TARGET_HAS_v64 && TCG_TARGET_REG_BITS == 32
               && check_size_impl(oprsz, 8)) {
        t_vec = tcg_temp_new_vec(TCG_TYPE_V64);
    } else {
        TCGv_i64 t_i64 = in ? in : tcg_const_i64(in_c);

        if (check_size_impl(oprsz, 8)) {
            for (i = 0; i < oprsz; i += 8) {
                tcg_gen_st_i64(t_i64, cpu_env, dofs + i);
            }
            if (in == NULL) {
                tcg_temp_free_i64(t_i64);
            }
            goto done;
        } else {
            TCGv_ptr a0 = tcg_temp_new_ptr();
            TCGv_i32 desc = tcg_const_i32(simd_desc(oprsz, maxsz, 0));

            tcg_gen_addi_ptr(a0, cpu_env, dofs);
            gen_helper_gvec_dup64(a0, desc, t_i64);

            tcg_temp_free_ptr(a0);
            tcg_temp_free_i32(desc);
            if (in == NULL) {
                tcg_temp_free_i64(t_i64);
            }
            return;
        }
    }

    if (in) {
        tcg_gen_dup_i64_vec(vece, t_vec, in);
    } else {
        tcg_gen_dup64i_vec(t_vec, in_c);
    }

    i = 0;
    if (TCG_TARGET_HAS_v256) {
        for (; i + 32 <= oprsz; i += 32) {
            tcg_gen_stl_vec(t_vec, cpu_env, dofs + i, TCG_TYPE_V256);
        }
    }
    if (TCG_TARGET_HAS_v128) {
        for (; i + 16 <= oprsz; i += 16) {
            tcg_gen_stl_vec(t_vec, cpu_env, dofs + i, TCG_TYPE_V128);
        }
    }
    if (TCG_TARGET_HAS_v64) {
        for (; i < oprsz; i += 8) {
            tcg_gen_stl_vec(t_vec, cpu_env, dofs + i, TCG_TYPE_V64);
        }
    }

 done:
    tcg_debug_assert(i == oprsz);
    if (i < maxsz) {
        expand_clr(dofs + i, maxsz - i);
    }
}

/* Likewise, but with zero.  */
static void expand_clr(uint32_t dofs, uint32_t maxsz)
{
    if (TCG_TARGET_REG_BITS == 64) {
        do_dup_i64(MO_64, dofs, maxsz, maxsz, NULL, 0);
    } else {
        do_dup_i32(MO_32, dofs, maxsz, maxsz, NULL, 0, gen_helper_gvec_dup32);
    }
}

/* Expand OPSZ bytes worth of two-operand operations using i32 elements.  */
static void expand_2_i32(uint32_t dofs, uint32_t aofs, uint32_t opsz,
                         void (*fni)(TCGv_i32, TCGv_i32))
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    uint32_t i;

    for (i = 0; i < opsz; i += 4) {
        tcg_gen_ld_i32(t0, cpu_env, aofs + i);
        fni(t0, t0);
        tcg_gen_st_i32(t0, cpu_env, dofs + i);
    }
    tcg_temp_free_i32(t0);
}

static void expand_2i_i32(uint32_t dofs, uint32_t aofs, uint32_t opsz,
                          unsigned c, bool load_dest,
                          void (*fni)(TCGv_i32, TCGv_i32, unsigned))
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    uint32_t i;

    for (i = 0; i < opsz; i += 4) {
        tcg_gen_ld_i32(t0, cpu_env, aofs + i);
        if (load_dest) {
            tcg_gen_ld_i32(t1, cpu_env, dofs + i);
        }
        fni(t1, t0, c);
        tcg_gen_st_i32(t1, cpu_env, dofs + i);
    }
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

/* Expand OPSZ bytes worth of three-operand operations using i32 elements.  */
static void expand_3_i32(uint32_t dofs, uint32_t aofs,
                         uint32_t bofs, uint32_t opsz, bool load_dest,
                         void (*fni)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    uint32_t i;

    for (i = 0; i < opsz; i += 4) {
        tcg_gen_ld_i32(t0, cpu_env, aofs + i);
        tcg_gen_ld_i32(t1, cpu_env, bofs + i);
        if (load_dest) {
            tcg_gen_ld_i32(t2, cpu_env, dofs + i);
        }
        fni(t2, t0, t1);
        tcg_gen_st_i32(t2, cpu_env, dofs + i);
    }
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
}

/* Expand OPSZ bytes worth of two-operand operations using i64 elements.  */
static void expand_2_i64(uint32_t dofs, uint32_t aofs, uint32_t opsz,
                         void (*fni)(TCGv_i64, TCGv_i64))
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    uint32_t i;

    for (i = 0; i < opsz; i += 8) {
        tcg_gen_ld_i64(t0, cpu_env, aofs + i);
        fni(t0, t0);
        tcg_gen_st_i64(t0, cpu_env, dofs + i);
    }
    tcg_temp_free_i64(t0);
}

static void expand_2i_i64(uint32_t dofs, uint32_t aofs, uint32_t opsz,
                          unsigned c, bool load_dest,
                          void (*fni)(TCGv_i64, TCGv_i64, unsigned))
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    uint32_t i;

    for (i = 0; i < opsz; i += 8) {
        tcg_gen_ld_i64(t0, cpu_env, aofs + i);
        if (load_dest) {
            tcg_gen_ld_i64(t1, cpu_env, dofs + i);
        }
        fni(t1, t0, c);
        tcg_gen_st_i64(t1, cpu_env, dofs + i);
    }
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

/* Expand OPSZ bytes worth of three-operand operations using i64 elements.  */
static void expand_3_i64(uint32_t dofs, uint32_t aofs,
                         uint32_t bofs, uint32_t opsz, bool load_dest,
                         void (*fni)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    uint32_t i;

    for (i = 0; i < opsz; i += 8) {
        tcg_gen_ld_i64(t0, cpu_env, aofs + i);
        tcg_gen_ld_i64(t1, cpu_env, bofs + i);
        if (load_dest) {
            tcg_gen_ld_i64(t2, cpu_env, dofs + i);
        }
        fni(t2, t0, t1);
        tcg_gen_st_i64(t2, cpu_env, dofs + i);
    }
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t0);
}

/* Expand OPSZ bytes worth of two-operand operations using host vectors.  */
static void expand_2_vec(unsigned vece, uint32_t dofs, uint32_t aofs,
                         uint32_t opsz, uint32_t tysz, TCGType type,
                         void (*fni)(unsigned, TCGv_vec, TCGv_vec))
{
    TCGv_vec t0 = tcg_temp_new_vec(type);
    uint32_t i;

    for (i = 0; i < opsz; i += tysz) {
        tcg_gen_ld_vec(t0, cpu_env, aofs + i);
        fni(vece, t0, t0);
        tcg_gen_st_vec(t0, cpu_env, dofs + i);
    }
    tcg_temp_free_vec(t0);
}

/* Expand OPSZ bytes worth of two-vector operands and an immediate operand
   using host vectors.  */
static void expand_2i_vec(unsigned vece, uint32_t dofs, uint32_t aofs,
                          uint32_t opsz, uint32_t tysz, TCGType type,
                          unsigned c, bool load_dest,
                          void (*fni)(unsigned, TCGv_vec, TCGv_vec, unsigned))
{
    TCGv_vec t0 = tcg_temp_new_vec(type);
    TCGv_vec t1 = tcg_temp_new_vec(type);
    uint32_t i;

    for (i = 0; i < opsz; i += tysz) {
        tcg_gen_ld_vec(t0, cpu_env, aofs + i);
        if (load_dest) {
            tcg_gen_ld_vec(t1, cpu_env, dofs + i);
        }
        fni(vece, t1, t0, c);
        tcg_gen_st_vec(t1, cpu_env, dofs + i);
    }
    tcg_temp_free_vec(t0);
    tcg_temp_free_vec(t1);
}

/* Expand OPSZ bytes worth of three-operand operations using host vectors.  */
static void expand_3_vec(unsigned vece, uint32_t dofs, uint32_t aofs,
                         uint32_t bofs, uint32_t opsz,
                         uint32_t tysz, TCGType type, bool load_dest,
                         void (*fni)(unsigned, TCGv_vec, TCGv_vec, TCGv_vec))
{
    TCGv_vec t0 = tcg_temp_new_vec(type);
    TCGv_vec t1 = tcg_temp_new_vec(type);
    TCGv_vec t2 = tcg_temp_new_vec(type);
    uint32_t i;

    for (i = 0; i < opsz; i += tysz) {
        tcg_gen_ld_vec(t0, cpu_env, aofs + i);
        tcg_gen_ld_vec(t1, cpu_env, bofs + i);
        if (load_dest) {
            tcg_gen_ld_vec(t2, cpu_env, dofs + i);
        }
        fni(vece, t2, t0, t1);
        tcg_gen_st_vec(t2, cpu_env, dofs + i);
    }
    tcg_temp_free_vec(t2);
    tcg_temp_free_vec(t1);
    tcg_temp_free_vec(t0);
}

/* Expand a vector two-operand operation.  */
void tcg_gen_gvec_2(uint32_t dofs, uint32_t aofs,
                    uint32_t oprsz, uint32_t maxsz, const GVecGen2 *g)
{
    check_size_align(oprsz, maxsz, dofs | aofs);
    check_overlap_2(dofs, aofs, maxsz);

    /* Quick check for sizes we won't support inline.  */
    if (oprsz > MAX_UNROLL * 32 || maxsz > MAX_UNROLL * 32) {
        goto do_ool;
    }

    /* Recall that ARM SVE allows vector sizes that are not a power of 2.
       Expand with successively smaller host vector sizes.  The intent is
       that e.g. oprsz == 80 would be expanded with 2x32 + 1x16.  */
    /* ??? For maxsz > oprsz, the host may be able to use an op-sized
       operation, zeroing the balance of the register.  We can then
       use a cl-sized store to implement the clearing without an extra
       store operation.  This is true for aarch64 and x86_64 hosts.  */

    if (TCG_TARGET_HAS_v256 && check_size_impl(oprsz, 32)
        && (!g->opc || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V256, g->vece))) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 32);
        expand_2_vec(g->vece, dofs, aofs, done, 32, TCG_TYPE_V256, g->fniv);
        dofs += done;
        aofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (TCG_TARGET_HAS_v128 && check_size_impl(oprsz, 16)
        && (!g->opc || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V128, g->vece))) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 16);
        expand_2_vec(g->vece, dofs, aofs, done, 16, TCG_TYPE_V128, g->fniv);
        dofs += done;
        aofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (check_size_impl(oprsz, 8)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 8);
        if (TCG_TARGET_HAS_v64 && !g->prefer_i64
            && (!g->opc
                || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V64, g->vece))) {
            expand_2_vec(g->vece, dofs, aofs, done, 8, TCG_TYPE_V64, g->fniv);
        } else if (g->fni8) {
            expand_2_i64(dofs, aofs, done, g->fni8);
        } else {
            done = 0;
        }
        dofs += done;
        aofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (g->fni4 && check_size_impl(oprsz, 4)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 4);
        expand_2_i32(dofs, aofs, done, g->fni4);
        dofs += done;
        aofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (oprsz == 0) {
        if (maxsz != 0) {
            expand_clr(dofs, maxsz);
        }
        return;
    }

 do_ool:
    tcg_gen_gvec_2_ool(dofs, aofs, oprsz, maxsz, g->data, g->fno);
}

void tcg_gen_gvec_2i(uint32_t dofs, uint32_t aofs, uint32_t oprsz,
                     uint32_t maxsz, unsigned c, const GVecGen2i *g)
{
    check_size_align(oprsz, maxsz, dofs | aofs);
    check_overlap_2(dofs, aofs, maxsz);

    /* Quick check for sizes we won't support inline.  */
    if (oprsz > MAX_UNROLL * 32 || maxsz > MAX_UNROLL * 32) {
        goto do_ool;
    }

    /* Recall that ARM SVE allows vector sizes that are not a power of 2.
       Expand with successively smaller host vector sizes.  The intent is
       that e.g. oprsz == 80 would be expanded with 2x32 + 1x16.  */
    /* ??? For maxsz > oprsz, the host may be able to use an op-sized
       operation, zeroing the balance of the register.  We can then
       use a cl-sized store to implement the clearing without an extra
       store operation.  This is true for aarch64 and x86_64 hosts.  */

    if (TCG_TARGET_HAS_v256 && check_size_impl(oprsz, 32)
        && (!g->opc || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V256, g->vece))) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 32);
        expand_2i_vec(g->vece, dofs, aofs, done, 32, TCG_TYPE_V256,
                      c, g->load_dest, g->fniv);
        dofs += done;
        aofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (TCG_TARGET_HAS_v128 && check_size_impl(oprsz, 16)
        && (!g->opc || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V128, g->vece))) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 16);
        expand_2i_vec(g->vece, dofs, aofs, done, 16, TCG_TYPE_V128,
                      c, g->load_dest, g->fniv);
        dofs += done;
        aofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (check_size_impl(oprsz, 8)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 8);
        if (TCG_TARGET_HAS_v64 && !g->prefer_i64
            && (!g->opc
                || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V64, g->vece))) {
            expand_2i_vec(g->vece, dofs, aofs, done, 8, TCG_TYPE_V64,
                          c, g->load_dest, g->fniv);
        } else if (g->fni8) {
            expand_2i_i64(dofs, aofs, done, c, g->load_dest, g->fni8);
        } else {
            done = 0;
        }
        dofs += done;
        aofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (g->fni4 && check_size_impl(oprsz, 4)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 4);
        expand_2i_i32(dofs, aofs, done, c, g->load_dest, g->fni4);
        dofs += done;
        aofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (oprsz == 0) {
        if (maxsz != 0) {
            expand_clr(dofs, maxsz);
        }
        return;
    }

 do_ool:
    tcg_gen_gvec_2_ool(dofs, aofs, oprsz, maxsz, 0, g->fno);
}

/* Expand a vector three-operand operation.  */
void tcg_gen_gvec_3(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                    uint32_t oprsz, uint32_t maxsz, const GVecGen3 *g)
{
    check_size_align(oprsz, maxsz, dofs | aofs | bofs);
    check_overlap_3(dofs, aofs, bofs, maxsz);

    /* Quick check for sizes we won't support inline.  */
    if (oprsz > MAX_UNROLL * 32 || maxsz > MAX_UNROLL * 32) {
        goto do_ool;
    }

    /* Recall that ARM SVE allows vector sizes that are not a power of 2.
       Expand with successively smaller host vector sizes.  The intent is
       that e.g. oprsz == 80 would be expanded with 2x32 + 1x16.  */
    /* ??? For maxsz > oprsz, the host may be able to use an op-sized
       operation, zeroing the balance of the register.  We can then
       use a cl-sized store to implement the clearing without an extra
       store operation.  This is true for aarch64 and x86_64 hosts.  */

    if (TCG_TARGET_HAS_v256 && check_size_impl(oprsz, 32)
        && (!g->opc || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V256, g->vece))) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 32);
        expand_3_vec(g->vece, dofs, aofs, bofs, done, 32, TCG_TYPE_V256,
                     g->load_dest, g->fniv);
        dofs += done;
        aofs += done;
        bofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (TCG_TARGET_HAS_v128 && check_size_impl(oprsz, 16)
        && (!g->opc || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V128, g->vece))) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 16);
        expand_3_vec(g->vece, dofs, aofs, bofs, done, 16, TCG_TYPE_V128,
                     g->load_dest, g->fniv);
        dofs += done;
        aofs += done;
        bofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (check_size_impl(oprsz, 8)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 8);
        if (TCG_TARGET_HAS_v64 && !g->prefer_i64
            && (!g->opc
                || tcg_can_emit_vec_op(g->opc, TCG_TYPE_V64, g->vece))) {
            expand_3_vec(g->vece, dofs, aofs, bofs, done, 8, TCG_TYPE_V64,
                         g->load_dest, g->fniv);
        } else if (g->fni8) {
            expand_3_i64(dofs, aofs, bofs, done, g->load_dest, g->fni8);
        } else {
            done = 0;
        }
        dofs += done;
        aofs += done;
        bofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (g->fni4 && check_size_impl(oprsz, 4)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 4);
        expand_3_i32(dofs, aofs, bofs, done, g->load_dest, g->fni4);
        dofs += done;
        aofs += done;
        bofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (oprsz == 0) {
        if (maxsz != 0) {
            expand_clr(dofs, maxsz);
        }
        return;
    }

 do_ool:
    tcg_gen_gvec_3_ool(dofs, aofs, bofs, oprsz, maxsz, g->data, g->fno);
}

/*
 * Expand specific vector operations.
 */

static void vec_mov2(unsigned vece, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_mov_vec(a, b);
}

void tcg_gen_gvec_mov(unsigned vece, uint32_t dofs, uint32_t aofs,
                      uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen2 g = {
        .fni8 = tcg_gen_mov_i64,
        .fniv = vec_mov2,
        .fno = gen_helper_gvec_mov,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_2(dofs, aofs, opsz, maxsz, &g);
}

void tcg_gen_gvec_dup_i32(unsigned vece, uint32_t dofs, uint32_t oprsz,
                          uint32_t maxsz, TCGv_i32 in)
{
    typedef void dup_fn(TCGv_ptr, TCGv_i32, TCGv_i32);
    static dup_fn * const fns[3] = {
        gen_helper_gvec_dup8,
        gen_helper_gvec_dup16,
        gen_helper_gvec_dup32
    };

    check_size_align(oprsz, maxsz, dofs);
    tcg_debug_assert(vece <= MO_32);
    do_dup_i32(vece, dofs, oprsz, maxsz, in, 0, fns[vece]);
}

void tcg_gen_gvec_dup_i64(unsigned vece, uint32_t dofs, uint32_t oprsz,
                          uint32_t maxsz, TCGv_i64 in)
{
    check_size_align(oprsz, maxsz, dofs);
    tcg_debug_assert(vece <= MO_64);
    if (vece <= MO_32) {
        /* This works for both host register sizes.  */
        tcg_gen_gvec_dup_i32(vece, dofs, oprsz, maxsz, (TCGv_i32)in);
    } else {
        do_dup_i64(vece, dofs, oprsz, maxsz, in, 0);
    }
}

void tcg_gen_gvec_dup_mem(unsigned vece, uint32_t dofs, uint32_t aofs,
                          uint32_t oprsz, uint32_t maxsz)
{
    tcg_debug_assert(vece <= MO_64);
    if (vece <= MO_32) {
        TCGv_i32 in = tcg_temp_new_i32();
        switch (vece) {
        case MO_8:
            tcg_gen_ld8u_i32(in, cpu_env, aofs);
            break;
        case MO_16:
            tcg_gen_ld16u_i32(in, cpu_env, aofs);
            break;
        case MO_32:
            tcg_gen_ld_i32(in, cpu_env, aofs);
            break;
        }
        tcg_gen_gvec_dup_i32(vece, dofs, oprsz, maxsz, in);
        tcg_temp_free_i32(in);
    } else {
        TCGv_i64 in = tcg_temp_new_i64();
        tcg_gen_ld_i64(in, cpu_env, aofs);
        tcg_gen_gvec_dup_i64(MO_64, dofs, oprsz, maxsz, in);
        tcg_temp_free_i64(in);
    }
}

void tcg_gen_gvec_dup64i(uint32_t dofs, uint32_t oprsz,
                         uint32_t maxsz, uint64_t x)
{
    check_size_align(oprsz, maxsz, dofs);
    do_dup_i64(MO_64, dofs, oprsz, maxsz, NULL, x);
}

void tcg_gen_gvec_dup32i(uint32_t dofs, uint32_t oprsz,
                         uint32_t maxsz, uint32_t x)
{
    if (TCG_TARGET_REG_BITS == 64) {
        do_dup_i64(MO_64, dofs, oprsz, maxsz, NULL, deposit64(x, 32, 32, x));
    } else {
        do_dup_i32(MO_32, dofs, oprsz, maxsz, NULL, x, gen_helper_gvec_dup32);
    }
}

void tcg_gen_gvec_dup16i(uint32_t dofs, uint32_t oprsz,
                         uint32_t maxsz, uint16_t x)
{
    tcg_gen_gvec_dup32i(dofs, oprsz, maxsz, 0x00010001 * x);
}

void tcg_gen_gvec_dup8i(uint32_t dofs, uint32_t oprsz,
                         uint32_t maxsz, uint8_t x)
{
    tcg_gen_gvec_dup32i(dofs, oprsz, maxsz, 0x01010101 * x);
}

void tcg_gen_gvec_not(unsigned vece, uint32_t dofs, uint32_t aofs,
                      uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen2 g = {
        .fni8 = tcg_gen_not_i64,
        .fniv = tcg_gen_not_vec,
        .fno = gen_helper_gvec_not,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_2(dofs, aofs, opsz, maxsz, &g);
}

/* Perform a vector addition using normal addition and a mask.  The mask
   should be the sign bit of each lane.  This 6-operation form is more
   efficient than separate additions when there are 4 or more lanes in
   the 64-bit operation.  */
static void gen_addv_mask(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b, TCGv_i64 m)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();

    tcg_gen_andc_i64(t1, a, m);
    tcg_gen_andc_i64(t2, b, m);
    tcg_gen_xor_i64(t3, a, b);
    tcg_gen_add_i64(d, t1, t2);
    tcg_gen_and_i64(t3, t3, m);
    tcg_gen_xor_i64(d, d, t3);

    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

void tcg_gen_vec_add8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 m = tcg_const_i64(REP8(0x80));
    gen_addv_mask(d, a, b, m);
    tcg_temp_free_i64(m);
}

void tcg_gen_vec_add16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 m = tcg_const_i64(REP16(0x8000));
    gen_addv_mask(d, a, b, m);
    tcg_temp_free_i64(m);
}

void tcg_gen_vec_add32_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_andi_i64(t1, a, ~0xffffffffull);
    tcg_gen_add_i64(t2, a, b);
    tcg_gen_add_i64(t1, t1, b);
    tcg_gen_deposit_i64(d, t1, t2, 0, 32);

    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

void tcg_gen_gvec_add(unsigned vece, uint32_t dofs, uint32_t aofs,
                      uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g[4] = {
        { .fni8 = tcg_gen_vec_add8_i64,
          .fniv = tcg_gen_add_vec,
          .fno = gen_helper_gvec_add8,
          .opc = INDEX_op_add_vec,
          .vece = MO_8 },
        { .fni8 = tcg_gen_vec_add16_i64,
          .fniv = tcg_gen_add_vec,
          .fno = gen_helper_gvec_add16,
          .opc = INDEX_op_add_vec,
          .vece = MO_16 },
        { .fni4 = tcg_gen_add_i32,
          .fniv = tcg_gen_add_vec,
          .fno = gen_helper_gvec_add32,
          .opc = INDEX_op_add_vec,
          .vece = MO_32 },
        { .fni8 = tcg_gen_add_i64,
          .fniv = tcg_gen_add_vec,
          .fno = gen_helper_gvec_add64,
          .opc = INDEX_op_add_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g[vece]);
}

/* Perform a vector subtraction using normal subtraction and a mask.
   Compare gen_addv_mask above.  */
static void gen_subv_mask(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b, TCGv_i64 m)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();

    tcg_gen_or_i64(t1, a, m);
    tcg_gen_andc_i64(t2, b, m);
    tcg_gen_eqv_i64(t3, a, b);
    tcg_gen_sub_i64(d, t1, t2);
    tcg_gen_and_i64(t3, t3, m);
    tcg_gen_xor_i64(d, d, t3);

    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

void tcg_gen_vec_sub8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 m = tcg_const_i64(REP8(0x80));
    gen_subv_mask(d, a, b, m);
    tcg_temp_free_i64(m);
}

void tcg_gen_vec_sub16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 m = tcg_const_i64(REP16(0x8000));
    gen_subv_mask(d, a, b, m);
    tcg_temp_free_i64(m);
}

void tcg_gen_vec_sub32_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_andi_i64(t1, b, ~0xffffffffull);
    tcg_gen_sub_i64(t2, a, b);
    tcg_gen_sub_i64(t1, a, t1);
    tcg_gen_deposit_i64(d, t1, t2, 0, 32);

    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

void tcg_gen_gvec_sub(unsigned vece, uint32_t dofs, uint32_t aofs,
                      uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g[4] = {
        { .fni8 = tcg_gen_vec_sub8_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_gvec_sub8,
          .opc = INDEX_op_sub_vec,
          .vece = MO_8 },
        { .fni8 = tcg_gen_vec_sub16_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_gvec_sub16,
          .opc = INDEX_op_sub_vec,
          .vece = MO_16 },
        { .fni4 = tcg_gen_sub_i32,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_gvec_sub32,
          .opc = INDEX_op_sub_vec,
          .vece = MO_32 },
        { .fni8 = tcg_gen_sub_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_gvec_sub64,
          .opc = INDEX_op_sub_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g[vece]);
}

/* Perform a vector negation using normal negation and a mask.
   Compare gen_subv_mask above.  */
static void gen_negv_mask(TCGv_i64 d, TCGv_i64 b, TCGv_i64 m)
{
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();

    tcg_gen_andc_i64(t3, m, b);
    tcg_gen_andc_i64(t2, b, m);
    tcg_gen_sub_i64(d, m, t2);
    tcg_gen_xor_i64(d, d, t3);

    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

void tcg_gen_vec_neg8_i64(TCGv_i64 d, TCGv_i64 b)
{
    TCGv_i64 m = tcg_const_i64(REP8(0x80));
    gen_negv_mask(d, b, m);
    tcg_temp_free_i64(m);
}

void tcg_gen_vec_neg16_i64(TCGv_i64 d, TCGv_i64 b)
{
    TCGv_i64 m = tcg_const_i64(REP16(0x8000));
    gen_negv_mask(d, b, m);
    tcg_temp_free_i64(m);
}

void tcg_gen_vec_neg32_i64(TCGv_i64 d, TCGv_i64 b)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_andi_i64(t1, b, ~0xffffffffull);
    tcg_gen_neg_i64(t2, b);
    tcg_gen_neg_i64(t1, t1);
    tcg_gen_deposit_i64(d, t1, t2, 0, 32);

    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

void tcg_gen_gvec_neg(unsigned vece, uint32_t dofs, uint32_t aofs,
                      uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen2 g[4] = {
        { .fni8 = tcg_gen_vec_neg8_i64,
          .fniv = tcg_gen_neg_vec,
          .fno = gen_helper_gvec_neg8,
          .opc = INDEX_op_neg_vec,
          .vece = MO_8 },
        { .fni8 = tcg_gen_vec_neg16_i64,
          .fniv = tcg_gen_neg_vec,
          .fno = gen_helper_gvec_neg16,
          .opc = INDEX_op_neg_vec,
          .vece = MO_16 },
        { .fni4 = tcg_gen_neg_i32,
          .fniv = tcg_gen_neg_vec,
          .fno = gen_helper_gvec_neg32,
          .opc = INDEX_op_neg_vec,
          .vece = MO_32 },
        { .fni8 = tcg_gen_neg_i64,
          .fniv = tcg_gen_neg_vec,
          .fno = gen_helper_gvec_neg64,
          .opc = INDEX_op_neg_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_2(dofs, aofs, opsz, maxsz, &g[vece]);
}

void tcg_gen_gvec_and(unsigned vece, uint32_t dofs, uint32_t aofs,
                      uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g = {
        .fni8 = tcg_gen_and_i64,
        .fniv = tcg_gen_and_vec,
        .fno = gen_helper_gvec_and,
        .opc = INDEX_op_and_vec,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g);
}

void tcg_gen_gvec_or(unsigned vece, uint32_t dofs, uint32_t aofs,
                     uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g = {
        .fni8 = tcg_gen_or_i64,
        .fniv = tcg_gen_or_vec,
        .fno = gen_helper_gvec_or,
        .opc = INDEX_op_or_vec,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g);
}

void tcg_gen_gvec_xor(unsigned vece, uint32_t dofs, uint32_t aofs,
                      uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g = {
        .fni8 = tcg_gen_xor_i64,
        .fniv = tcg_gen_xor_vec,
        .fno = gen_helper_gvec_xor,
        .opc = INDEX_op_xor_vec,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g);
}

void tcg_gen_gvec_andc(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g = {
        .fni8 = tcg_gen_andc_i64,
        .fniv = tcg_gen_andc_vec,
        .fno = gen_helper_gvec_andc,
        .opc = INDEX_op_andc_vec,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g);
}

void tcg_gen_gvec_orc(unsigned vece, uint32_t dofs, uint32_t aofs,
                      uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g = {
        .fni8 = tcg_gen_orc_i64,
        .fniv = tcg_gen_orc_vec,
        .fno = gen_helper_gvec_orc,
        .opc = INDEX_op_orc_vec,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g);
}

void tcg_gen_vec_shl8i_i64(TCGv_i64 d, TCGv_i64 a, unsigned c)
{
    uint64_t mask = ((0xff << c) & 0xff) * (-1ull / 0xff);
    tcg_gen_shli_i64(d, a, c);
    tcg_gen_andi_i64(d, d, mask);
}

void tcg_gen_vec_shl16i_i64(TCGv_i64 d, TCGv_i64 a, unsigned c)
{
    uint64_t mask = ((0xffff << c) & 0xffff) * (-1ull / 0xffff);
    tcg_gen_shli_i64(d, a, c);
    tcg_gen_andi_i64(d, d, mask);
}

void tcg_gen_gvec_shli(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t opsz, uint32_t clsz, unsigned shift)
{
    static const GVecGen2i g[4] = {
        { .fni8 = tcg_gen_vec_shl8i_i64,
          .fniv = tcg_gen_shli_vec,
          .fno = gen_helper_gvec_shl8i,
          .opc = INDEX_op_shli_vec,
          .vece = MO_8 },
        { .fni8 = tcg_gen_vec_shl16i_i64,
          .fniv = tcg_gen_shli_vec,
          .fno = gen_helper_gvec_shl16i,
          .opc = INDEX_op_shli_vec,
          .vece = MO_16 },
        { .fni4 = tcg_gen_shli_i32,
          .fniv = tcg_gen_shli_vec,
          .fno = gen_helper_gvec_shl32i,
          .opc = INDEX_op_shli_vec,
          .vece = MO_32 },
        { .fni8 = tcg_gen_shli_i64,
          .fniv = tcg_gen_shli_vec,
          .fno = gen_helper_gvec_shl64i,
          .opc = INDEX_op_shli_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    if (shift == 0) {
        tcg_gen_gvec_mov(vece, dofs, aofs, opsz, clsz);
    } else {
        tcg_gen_gvec_2i(dofs, aofs, opsz, clsz, shift, &g[vece]);
    }
}

void tcg_gen_vec_shr8i_i64(TCGv_i64 d, TCGv_i64 a, unsigned c)
{
    uint64_t mask = (0xff >> c) * (-1ull / 0xff);
    tcg_gen_shri_i64(d, a, c);
    tcg_gen_andi_i64(d, d, mask);
}

void tcg_gen_vec_shr16i_i64(TCGv_i64 d, TCGv_i64 a, unsigned c)
{
    uint64_t mask = (0xffff >> c) * (-1ull / 0xffff);
    tcg_gen_shri_i64(d, a, c);
    tcg_gen_andi_i64(d, d, mask);
}

void tcg_gen_gvec_shri(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t opsz, uint32_t clsz, unsigned shift)
{
    static const GVecGen2i g[4] = {
        { .fni8 = tcg_gen_vec_shr8i_i64,
          .fniv = tcg_gen_shri_vec,
          .fno = gen_helper_gvec_shr8i,
          .opc = INDEX_op_shri_vec,
          .vece = MO_8 },
        { .fni8 = tcg_gen_vec_shr16i_i64,
          .fniv = tcg_gen_shri_vec,
          .fno = gen_helper_gvec_shr16i,
          .opc = INDEX_op_shri_vec,
          .vece = MO_16 },
        { .fni4 = tcg_gen_shri_i32,
          .fniv = tcg_gen_shri_vec,
          .fno = gen_helper_gvec_shr32i,
          .opc = INDEX_op_shri_vec,
          .vece = MO_32 },
        { .fni8 = tcg_gen_shri_i64,
          .fniv = tcg_gen_shri_vec,
          .fno = gen_helper_gvec_shr64i,
          .opc = INDEX_op_shri_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    if (shift == 0) {
        tcg_gen_gvec_mov(vece, dofs, aofs, opsz, clsz);
    } else {
        tcg_gen_gvec_2i(dofs, aofs, opsz, clsz, shift, &g[vece]);
    }
}

void tcg_gen_vec_sar8i_i64(TCGv_i64 d, TCGv_i64 a, unsigned c)
{
    uint64_t s_mask = (0x80 >> c) * (-1ull / 0xff);
    uint64_t c_mask = (0xff >> c) * (-1ull / 0xff);
    TCGv_i64 s = tcg_temp_new_i64();

    tcg_gen_shri_i64(d, a, c);
    tcg_gen_andi_i64(s, d, s_mask);  /* isolate (shifted) sign bit */
    tcg_gen_muli_i64(s, s, (2 << c) - 2); /* replicate isolated signs */
    tcg_gen_andi_i64(d, d, c_mask);  /* clear out bits above sign  */
    tcg_gen_or_i64(d, d, s);         /* include sign extension */
    tcg_temp_free_i64(s);
}

void tcg_gen_vec_sar16i_i64(TCGv_i64 d, TCGv_i64 a, unsigned c)
{
    uint64_t s_mask = (0x8000 >> c) * (-1ull / 0xffff);
    uint64_t c_mask = (0xffff >> c) * (-1ull / 0xffff);
    TCGv_i64 s = tcg_temp_new_i64();

    tcg_gen_shri_i64(d, a, c);
    tcg_gen_andi_i64(s, d, s_mask);  /* isolate (shifted) sign bit */
    tcg_gen_andi_i64(d, d, c_mask);  /* clear out bits above sign  */
    tcg_gen_muli_i64(s, s, (2 << c) - 2); /* replicate isolated signs */
    tcg_gen_or_i64(d, d, s);         /* include sign extension */
    tcg_temp_free_i64(s);
}

void tcg_gen_gvec_sari(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t opsz, uint32_t clsz, unsigned shift)
{
    static const GVecGen2i g[4] = {
        { .fni8 = tcg_gen_vec_sar8i_i64,
          .fniv = tcg_gen_sari_vec,
          .fno = gen_helper_gvec_sar8i,
          .opc = INDEX_op_sari_vec,
          .vece = MO_8 },
        { .fni8 = tcg_gen_vec_sar16i_i64,
          .fniv = tcg_gen_sari_vec,
          .fno = gen_helper_gvec_sar16i,
          .opc = INDEX_op_sari_vec,
          .vece = MO_16 },
        { .fni4 = tcg_gen_sari_i32,
          .fniv = tcg_gen_sari_vec,
          .fno = gen_helper_gvec_sar32i,
          .opc = INDEX_op_sari_vec,
          .vece = MO_32 },
        { .fni8 = tcg_gen_sari_i64,
          .fniv = tcg_gen_sari_vec,
          .fno = gen_helper_gvec_sar64i,
          .opc = INDEX_op_sari_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    if (shift == 0) {
        tcg_gen_gvec_mov(vece, dofs, aofs, opsz, clsz);
    } else {
        tcg_gen_gvec_2i(dofs, aofs, opsz, clsz, shift, &g[vece]);
    }
}

static void do_zip(unsigned vece, uint32_t dofs, uint32_t aofs,
                   uint32_t bofs, uint32_t oprsz, uint32_t maxsz,
                   bool high)
{
    static gen_helper_gvec_3 * const zip_fns[4] = {
        gen_helper_gvec_zip8,
        gen_helper_gvec_zip16,
        gen_helper_gvec_zip32,
        gen_helper_gvec_zip64,
    };

    TCGType type;
    uint32_t step, i, n;
    TCGOpcode zip_op;

    check_size_align(oprsz, maxsz, dofs | aofs | bofs);
    check_overlap_3(dofs, aofs, bofs, oprsz);
    tcg_debug_assert(vece <= MO_64);

    /* Quick check for sizes we won't support inline.  */
    if (oprsz > 4 * 32 || maxsz > MAX_UNROLL * 32) {
        goto do_ool;
    }

    zip_op = high ? INDEX_op_ziph_vec : INDEX_op_zipl_vec;

    /* Since these operations don't operate in lock-step lanes,
       we must care for overlap.  */
    if (TCG_TARGET_HAS_v256 && oprsz % 32 == 0 && oprsz / 32 <= 8
        && tcg_can_emit_vec_op(zip_op, TCG_TYPE_V256, vece)) {
        type = TCG_TYPE_V256;
        step = 32;
        n = oprsz / 32;
    } else if (TCG_TARGET_HAS_v128 && oprsz % 16 == 0 && oprsz / 16 <= 8
               && tcg_can_emit_vec_op(zip_op, TCG_TYPE_V128, vece)) {
        type = TCG_TYPE_V128;
        step = 16;
        n = oprsz / 16;
    } else if (TCG_TARGET_HAS_v64 && oprsz % 8 == 0 && oprsz / 8 <= 8
               && tcg_can_emit_vec_op(zip_op, TCG_TYPE_V64, vece)) {
        type = TCG_TYPE_V64;
        step = 8;
        n = oprsz / 8;
    } else {
        goto do_ool;
    }

    if (n == 1) {
        TCGv_vec t1 = tcg_temp_new_vec(type);
        TCGv_vec t2 = tcg_temp_new_vec(type);

        tcg_gen_ld_vec(t1, cpu_env, aofs);
        tcg_gen_ld_vec(t2, cpu_env, bofs);
        if (high) {
            tcg_gen_ziph_vec(vece, t1, t1, t2);
        } else {
            tcg_gen_zipl_vec(vece, t1, t1, t2);
        }
        tcg_gen_st_vec(t1, cpu_env, dofs);
        tcg_temp_free_vec(t1);
        tcg_temp_free_vec(t2);
    } else {
        TCGv_vec ta[4], tb[4], tmp;

        if (high) {
            aofs += oprsz / 2;
            bofs += oprsz / 2;
        }

        for (i = 0; i < (n / 2 + n % 2); ++i) {
            ta[i] = tcg_temp_new_vec(type);
            tb[i] = tcg_temp_new_vec(type);
            tcg_gen_ld_vec(ta[i], cpu_env, aofs + i * step);
            tcg_gen_ld_vec(tb[i], cpu_env, bofs + i * step);
        }

        tmp = tcg_temp_new_vec(type);
        for (i = 0; i < n; ++i) {
            if (i & 1) {
                tcg_gen_ziph_vec(vece, tmp, ta[i / 2], tb[i / 2]);
            } else {
                tcg_gen_zipl_vec(vece, tmp, ta[i / 2], tb[i / 2]);
            }
            tcg_gen_st_vec(tmp, cpu_env, dofs + i * step);
        }
        tcg_temp_free_vec(tmp);

        for (i = 0; i < (n / 2 + n % 2); ++i) {
            tcg_temp_free_vec(ta[i]);
            tcg_temp_free_vec(tb[i]);
        }
    }
    if (oprsz < maxsz) {
        expand_clr(dofs + oprsz, maxsz - oprsz);
    }
    return;

  do_ool:
    if (high) {
        aofs += oprsz / 2;
        bofs += oprsz / 2;
    }
    tcg_gen_gvec_3_ool(dofs, aofs, bofs, oprsz, maxsz, 0, zip_fns[vece]);
}

void tcg_gen_gvec_zipl(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t bofs, uint32_t oprsz, uint32_t maxsz)
{
    do_zip(vece, dofs, aofs, bofs, oprsz, maxsz, false);
}

void tcg_gen_gvec_ziph(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t bofs, uint32_t oprsz, uint32_t maxsz)
{
    do_zip(vece, dofs, aofs, bofs, oprsz, maxsz, true);
}

static void do_uzp(unsigned vece, uint32_t dofs, uint32_t aofs,
                   uint32_t bofs, uint32_t oprsz, uint32_t maxsz, bool odd)
{
    static gen_helper_gvec_3 * const uzp_fns[4] = {
        gen_helper_gvec_uzp8,
        gen_helper_gvec_uzp16,
        gen_helper_gvec_uzp32,
        gen_helper_gvec_uzp64,
    };

    TCGType type;
    uint32_t step, i, n;
    TCGv_vec t[8];
    TCGOpcode uzp_op;

    check_size_align(oprsz, maxsz, dofs | aofs | bofs);
    check_overlap_3(dofs, aofs, bofs, oprsz);
    tcg_debug_assert(vece <= MO_64);

    /* Quick check for sizes we won't support inline.  */
    if (oprsz > 4 * 32 || maxsz > MAX_UNROLL * 32) {
        goto do_ool;
    }

    uzp_op = odd ? INDEX_op_uzpo_vec : INDEX_op_uzpe_vec;

    /* Since these operations don't operate in lock-step lanes,
       we must care for overlap.  */
    if (TCG_TARGET_HAS_v256 && oprsz % 32 == 0 && oprsz / 32 <= 4
        && tcg_can_emit_vec_op(uzp_op, TCG_TYPE_V256, vece)) {
        type = TCG_TYPE_V256;
        step = 32;
        n = oprsz / 32;
    } else if (TCG_TARGET_HAS_v128 && oprsz % 16 == 0 && oprsz / 16 <= 4
               && tcg_can_emit_vec_op(uzp_op, TCG_TYPE_V128, vece)) {
        type = TCG_TYPE_V128;
        step = 16;
        n = oprsz / 16;
    } else if (TCG_TARGET_HAS_v64 && oprsz % 8 == 0 && oprsz / 8 <= 4
               && tcg_can_emit_vec_op(uzp_op, TCG_TYPE_V64, vece)) {
        type = TCG_TYPE_V64;
        step = 8;
        n = oprsz / 8;
    } else {
        goto do_ool;
    }

    for (i = 0; i < n; ++i) {
        t[i] = tcg_temp_new_vec(type);
        tcg_gen_ld_vec(t[i], cpu_env, aofs + i * step);
    }
    for (i = 0; i < n; ++i) {
        t[n + i] = tcg_temp_new_vec(type);
        tcg_gen_ld_vec(t[n + i], cpu_env, bofs + i * step);
    }
    for (i = 0; i < n; ++i) {
        if (odd) {
            tcg_gen_uzpo_vec(vece, t[2 * i], t[2 * i], t[2 * i + 1]);
        } else {
            tcg_gen_uzpe_vec(vece, t[2 * i], t[2 * i], t[2 * i + 1]);
        }
        tcg_gen_st_vec(t[2 * i], cpu_env, dofs + i * step);
        tcg_temp_free_vec(t[2 * i]);
        tcg_temp_free_vec(t[2 * i + 1]);
    }
    if (oprsz < maxsz) {
        expand_clr(dofs + oprsz, maxsz - oprsz);
    }
    return;

 do_ool:
    tcg_gen_gvec_3_ool(dofs, aofs, bofs, oprsz, maxsz,
                       (1 << vece) * odd, uzp_fns[vece]);
}

void tcg_gen_gvec_uzpe(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t bofs, uint32_t oprsz, uint32_t maxsz)
{
    do_uzp(vece, dofs, aofs, bofs, oprsz, maxsz, false);
}

void tcg_gen_gvec_uzpo(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t bofs, uint32_t oprsz, uint32_t maxsz)
{
    do_uzp(vece, dofs, aofs, bofs, oprsz, maxsz, true);
}

static void gen_trne8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    uint64_t m = 0x00ff00ff00ff00ffull;
    tcg_gen_andi_i64(a, a, m);
    tcg_gen_andi_i64(b, b, m);
    tcg_gen_shli_i64(b, b, 8);
    tcg_gen_or_i64(d, a, b);
}

static void gen_trne16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    uint64_t m = 0x0000ffff0000ffffull;
    tcg_gen_andi_i64(a, a, m);
    tcg_gen_andi_i64(b, b, m);
    tcg_gen_shli_i64(b, b, 16);
    tcg_gen_or_i64(d, a, b);
}

static void gen_trne32_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_deposit_i64(d, a, b, 32, 32);
}

void tcg_gen_gvec_trne(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g[4] = {
        { .fni8 = gen_trne8_i64,
          .fniv = tcg_gen_trne_vec,
          .fno = gen_helper_gvec_trn8,
          .opc = INDEX_op_trne_vec,
          .vece = MO_8 },
        { .fni8 = gen_trne16_i64,
          .fniv = tcg_gen_trne_vec,
          .fno = gen_helper_gvec_trn16,
          .opc = INDEX_op_trne_vec,
          .vece = MO_16 },
        { .fni8 = gen_trne32_i64,
          .fniv = tcg_gen_trne_vec,
          .fno = gen_helper_gvec_trn32,
          .opc = INDEX_op_trne_vec,
          .vece = MO_32 },
        { .fniv = tcg_gen_trne_vec,
          .fno = gen_helper_gvec_trn64,
          .opc = INDEX_op_trne_vec,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g[vece]);
}

static void gen_trno8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    uint64_t m = 0xff00ff00ff00ff00ull;
    tcg_gen_andi_i64(a, a, m);
    tcg_gen_andi_i64(b, b, m);
    tcg_gen_shri_i64(a, a, 8);
    tcg_gen_or_i64(d, a, b);
}

static void gen_trno16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    uint64_t m = 0xffff0000ffff0000ull;
    tcg_gen_andi_i64(a, a, m);
    tcg_gen_andi_i64(b, b, m);
    tcg_gen_shri_i64(a, a, 16);
    tcg_gen_or_i64(d, a, b);
}

static void gen_trno32_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_shri_i64(a, a, 32);
    tcg_gen_deposit_i64(d, b, a, 0, 32);
}

void tcg_gen_gvec_trno(unsigned vece, uint32_t dofs, uint32_t aofs,
                       uint32_t bofs, uint32_t opsz, uint32_t maxsz)
{
    static const GVecGen3 g[4] = {
        { .fni8 = gen_trno8_i64,
          .fniv = tcg_gen_trno_vec,
          .fno = gen_helper_gvec_trn8,
          .opc = INDEX_op_trno_vec,
          .data = 1,
          .vece = MO_8 },
        { .fni8 = gen_trno16_i64,
          .fniv = tcg_gen_trno_vec,
          .fno = gen_helper_gvec_trn16,
          .opc = INDEX_op_trno_vec,
          .data = 2,
          .vece = MO_16 },
        { .fni8 = gen_trno32_i64,
          .fniv = tcg_gen_trno_vec,
          .fno = gen_helper_gvec_trn32,
          .opc = INDEX_op_trno_vec,
          .data = 4,
          .vece = MO_32 },
        { .fniv = tcg_gen_trno_vec,
          .fno = gen_helper_gvec_trn64,
          .opc = INDEX_op_trno_vec,
          .data = 8,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3(dofs, aofs, bofs, opsz, maxsz, &g[vece]);
}

/* Expand OPSZ bytes worth of three-operand operations using i32 elements.  */
static void expand_cmp_i32(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                           uint32_t opsz, TCGCond cond)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    uint32_t i;

    for (i = 0; i < opsz; i += 4) {
        tcg_gen_ld_i32(t0, cpu_env, aofs + i);
        tcg_gen_ld_i32(t1, cpu_env, bofs + i);
        tcg_gen_setcond_i32(cond, t0, t0, t1);
        tcg_gen_neg_i32(t0, t0);
        tcg_gen_st_i32(t0, cpu_env, dofs + i);
    }
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
}

static void expand_cmp_i64(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                           uint32_t opsz, TCGCond cond)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    uint32_t i;

    for (i = 0; i < opsz; i += 8) {
        tcg_gen_ld_i64(t0, cpu_env, aofs + i);
        tcg_gen_ld_i64(t1, cpu_env, bofs + i);
        tcg_gen_setcond_i64(cond, t0, t0, t1);
        tcg_gen_neg_i64(t0, t0);
        tcg_gen_st_i64(t0, cpu_env, dofs + i);
    }
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t0);
}

static void expand_cmp_vec(unsigned vece, uint32_t dofs, uint32_t aofs,
                           uint32_t bofs, uint32_t opsz, uint32_t tysz,
                           TCGType type, TCGCond cond)
{
    TCGv_vec t0 = tcg_temp_new_vec(type);
    TCGv_vec t1 = tcg_temp_new_vec(type);
    uint32_t i;

    for (i = 0; i < opsz; i += tysz) {
        tcg_gen_ld_vec(t0, cpu_env, aofs + i);
        tcg_gen_ld_vec(t1, cpu_env, bofs + i);
        tcg_gen_cmp_vec(cond, vece, t0, t0, t1);
        tcg_gen_st_vec(t0, cpu_env, dofs + i);
    }
    tcg_temp_free_vec(t1);
    tcg_temp_free_vec(t0);
}

void tcg_gen_gvec_cmp(TCGCond cond, unsigned vece, uint32_t dofs,
                      uint32_t aofs, uint32_t bofs,
                      uint32_t oprsz, uint32_t maxsz)
{
    static gen_helper_gvec_3 * const eq_fn[4] = {
        gen_helper_gvec_eq8, gen_helper_gvec_eq16,
        gen_helper_gvec_eq32, gen_helper_gvec_eq64
    };
    static gen_helper_gvec_3 * const ne_fn[4] = {
        gen_helper_gvec_ne8, gen_helper_gvec_ne16,
        gen_helper_gvec_ne32, gen_helper_gvec_ne64
    };
    static gen_helper_gvec_3 * const lt_fn[4] = {
        gen_helper_gvec_lt8, gen_helper_gvec_lt16,
        gen_helper_gvec_lt32, gen_helper_gvec_lt64
    };
    static gen_helper_gvec_3 * const le_fn[4] = {
        gen_helper_gvec_le8, gen_helper_gvec_le16,
        gen_helper_gvec_le32, gen_helper_gvec_le64
    };
    static gen_helper_gvec_3 * const ltu_fn[4] = {
        gen_helper_gvec_ltu8, gen_helper_gvec_ltu16,
        gen_helper_gvec_ltu32, gen_helper_gvec_ltu64
    };
    static gen_helper_gvec_3 * const leu_fn[4] = {
        gen_helper_gvec_leu8, gen_helper_gvec_leu16,
        gen_helper_gvec_leu32, gen_helper_gvec_leu64
    };
    gen_helper_gvec_3 *fn;
    uint32_t tmp;

    check_size_align(oprsz, maxsz, dofs | aofs | bofs);
    check_overlap_3(dofs, aofs, bofs, maxsz);

    if (cond == TCG_COND_NEVER || cond == TCG_COND_ALWAYS) {
        tcg_gen_gvec_dup32i(dofs, oprsz, maxsz, -(cond == TCG_COND_ALWAYS));
        return;
    }

    /* Quick check for sizes we won't support inline.  */
    if (oprsz > MAX_UNROLL * 32 || maxsz > MAX_UNROLL * 32) {
        goto do_ool;
    }

    /* Recall that ARM SVE allows vector sizes that are not a power of 2.
       Expand with successively smaller host vector sizes.  The intent is
       that e.g. oprsz == 80 would be expanded with 2x32 + 1x16.  */
    /* ??? For maxsz > oprsz, the host may be able to use an op-sized
       operation, zeroing the balance of the register.  We can then
       use a cl-sized store to implement the clearing without an extra
       store operation.  This is true for aarch64 and x86_64 hosts.  */

    if (TCG_TARGET_HAS_v256 && check_size_impl(oprsz, 32)
        && tcg_can_emit_vec_op(INDEX_op_cmp_vec, TCG_TYPE_V256, vece)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 32);
        expand_cmp_vec(vece, dofs, aofs, bofs, done, 32, TCG_TYPE_V256, cond);
        dofs += done;
        aofs += done;
        bofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (TCG_TARGET_HAS_v128 && check_size_impl(oprsz, 16)
        && tcg_can_emit_vec_op(INDEX_op_cmp_vec, TCG_TYPE_V128, vece)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 16);
        expand_cmp_vec(vece, dofs, aofs, bofs, done, 16, TCG_TYPE_V128, cond);
        dofs += done;
        aofs += done;
        bofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (check_size_impl(oprsz, 8)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 8);
        if (TCG_TARGET_HAS_v64
            && (TCG_TARGET_REG_BITS == 32 || vece != MO_64)
            && tcg_can_emit_vec_op(INDEX_op_cmp_vec, TCG_TYPE_V64, vece)) {
            expand_cmp_vec(vece, dofs, aofs, bofs, done, 8, TCG_TYPE_V64, cond);
        } else if (vece == MO_64) {
            expand_cmp_i64(dofs, aofs, bofs, done, cond);
        } else {
            done = 0;
        }
        dofs += done;
        aofs += done;
        bofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (vece == MO_32 && check_size_impl(oprsz, 4)) {
        uint32_t done = QEMU_ALIGN_DOWN(oprsz, 4);
        expand_cmp_i32(dofs, aofs, bofs, done, cond);
        dofs += done;
        aofs += done;
        bofs += done;
        oprsz -= done;
        maxsz -= done;
    }

    if (oprsz == 0) {
        if (maxsz != 0) {
            expand_clr(dofs, maxsz);
        }
        return;
    }

 do_ool:
    switch (cond) {
    case TCG_COND_EQ:
        fn = eq_fn[vece];
        break;
    case TCG_COND_NE:
        fn = ne_fn[vece];
        break;
    case TCG_COND_GT:
        tmp = aofs, aofs = bofs, bofs = tmp;
        /* fallthru */
    case TCG_COND_LT:
        fn = lt_fn[vece];
        break;
    case TCG_COND_GE:
        tmp = aofs, aofs = bofs, bofs = tmp;
        /* fallthru */
    case TCG_COND_LE:
        fn = le_fn[vece];
        break;
    case TCG_COND_GTU:
        tmp = aofs, aofs = bofs, bofs = tmp;
        /* fallthru */
    case TCG_COND_LTU:
        fn = ltu_fn[vece];
        break;
    case TCG_COND_GEU:
        tmp = aofs, aofs = bofs, bofs = tmp;
        /* fallthru */
    case TCG_COND_LEU:
        fn = leu_fn[vece];
        break;
    default:
        g_assert_not_reached();
    }
    tcg_gen_gvec_3_ool(dofs, aofs, bofs, oprsz, maxsz, 0, fn);
}
