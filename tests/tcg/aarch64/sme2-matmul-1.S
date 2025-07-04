/*
 * SPDX-FileCopyrightText: Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copied from
 * https://learn.arm.com/learning-paths/cross-platform/multiplying-matrices-with-sme2/
 *
 * and modified for testing with qemu-aarch64.
 */

    .text
    .cfi_sections .debug_frame  // put stack frame info into .debug_frame instead of .eh_frame

    .global ool_svcntsw
    .type ool_svcntsw, "function"
    .cfi_startproc
ool_svcntsw:
    rdsvl  x0, #1
    lsr    x0, x0, #1
    ret
    .cfi_endproc
    .size   ool_svcntsw, .-ool_svcntsw

    .global preprocess_l_asm
    .type preprocess_l_asm, "function"
    .cfi_startproc

preprocess_l_asm:
    // preprocess_l_asm(uint64_t nbr, uint64_t nbc, const float * restrict a, float * a_mod);
    // x0 : nbr
    // x1 : nbc
    // x2 : &a
    // x3 : &a_mod
    // x4 : SVLs (=cntw)
    // x5 : Exit condition for inner loop
    // x6 : a_ptr
    // x7 : Outer loop counter
    // x8 : a_base
    // x9 : a_mod store base address
    // x10: 32b Tile0 store end pointer
    // x11: SVLs*nbc
    // x12: Load/Store loop counter
    // x13: 32b Tile1 store end pointer
    // x14: 2*nbc
    // x15: 3*nbc
    // x16: 32b tile size

// Assumptions:
// nbr in matLeft (M): any
// nbc in matLeft, nbr in matRight (K): any K > 2
// nbc in matRight (N): any
//
// Left matrix re-arrangement:
// Block of SVLs rows is transposed and contiguously stored.
// Then the same transformation is applied to remaining blocks of SVLs rows.
// The last block of rows is zero-padded to SVLs rows, if applicable.

    smstart

// constants
    cntw    x4                      // SVLs
    mul     x11, x4, x1             // SVLs*nbc
    lsl     x14, x1, #1             // 2*nbc
    add     x15, x14, x1            // 3*nbc

    mul     x16, x4, x4             // SVLs*SVLs

    mov     x7, #0
    whilelt p0.s, x7, x0            // Tile predicate (M dimension)

.Loop_outer:
    mov     x8, x2                  // a load base address
    mov     x9, x3                  // a_mod store base address
    add     x5,  x2, x1, lsl #2     // Exit condition for inner loop

    add     x10, x9 , x11, lsl #2   // 32b Tile0 store predicate condition
    sub     x13, x10, x16, lsl #2   // 32b Tile1 store predicate condition
    whilelt pn8.b, x8, x5, vlx2     // Tile predicate-as-counter (K dimension)

.Loop_inner:
    mov     x6, x8                  // a_ptr

    mov     w12, #0                 // Load_loop counter

.Load_loop:
    psel    pn10, pn8, p0.s[w12, 0]
    psel    pn11, pn8, p0.s[w12, 1]
    psel    pn12, pn8, p0.s[w12, 2]
    psel    pn13, pn8, p0.s[w12, 3]
    ld1w    {z20.s, z28.s}, pn10/z, [x6]                // Load 2 row vectors from a_ptr
    ld1w    {z21.s, z29.s}, pn11/z, [x6, x1,  lsl #2]   // Load " "   "       from a_ptr + nbc
    ld1w    {z22.s, z30.s}, pn12/z, [x6, x14, lsl #2]   // Load " "   "       from a_ptr + nbc*2
    ld1w    {z23.s, z31.s}, pn13/z, [x6, x15, lsl #2]   // Load " "   "       from a_ptr + nbc*3
    mova    za0h.s[w12, 0:3], {z20.s-z23.s}
    mova    za1h.s[w12, 0:3], {z28.s-z31.s}

    add     x6, x6, x1, lsl #4      // a_ptr+=4*nbc FP32 elms [Bytes]
    add     w12, w12, #4            // increment counter
    cmp     w12, w4
    b.mi    .Load_loop

    mov     w12, #0                 // Store_loop counter

.Store_loop:
    whilelt pn10.b, x9, x10, vlx4
    whilelt pn11.b, x9, x13, vlx4
    mova    {z0.s-z3.s}, za0v.s[w12, 0:3]
    mova    {z4.s-z7.s}, za1v.s[w12, 0:3]
    st1w    {z0.s-z3.s}, pn10, [x9]               // Store 4 col vectors to a_mod
    st1w    {z4.s-z7.s}, pn11, [x9, x16, lsl #2]  // Store 4 col vectors to a_mod + SVLs*SVLs
    addvl   x9, x9, #4              // a_mod += 4*SVLb [Bytes]
    add     w12, w12, #4            // increment counter
    cmp     w12, w4
    b.mi    .Store_loop

    add     x9, x9, x16, lsl #2
    addvl   x8, x8, #2              // a_base += 2*SVLb [Bytes]
    whilelt pn8.b, x8, x5, vlx2
    b.first .Loop_inner

    add     x3, x3, x11, lsl #2     // &a_mod += SVLs*nbc FP32 elms [Bytes]
    add     x2, x2, x11, lsl #2     // &a += SVLs*nbc FP32 elms [Bytes]
    incw    x7

    whilelt p0.s, x7, x0
    b.first .Loop_outer

    smstop

    ret
    .cfi_endproc
    .size   preprocess_l_asm, .-preprocess_l_asm

    .global matmul_asm_impl
    .type matmul_asm_impl, "function"
    .cfi_startproc

matmul_asm_impl:
    // matmul_asm_impl(M, K, N, matLeft, matRight, matResult_opt);
    // x0 : M
    // x1 : K, lda
    // x2 : N, ldc, ldb
    // x3 : &matLeft
    // x4 : &matRight
    // x5 : &matResult_opt
    // x6 : SVLs-2
    // x7 : a_ptr pointer
    // x8 : a_ptr end address
    // x9 : c_base pointer
    // x10: c_ptr0 pointer
    // x11: Exit condition for N loop
    // x12: M loop counter
    // x13: Store loop counter
    // x14: Predicate select index
    // x15: Exit condition for K loop
    // x16: b_base pointer
    // x17: b_ptr pointer
    // x18: (SVLs+1)*ldc
    // x19: ldb + SVLs
    // x20: SVLs*lda + SVLs
    // x21: c_ptr1 pointer
    // x22: SVLs*lda
    // x23: SVLs*ldc

// Assumptions:
// nbr in matLeft (M): any
// nbc in matLeft, nbr in matRight (K): any K > 2
// nbc in matRight (N): any
//
// Left matrix is pre-arranged.
//
// 32-bit accumulator mapping with 2x2 tiles processing

    stp     x19, x20, [sp, #-48]!
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]

    smstart

// constants
    cntw    x6                      // SVLs
    mul     x22, x6, x1             // SVLs*lda
    mul     x23, x6, x2             // SVLs*ldc
    add     x18, x23, x2            // SVLs*ldc + ldc
    add     x11, x4, x2, lsl #2     // Exit condition for N loop
    mov     x12, #0
    cntb    x6                      // SVLb
    mov     x14, #0
    ptrue   pn10.b                  // Predicate as counter for SME2 VLx2 (a_ptr loads)
    whilelt pn8.s, x12, x0, vlx2    // tiles predicate (M dimension)
    sub     w6, w6, #8              // SVLb-8

.Loop_M:
    // Extracting tile 0/1 and tile 2/3 predicates (M dimension) from vlx2 predicate.
    pext    { p2.s, p3.s }, pn8[0]
    mov     x16, x4                 // b_base
    mov     x9, x5                  // c_base

    whilelt pn9.b, x16, x11, vlx2   // tiles predicate (N dimension)

.Loop_N:
    mov     x7, x3                  // a_ptr = a_base
    mov     x17, x16                // b_ptr = b_base
    mov     x10, x9                 // c_ptr0 = c_base

    // Extracting tile 0/2 and tile 1/3 predicates (N dimension) from vlx2 predicate.
    pext    { p0.b, p1.b }, pn9[0]

    add     x8, x3, x22, lsl #2     // a_base + SVLs*lda FP32 elms [Bytes]
    addvl   x15, x8, #-1            // Exit condition for K loop
    ld1w    {z1.s},  p2/z,   [x7]   // Load 1st vector from a_ptr

    zero    {za}
    ld1w    {z2.s-z3.s},  pn9/z,   [x17]  // Load 2 vectors from b_ptr

    fmopa   za0.s,  p2/m,   p0/m,   z1.s,   z2.s  // ZA0 += 1st a_ptr vector OP 1st b_ptr vector
    ld1w    {z5.s},  p3/z,   [x7, x22, lsl #2]    // Load 2nd vector from a_ptr
    addvl   x7, x7, #1                            // a_ptr += SVLb [Bytes]

.Loop_K:
    fmopa   za2.s,  p3/m,   p0/m,   z5.s,   z2.s       // ZA2 += 2nd a_ptr vector OP 1st b_ptr vector

    fmopa   za1.s,  p2/m,   p1/m,   z1.s,   z3.s       // ZA1 += 1st a_ptr vector OP 2nd b_ptr vector
    ld1w    {z0.s-z1.s},  pn10/z,   [x7]               // Load next 2 vectors from a_ptr

    fmopa   za3.s,  p3/m,   p1/m,   z5.s,   z3.s       // ZA3 += 2nd a_ptr vector OP 2nd b_ptr vector
    ld1w    {z6.s-z7.s},  pn9/z,   [x17, x2, lsl #2]   // Load next 2 vectors from b_ptr

    fmopa   za0.s,  p2/m,   p0/m,   z0.s,   z6.s       // ZA0 += 1st a_ptr vector OP 1st b_ptr vector
    psel    pn11, pn10, p3.s[w14, 0]                   // Select predicate-as-counter
    ld1w    {z4.s-z5.s},  pn11/z,   [x7, x22, lsl #2]  // Load next 2 vectors from a_ptr

    fmopa   za2.s,  p3/m,   p0/m,   z4.s,   z6.s       // ZA2 += 2nd a_ptr vector OP 1st b_ptr vector
    add     x17, x17, x2, lsl #3                       // b_ptr += 2*ldb FP32 elms [Bytes]

    fmopa   za1.s,  p2/m,   p1/m,   z0.s,   z7.s       // ZA1 += 1st a_ptr vector OP 2nd b_ptr vector

    fmopa   za3.s,  p3/m,   p1/m,   z4.s,   z7.s       // ZA3 += 2nd a_ptr vector OP 2nd b_ptr vector
    ld1w    {z2.s-z3.s},  pn9/z,   [x17]               // Load next 2 vectors from b_ptr

    fmopa   za0.s,  p2/m,   p0/m,   z1.s,   z2.s       // ZA0 += 1st a_ptr vector OP 1st b_ptr vector
    addvl   x7, x7, #2                                 // a_ptr += 2*SVLb [Bytes]

    cmp     x7, x15
    b.mi    .Loop_K

    fmopa   za2.s,  p3/m,   p0/m,   z5.s,   z2.s       // ZA2 += 2nd a_ptr vector OP 1st b_ptr vector

    fmopa   za1.s,  p2/m,   p1/m,   z1.s,   z3.s       // ZA1 += 1st a_ptr vector OP 2nd b_ptr vector

    fmopa   za3.s,  p3/m,   p1/m,   z5.s,   z3.s       // ZA3 += 2nd a_ptr vector OP 2nd b_ptr vector
    add     x17, x17, x2, lsl #2                       // b_ptr += 2*ldb FP32 elms [Bytes]

    cmp     x7, x8
    b.pl    .Ktail_end

.Ktail_start:
    ld1w    {z1.s},  p2/z,   [x7]
    ld1w    {z2.s-z3.s},  pn9/z,   [x17]

    fmopa   za0.s,  p2/m,   p0/m,   z1.s,   z2.s
    ld1w    {z5.s},  p3/z,   [x7, x22, lsl #2]

    fmopa   za2.s,  p3/m,   p0/m,   z5.s,   z2.s

    fmopa   za1.s,  p2/m,   p1/m,   z1.s,   z3.s

    fmopa   za3.s,  p3/m,   p1/m,   z5.s,   z3.s

.Ktail_end:
    mov     w13, #0
    psel    pn11, pn9, p2.b[w13, 0]
    psel    pn12, pn9, p3.b[w13, 0]
    // Move from ZA tiles to vectors: z0 = za0h[1], z1 = za1h[1], z2 = za2h[1], z3 = za3h[1]
    mova    { z0.b-z3.b }, za0h.b[w13, 0:3]
    st1w    { z0.s-z1.s }, pn11, [x10]                  // Store to c_ptr0
    st1w    { z2.s-z3.s }, pn12, [x10, x23, lsl #2]     // Store to c_ptr0 + SVLs*ldc
.Loop_store_ZA:
    psel    pn11, pn9, p2.b[w13, 4]
    psel    pn12, pn9, p3.b[w13, 4]
    mova    { z0.b-z3.b }, za0h.b[w13, 4:7]
    st1w    { z0.s-z1.s }, pn11, [x10, x2,  lsl #2]      // Store to c_ptr0 + ldc
    st1w    { z2.s-z3.s }, pn12, [x10, x18,  lsl #2]     // Store to c_ptr0 + (SVLs+1)*ldc

    add     x10, x10, x2, lsl #3    // c_ptr0 += 2*ldc FP32 elms [Bytes]
    add     w13, w13, #8

    psel    pn11, pn9, p2.b[w13, 0]
    psel    pn12, pn9, p3.b[w13, 0]
    mova    { z0.b-z3.b }, za0h.b[w13, 0:3]
    st1w    { z0.s-z1.s }, pn11, [x10]                  // Store to c_ptr0
    st1w    { z2.s-z3.s }, pn12, [x10, x23, lsl #2]     // Store to c_ptr0 + SVLs*ldc
    cmp     w13, w6
    b.mi    .Loop_store_ZA

    psel    pn11, pn9, p2.b[w13, 4]
    psel    pn12, pn9, p3.b[w13, 4]
    mova    { z0.b-z3.b }, za0h.b[w13, 4:7]
    st1w    { z0.s-z1.s }, pn11, [x10, x2,  lsl #2]      // Store to c_ptr0 + ldc
    st1w    { z2.s-z3.s }, pn12, [x10, x18,  lsl #2]     // Store to c_ptr0 + (SVLs+1)*ldc

    addvl   x9, x9, #2
    addvl   x16, x16, #2            // b_base += 2*SVLb [Bytes]
    whilelt pn9.b, x16, x11, vlx2   // tile predicate (N dimension)
    b.first .Loop_N

    add     x3, x3, x22, lsl #3     // a_base += 2*SVLs*lda FP32 elms [Bytes]
    add     x5, x5, x23, lsl #3     // c_base += 2*SVLs*ldc FP32 elms [Bytes]
    incw    x12, all, mul #2        // M loop counter += 2* SVLs
    whilelt pn8.s, x12, x0, vlx2    // tiles predicate (M dimension)
    b.first    .Loop_M

    smstop

    ldp     x23, x24, [sp, #32]
    ldp     x21, x22, [sp, #16]
    ldp     x19, x20, [sp], #48

    ret
    .cfi_endproc
    .size   matmul_asm_impl, .-matmul_asm_impl
