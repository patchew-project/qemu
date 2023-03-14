/*
 * Smoke test RXSBG instruction with T=1.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    unsigned long r1, r2, cc;

    r1 = 0xc8dc86a225a77bb4;
    r2 = 0xd6aff24fa3e7320;
    cc = 0;
    asm("rxsbg %[r1],%[r2],177,43,228\n"
        "ipm %[cc]"
        : [cc] "+r" (cc)
        : [r1] "r" (r1)
        , [r2] "r" (r2)
        : "cc");
    cc = (cc >> 28) & 1;
    assert(cc == 1);

    return EXIT_SUCCESS;
}
