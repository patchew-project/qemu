/*
 * Test for VSTART set to overflow VL
 *
 * TCG vector instructions should call VSTART_CHECK_EARLY_EXIT() to check
 * this case, otherwise memory addresses can underflow and misbehave or
 * crash QEMU.
 *
 * TODO: Add stores and other instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <riscv_vector.h>

#define VSTART_OVERFLOW_TEST(insn)                \
({                                                \
    uint8_t vmem[64] = { 0 };                     \
    uint64_t vstart;                              \
    asm volatile("                           \r\n \
    # Set VL=52 and VSTART=56                \r\n \
    li          t0, 52                       \r\n \
    vsetvli     x0, t0, e8, m4, ta, ma       \r\n \
    li          t0, 56                       \r\n \
    csrrw       x0, vstart, t0               \r\n \
    li          t1, 64                       \r\n \
    " insn "                                 \r\n \
    csrr        %0, vstart                   \r\n \
    " : "=r"(vstart), "+A"(vmem) :: "t0", "t1", "v24", "memory"); \
    vstart;                                       \
})

int run_vstart_overflow_tests()
{
    /*
     * An implementation is permitted to raise an illegal instruction
     * exception when executing a vector instruction if vstart is set to a
     * value that could not be produced by the execution of that instruction
     * with the same vtype. If TCG is changed to do this, then this test
     * could be updated to handle the SIGILL.
     */
    if (VSTART_OVERFLOW_TEST("vl1re16.v    v24, %1")) {
        return 1;
    }

    if (VSTART_OVERFLOW_TEST("vs1r.v       v24, %1")) {
        return 1;
    }

    if (VSTART_OVERFLOW_TEST("vle16.v      v24, %1")) {
        return 1;
    }

    if (VSTART_OVERFLOW_TEST("vse16.v      v24, %1")) {
        return 1;
    }

    if (VSTART_OVERFLOW_TEST("vluxei8.v    v24, %1, v20")) {
        return 1;
    }

    if (VSTART_OVERFLOW_TEST("vlse16.v     v24, %1, t1")) {
        return 1;
    }

    if (VSTART_OVERFLOW_TEST("vlseg2e8.v  v24, %1")) {
        return 1;
    }

    return 0;
}

int main()
{
    return run_vstart_overflow_tests();
}
