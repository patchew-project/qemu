/*
 * RISC-V vector load/store page crossing test
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .section .rodata
    // Page aligned so the within-page cases never cross a boundary
    .set MAX_PAGE_SIZE, 65536
    .balign MAX_PAGE_SIZE
src_data:
    .byte 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
    .byte 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
    .set NUM_BYTES, . - src_data

    .balign MAX_PAGE_SIZE
pageA_start:
    // Fill the page, stopping 8 bytes short of the boundary
    .fill MAX_PAGE_SIZE - 8, 1, 0xfa
src_cross_data:
    .byte 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
pageB_start:
    .byte 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F
    .set NUM_CROSS_BYTES, . - src_cross_data

    .section .bss
    .balign MAX_PAGE_SIZE
dst_data:
    .space 16

    .balign MAX_PAGE_SIZE
    .space (MAX_PAGE_SIZE - 8)
dst_cross_data:
    .space 16

.option norelax
.option norvc

    .text
    .globl _start
_start:
    // Case 1: Load/store within a page. t3 is the exit code on failure
    li      t3, 1
    la      a0, src_data
    li      a1, NUM_BYTES

    // SEW=8, LMUL=1
    vsetvli t0, a1, e8, m1, ta, ma

    // Check the load will cover 16 bytes
    bne     t0, a1, fail

    // perform load
    vle8.v  v1, (a0)

    // Store vector content in memory
    la      a2, dst_data
    vse8.v  v1, (a2)

    // Compare first half
    ld      t1, 0(a0)
    ld      t2, 0(a2)
    bne     t1, t2, fail

    // Compare second half
    ld      t1, 8(a0)
    ld      t2, 8(a2)
    bne     t1, t2, fail

    // Case 2: Load straddles a page boundary
    li      t3, 2
    la      a0, src_cross_data
    li      a1, NUM_CROSS_BYTES

    // SEW=8, LMUL=1
    vsetvli t0, a1, e8, m1, ta, ma
    bne     t0, a1, fail

    // perform load
    vle8.v  v1, (a0)

    // Store vector content in memory
    la      a2, dst_data
    vse8.v  v1, (a2)

    // Compare first half
    ld      t1, 0(a0)
    ld      t2, 0(a2)
    bne     t1, t2, fail

    // Compare second half
    ld      t1, 8(a0)
    ld      t2, 8(a2)
    bne     t1, t2, fail

    // Case 3: Store straddles a page boundary
    li      t3, 3
    la      a0, src_data
    li      a1, NUM_BYTES

    // SEW=8, LMUL=1
    vsetvli t0, a1, e8, m1, ta, ma
    bne     t0, a1, fail

    // perform load
    vle8.v  v1, (a0)

    // Store vector content in memory
    la      a2, dst_cross_data
    vse8.v  v1, (a2)

    // Compare first half
    ld      t1, 0(a0)
    ld      t2, 0(a2)
    bne     t1, t2, fail

    // Compare second half
    ld      t1, 8(a0)
    ld      t2, 8(a2)
    bne     t1, t2, fail

pass:
    li      a0, 0
    j       exit

fail:
    mv      a0, t3

exit:
    li      a7, 93        // sys_exit
    ecall
