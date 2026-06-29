/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <limits.h>
#include <stdlib.h>
static const unsigned long FPSCR_PR_BIT = 1UL << 19;

static void single_precision_mode(void) {
    unsigned long fpscr;
    // Read FPSCR register
    asm volatile("sts fpscr, %0" : "=r" (fpscr));
    // Set precision mode to single-precision
    fpscr &= ~FPSCR_PR_BIT;
    // Write FPSCR register
    asm volatile("lds %0, fpscr" : : "r" (fpscr)); 
}

// Set XMTRX registers to a doubling scale matrix
static void xmtrx_scale_2(void) {
    asm volatile(
        "fldi0 fr0\n" // Pair dr0 (fr0, fr1) = zero,zero
        "fldi0 fr1\n"
        "fldi1 fr2\n"
        "fadd fr2,fr2\n" // Pair dr2 (fr2,fr3) = two,zero
        "fldi0 fr3\n"
        "fldi0 fr4\n"
        "fmov fr2,fr5\n" // Pair dr4 (fr4, fr5) = zero,two

        // Enable pair-write
        "fschg\n" 
        // matrix[0][0] = 2.0
        // matrix[0][1] = 0.0
        "fmov dr2,xd0\n" 
        // matrix[0][2] = 0.0
        // matrix[0][3] = 0.0
        "fmov dr0,xd2\n"
        // matrix[1][0] = 0.0
        // matrix[1][1] = 2.0
        "fmov dr4,xd4\n"
        // matrix[1][2] = 0.0
        // matrix[1][3] = 0.0
        "fmov dr0,xd6\n"
        // matrix[2][0] = 0.0
        // matrix[2][1] = 0.0
        "fmov dr0,xd8\n"
        // matrix[2][2] = 2.0
        // matrix[2][3] = 0.0
        "fmov dr2,xd10\n"
        // matrix[3][0] = 0.0
        // matrix[3][1] = 0.0
        "fmov dr0,xd12\n"
        // matrix[3][2] = 0.0
        // matrix[3][3] = 2.0
        "fmov dr4,xd14\n"
        // Disable pair-write
        "fschg"
        :
        :
        : "fr0", "fr1", "fr2", "fr3", "fr4", "fr5");
}

// The SH4 manual specifies `ftrv` is only available when FPSCR register precision-mode (PR) bit is 0, aka single-precision
// Transform vector vin by xmtrx; store result in vector vout
static void ftrv(const float vin[4], float vout[4]) {
    asm volatile("fmov %4,fr0\n" 
                 "fmov %5,fr1\n" 
                 "fmov %6,fr2\n" 
                 "fmov %7,fr3\n" 
                 "ftrv xmtrx,fv0\n"
                 "fmov fr0,%0\n" 
                 "fmov fr1,%1\n" 
                 "fmov fr2,%2\n" 
                 "fmov fr3,%3" 
                 : "=f" (vout[0]), "=f" (vout[1]), "=f" (vout[2]), "=f" (vout[3])
                 : "f" (vin[0]), "f" (vin[1]), "f" (vin[2]), "f" (vin[3])
                 : "fr0", "fr1", "fr2", "fr3");
}

int main(void)
{
    single_precision_mode();
    xmtrx_scale_2();
    float vin[4] = {1.0, 2.0, 3.0, 4.0};
    float vout[4] = { 0.0, 0.0, 0.0, 0.0};

    ftrv(vin, vout);
    if (vout[0] != 2.0) {
        abort();
    }
    if (vout[1] != 4.0) {
        abort();
    }
    if (vout[2] != 6.0) {
        abort();
    }
    if (vout[3] != 8.0) {
        abort();
    }

    return 0;
}
