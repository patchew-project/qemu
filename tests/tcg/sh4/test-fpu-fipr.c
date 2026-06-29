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

static float fipr(const float v1[4], const float v2[4]) {
    float fout = 0.0;
    // Perform inner product of fv4 and fv8
    // Result is stored in the last float register of the last vector argument
    asm volatile(
        "fmov %1,fr4\n"
        "fmov %2,fr5\n"
        "fmov %3,fr6\n"
        "fmov %4,fr7\n"
        "fmov %5,fr8\n"
        "fmov %6,fr9\n"
        "fmov %7,fr10\n"
        "fmov %8,fr11\n"
        "fipr	fv4,fv8\n" // (fr4,fr5,fr6,fr7) dot (fr8,fr9,fr10,fr11)
        "fmov fr11,%0\n"   // Result in fr11
        : "=f" (fout) 
        : "f" (v1[0]), "f" (v1[1]), "f" (v1[2]), "f" (v1[3]), "f" (v2[0]), "f" (v2[1]), "f" (v2[2]), "f" (v2[3]));
    return fout;
}

int main(void)
{
    // The SH4 manual specifies `fipr` is only available when FPSCR register precision-mode (PR) bit is 0
    single_precision_mode();

    float v1[4] = {1.0, 0.0, 0.0, 0.0};
    float mag_sq = 0.0;
    float dot = 0.0;

    mag_sq = fipr(v1, v1);
    if (mag_sq != 1.0) {
        abort();
    }

    v1[0] = 2.0;
    mag_sq = fipr(v1, v1);
    if (mag_sq != 4.0) {
        abort();
    }

    float v2[4] = {1.0, 0.0, 0.0, 0.0};
    dot = fipr(v1, v2);
    if (dot != 2.0) {
        abort();
    }

    return 0;
}
