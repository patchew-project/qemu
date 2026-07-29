/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define ARR_SIZE 16
static const float in_arr[ARR_SIZE] = {
    1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0,
    9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0
};

static float out_arr[ARR_SIZE] = {
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};

static const unsigned long FPSCR_PR_BIT = 1UL << 19;
static void single_precision_mode(void)
{
    unsigned long fpscr;
    asm volatile("sts fpscr, %0" : "=r" (fpscr));
    fpscr &= ~FPSCR_PR_BIT;
    asm volatile("lds %0, fpscr" : : "r" (fpscr));
}

static void double_precision_mode(void)
{
    unsigned long fpscr;
    asm volatile("sts fpscr, %0" : "=r" (fpscr));
    fpscr |= FPSCR_PR_BIT;
    asm volatile("lds %0, fpscr" : : "r" (fpscr));
}

static void fill_arr(float *arr, float n)
{
    for (int i = 0; i < ARR_SIZE; i++) {
        arr[i] = n;
    }
}

/* uses fmov frm,frn instruction */
static int test_fmov_freg_freg(void)
{
    int pass = true;

    register float out asm ("fr0") = 0.0;
    asm volatile(
        "fldi1 fr1\n"
        "fmov fr1,%0\n"
        : "=f" (out)
    );

    if (out != 1.0) {
        fprintf(
            stderr,
            "fmov freg,freg failed. Expected %f, got %f\n", 1.0, out
        );
        pass = false;
    }

    return pass;
}

/* uses fmov frm,@rn instruction */
static int test_fmov_freg_addr(float *out)
{
    int pass = true;
    fill_arr(out, 0);

    register float *out_addr = out;
    asm volatile(
        "fldi1   fr0\n"
        "fmov.s  fr0,@%0\n"
        : : "r" (out_addr) : "memory"
    );

    if (out[0] != 1.0) {
        fprintf(
            stderr,
            "fmov freg,addr failed. Expected %f, got %f\n", 1.0, out[0]
        );
        pass = false;
    }

    fill_arr(out, 0);
    return pass;
}

/* uses fmov @rm,frn instruction */
static int test_fmov_addr_freg(const float *in)
{
    int pass = true;
    const register float *in_addr = in;
    register float out = 0.0;
    asm volatile(
        "fmov.s    @%1,%0\n"
        : "=f" (out) : "r" (in_addr)
    );

    if (out != 1.0) {
        fprintf(
            stderr,
            "fmov addr,freg failed. Expected %f, got %f\n", 1.0, out
        );
        pass = false;
    }

    return pass;
}

/* uses fmov frm,@(r0,rn) instruction */
static int test_fmov_freg_indirect_addr(float *out)
{
    int pass = true;
    fill_arr(out, 0.0);

    register float *out_addr = out;
    asm volatile(
        "mov      #4,r0\n"
        "fldi1    fr0\n"
        "fmov.s   fr0, @(r0, %0)\n"
        : : "r" (out_addr) : "memory"
    );

    if (out[1] != 1.0) {
        fprintf(
            stderr,
            "fmov freg,indirect_addr failed. Expected %f, got %f\n", 1.0, out[1]
        );
        pass = false;
    }

    fill_arr(out, 0.0);
    return pass;
}

/* uses fmov @(r0,rm),frn instruction */
static int test_fmov_indirect_addr_freg(const float *in)
{
    int pass = true;

    const register float *in_addr asm ("r1") = in;
    register float out asm ("fr0") = 0.0;
    asm volatile(
        "mov      #4,r0\n"
        "fmov.s   @(r0, %1), %0\n"
        : "=f" (out) : "r" (in_addr) : "memory"
    );

    if (out != in[1]) {
        fprintf(
            stderr,
            "fmov indirect_addr,freg failed. Expected %f, got %f\n", in[1], out
        );
        pass = false;
    }

    return pass;
}

/* uses fmov drm,drn instruction */
static int test_fmov_dreg_dreg(void)
{
    int pass = true;
    double_precision_mode();

    register double out asm ("dr0") = 0.0;
    asm volatile(
        "mov   #42,r0\n"
        "lds   r0,fpul\n"
        "float fpul,dr2\n"
        "fmov  dr2,%0\n"
        : "=f" (out)
    );

    if (out != 42.0) {
        fprintf(
            stderr,
            "fmov dreg,dreg failed. Expected %f, got %f\n", 42.0, out
        );
        pass = false;
    }

    single_precision_mode();
    return pass;
}

/* uses fmov drm,@rn instruction */
static int test_fmov_dreg_addr(float *out)
{
    int pass = true;
    fill_arr(out, -1.0);

    register float *out_addr = out;
    asm volatile(
        "fldi0 fr0\n"
        "fldi1 fr1\n"
        "fschg\n" /* Set fast pairs on */
        "fmov   dr0, @%0\n"
        "fschg\n" /* Set fast pairs off */
        : : "r" (out_addr) : "memory"
    );

    if (out[0] != 0.0) {
        fprintf(
            stderr,
            "fmov dreg,addr failed. " \
                "Expected element 0 to be %f, got %f\n", 0.0, out[0]
        );
        pass = false;
    }
    if (out[1] != 1.0) {
        fprintf(
            stderr,
            "fmov dreg,addr failed. " \
                "Expected element 1 to be %f, got %f\n", 1.0, out[1]
        );
        pass = false;
    }

    fill_arr(out, 0.0);
    return pass;
}

/* uses fmov @rm,drn instruction */
static int test_fmov_addr_dreg(const float *in)
{
    int pass = true;

    const register float *in_addr asm ("r1") = in;
    register float write_out_0 asm ("fr0") = 0.0;
    register float write_out_1 asm ("fr1") = 0.0;

    asm volatile(
        "fschg\n" /* Set fast pairs on */
        "fmov    @%2,dr0\n"
        "fschg\n" /* Set fast pairs off */
        : "=f" (write_out_0), "=f" (write_out_1) : "r" (in_addr) : "memory"
    );

    if (write_out_0 != in[0]) {
        fprintf(
            stderr,
            "fmov addr,dreg failed. " \
                "Expected element 0 to be %f, got %f\n", in[0], write_out_0
        );
        pass = false;
    }
    if (write_out_1 != in[1]) {
        fprintf(
            stderr,
            "fmov addr,dreg failed. " \
                "Expected element 1 to be %f, got %f\n", in[1], write_out_1
        );
        pass = false;
    }

    return pass;
}

/* uses fmov drm,@(r0,rn) instruction */
static int test_fmov_dreg_indirect_addr(float *out)
{
    int pass = true;
    fill_arr(out, -1.0);

    register float *out_addr = out;
    asm volatile(
        "mov      #8,r0\n"
        "fldi0    fr0\n"
        "fldi1    fr1\n"
        "fschg\n" /* Set fast pairs on */
        "fmov   dr0, @(r0, %0)\n"
        "fschg\n" /* Set fast pairs off */
        : : "r" (out_addr) : "memory"
    );

    if (out[2] != 0.0) {
        fprintf(
            stderr,
            "fmov dreg,indirect_addr failed. " \
                "Expected element 0 to be %f, got %f\n", 0.0, out[2]
        );
        pass = false;
    }

    if (out[3] != 1.0) {
        fprintf(
            stderr,
            "fmov dreg,indirect_addr failed. " \
                "Expected element 1 to be %f, got %f\n", 1.0, out[3]
        );
        pass = false;
    }

    fill_arr(out, 0.0);
    return pass;
}

/* uses fmov @(r0,rm),drn instruction */
static int test_fmov_indirect_addr_dreg(const float *in)
{
    int pass = true;

    const register float *in_addr asm ("r1") = in;
    register float write_out_0 asm ("fr0") = 0.0;
    register float write_out_1 asm ("fr1") = 0.0;
    asm volatile(
        "mov      #8,r0\n"
        "fschg\n" /* Set fast pairs on */
        "fmov     @(r0, %2), dr0\n"
        "fschg\n" /* Set fast pairs off */
        : "=f" (write_out_0), "=f" (write_out_1) : "r" (in_addr) : "memory"
    );

    if (write_out_0 != in[2]) {
        fprintf(
            stderr,
            "fmov indirect_addr,dreg failed. " \
                "Expected element 0 to be %f, got %f\n", in[2], write_out_0
        );
        pass = false;
    }

    if (write_out_1 != in[3]) {
        fprintf(
            stderr,
            "fmov indirect_addr,dreg failed. " \
                "Expected element 1 to be %f, got %f\n", in[3], write_out_1
        );
        pass = false;
    }

    return pass;
}

/*
 * reads in data from `in` using fast pairs
 * writes data to `out` using slow singles
 * uses fmov @rm+,drn instruction
 * uses fmov frm,@-rn instruction
 */
static int test_fmov_fast_in_slow_out(const float *in, float *out)
{
    int pass = true;
    fill_arr(out, 0.0);

    const register float *in_addr = in;
    register float *out_addr = out;
    asm volatile(
        "fschg\n" /* Set fast pairs on */
        "fmov @%0+,dr0\n"
        "fmov @%0+,dr2\n"
        "fmov @%0+,dr4\n"
        "fmov @%0+,dr6\n"
        "fmov @%0+,dr8\n"
        "fmov @%0+,dr10\n"
        "fmov @%0+,dr12\n"
        "fmov @%0+,dr14\n"
        "fschg\n" /* Set fast pairs off */

        "add #64,%1\n" /* Start at the back and work to the front */
        "fmov fr15,@-%1\n"
        "fmov fr14,@-%1\n"
        "fmov fr13,@-%1\n"
        "fmov fr12,@-%1\n"
        "fmov fr11,@-%1\n"
        "fmov fr10,@-%1\n"
        "fmov fr9,@-%1\n"
        "fmov fr8,@-%1\n"
        "fmov fr7,@-%1\n"
        "fmov fr6,@-%1\n"
        "fmov fr5,@-%1\n"
        "fmov fr4,@-%1\n"
        "fmov fr3,@-%1\n"
        "fmov fr2,@-%1\n"
        "fmov fr1,@-%1\n"
        "fmov fr0,@-%1\n"
        : "+r" (in_addr), "+r" (out_addr) :: "memory"
      );

    for (int i = 0; i < ARR_SIZE; i++) {
        if (in[i] != out[i]) {
            fprintf(
                stderr,
                "fmov fast->slow failed. " \
                    "Expected element %i to be %f, got %f\n", i, in[i], out[i]
            );
            pass = false;
        }
    }

    fill_arr(out, 0.0);
    return pass;
}

/* reads in data from `in` using slow singles */
/* writes data to `out` using fast pairs */
/* uses fmov @rm+,frn instruction */
/* uses fmov drm,@-rn instruction */
static int test_fmov_slow_in_fast_out(const float in[16], float out[16])
{
    int pass = true;
    fill_arr(out, 0.0);

    const register float *in_addr = in;
    register float *out_addr = out;
    asm volatile(
        "fmov @%0+,fr0\n"
        "fmov @%0+,fr1\n"
        "fmov @%0+,fr2\n"
        "fmov @%0+,fr3\n"
        "fmov @%0+,fr4\n"
        "fmov @%0+,fr5\n"
        "fmov @%0+,fr6\n"
        "fmov @%0+,fr7\n"
        "fmov @%0+,fr8\n"
        "fmov @%0+,fr9\n"
        "fmov @%0+,fr10\n"
        "fmov @%0+,fr11\n"
        "fmov @%0+,fr12\n"
        "fmov @%0+,fr13\n"
        "fmov @%0+,fr14\n"
        "fmov @%0+,fr15\n"

        "fschg\n" /* Set fast pairs on */
        "add #64,%1\n" /* Start at the back and work to the front */
        "fmov dr14,@-%1\n"
        "fmov dr12,@-%1\n"
        "fmov dr10,@-%1\n"
        "fmov dr8,@-%1\n"
        "fmov dr6,@-%1\n"
        "fmov dr4,@-%1\n"
        "fmov dr2,@-%1\n"
        "fmov dr0,@-%1\n"
        "fschg\n" /* Set fast pairs off */
        : "+r" (in_addr), "+r" (out_addr) :: "memory"
      );

    for (int i = 0; i < ARR_SIZE; i++) {
        if (in[i] != out[i]) {
            fprintf(
                stderr,
                "fmov slow->fast failed. " \
                    "Expected element %i to be %f, got %f\n", i, in[i], out[i]
            );
            pass = false;
        }
    }

    fill_arr(out, 0.0);
    return pass;
}

int main(void)
{
    single_precision_mode();
    int pass = true;

    if (!test_fmov_freg_freg()) {
        pass = false;
    };

    if (!test_fmov_freg_addr(out_arr)) {
        pass = false;
    }

    if (!test_fmov_addr_freg(in_arr)) {
        pass = false;
    }

    if (!test_fmov_freg_indirect_addr(out_arr)) {
        pass = false;
    }

    if (!test_fmov_indirect_addr_freg(in_arr)) {
        pass = false;
    };

    if (!test_fmov_dreg_dreg()) {
        pass = false;
    }

    if (!test_fmov_dreg_addr(out_arr)) {
        pass = false;
    }

    if (!test_fmov_addr_dreg(in_arr)) {
        pass = false;
    }

    if (!test_fmov_dreg_indirect_addr(out_arr)) {
        pass = false;
    }

    if (!test_fmov_indirect_addr_dreg(in_arr)) {
        pass = false;
    };

    if (!test_fmov_fast_in_slow_out(in_arr, out_arr)) {
        pass = false;
    }

    if (!test_fmov_slow_in_fast_out(in_arr, out_arr)) {
        pass = false;
    }

    if (!pass) {
        fprintf(
            stderr,
            "Fmov tests failed\n"
        );
        abort();
    }

    return 0;
}
