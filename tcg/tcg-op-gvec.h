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

/*
 * "Generic" vectors.  All operands are given as offsets from ENV,
 * and therefore cannot also be allocated via tcg_global_mem_new_*.
 * OPRSZ is the byte size of the vector upon which the operation is performed.
 * MAXSZ is the byte size of the full vector; bytes beyond OPSZ are cleared.
 *
 * All sizes must be 8 or any multiple of 16.
 * When OPRSZ is 8, the alignment may be 8, otherwise must be 16.
 * Operands may completely, but not partially, overlap.
 */

/* Expand a call to a gvec-style helper, with pointers to two vector
   operands, and a descriptor (see tcg-gvec-desc.h).  */
typedef void (gen_helper_gvec_2)(TCGv_ptr, TCGv_ptr, TCGv_i32);
void tcg_gen_gvec_2_ool(uint32_t dofs, uint32_t aofs,
                        uint32_t oprsz, uint32_t maxsz, int32_t data,
                        gen_helper_gvec_2 *fn);

/* Similarly, passing an extra pointer (e.g. env or float_status).  */
typedef void (gen_helper_gvec_2_ptr)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
void tcg_gen_gvec_2_ptr(uint32_t dofs, uint32_t aofs,
                        TCGv_ptr ptr, uint32_t oprsz, uint32_t maxsz,
                        int32_t data, gen_helper_gvec_2_ptr *fn);

/* Similarly, with three vector operands.  */
typedef void (gen_helper_gvec_3)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
void tcg_gen_gvec_3_ool(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                        uint32_t oprsz, uint32_t maxsz, int32_t data,
                        gen_helper_gvec_3 *fn);

typedef void (gen_helper_gvec_3_ptr)(TCGv_ptr, TCGv_ptr, TCGv_ptr,
                                     TCGv_ptr, TCGv_i32);
void tcg_gen_gvec_3_ptr(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                        TCGv_ptr ptr, uint32_t oprsz, uint32_t maxsz,
                        int32_t data, gen_helper_gvec_3_ptr *fn);

/* Expand a gvec operation.  Either inline or out-of-line depending on
   the actual vector size and the operations supported by the host.  */
typedef struct {
    /* Expand inline as a 64-bit or 32-bit integer.
       Only one of these will be non-NULL.  */
    void (*fni8)(TCGv_i64, TCGv_i64);
    void (*fni4)(TCGv_i32, TCGv_i32);
    /* Expand inline with a host vector type.  */
    void (*fniv)(TCGv_vec, TCGv_vec);
    /* Expand out-of-line helper w/descriptor.  */
    gen_helper_gvec_2 *fno;
    /* Prefer i64 to v64.  */
    bool prefer_i64;
} GVecGen2;

typedef struct {
    /* Expand inline as a 64-bit or 32-bit integer.
       Only one of these will be non-NULL.  */
    void (*fni8)(TCGv_i64, TCGv_i64, TCGv_i64);
    void (*fni4)(TCGv_i32, TCGv_i32, TCGv_i32);
    /* Expand inline with a host vector type.  */
    void (*fniv)(TCGv_vec, TCGv_vec, TCGv_vec);
    /* Expand out-of-line helper w/descriptor.  */
    gen_helper_gvec_3 *fno;
    /* Prefer i64 to v64.  */
    bool prefer_i64;
    /* Load dest as a 3rd source operand.  */
    bool load_dest;
} GVecGen3;

void tcg_gen_gvec_2(uint32_t dofs, uint32_t aofs,
                    uint32_t opsz, uint32_t clsz, const GVecGen2 *);
void tcg_gen_gvec_3(uint32_t dofs, uint32_t aofs, uint32_t bofs,
                    uint32_t opsz, uint32_t clsz, const GVecGen3 *);

/* Expand a specific vector operation.  */

#define DEF(X) \
    void tcg_gen_gvec_##X(uint32_t dofs, uint32_t aofs, \
                          uint32_t opsz, uint32_t clsz)

DEF(mov);
DEF(not);
DEF(neg8);
DEF(neg16);
DEF(neg32);
DEF(neg64);

#undef DEF
#define DEF(X) \
    void tcg_gen_gvec_##X(uint32_t dofs, uint32_t aofs, uint32_t bofs, \
                          uint32_t opsz, uint32_t clsz)

DEF(add8);
DEF(add16);
DEF(add32);
DEF(add64);

DEF(sub8);
DEF(sub16);
DEF(sub32);
DEF(sub64);

DEF(and);
DEF(or);
DEF(xor);
DEF(andc);
DEF(orc);

#undef DEF

/*
 * 64-bit vector operations.  Use these when the register has been allocated
 * with tcg_global_mem_new_i64, and so we cannot also address it via pointer.
 * OPRSZ = MAXSZ = 8.
 */

void tcg_gen_vec_neg8_i64(TCGv_i64 d, TCGv_i64 a);
void tcg_gen_vec_neg16_i64(TCGv_i64 d, TCGv_i64 a);
void tcg_gen_vec_neg32_i64(TCGv_i64 d, TCGv_i64 a);

void tcg_gen_vec_add8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
void tcg_gen_vec_add16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
void tcg_gen_vec_add32_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);

void tcg_gen_vec_sub8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
void tcg_gen_vec_sub16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
void tcg_gen_vec_sub32_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
