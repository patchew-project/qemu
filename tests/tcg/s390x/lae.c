/*
 * Test the LOAD ADDRESS EXTENDED instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    unsigned long long ar = -1, b2 = 100000, r, x2 = 500;
    int tmp;

    asm("ear %[tmp],%[r]\n"
        "lae %[r],42(%[x2],%[b2])\n"
        "ear %[ar],%[r]\n"
        "sar %[r],%[tmp]"
        : [tmp] "=&r" (tmp), [r] "=&r" (r), [ar] "+r" (ar)
        : [b2] "r" (b2), [x2] "r" (x2)
        : "memory");
    assert(ar == 0xffffffff00000000ULL);
    assert(r == 100542);

    return EXIT_SUCCESS;
}
