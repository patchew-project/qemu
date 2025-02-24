/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test packed decimal real conversion to long double. */

#include <stdio.h>

struct T {
    unsigned int d[3];
    long double f;
};

static const struct T tests[] = {
    { { 0x00000001, 0x00000000, 0x00000000 }, 1.0e0l },
    { { 0x01000001, 0x00000000, 0x00000000 }, 1.0e1l },
    { { 0x00100001, 0x00000000, 0x00000000 }, 1.0e10l },
    { { 0x00000000, 0x10000000, 0x00000000 }, 0.1e0l },
    { { 0x41000001, 0x00000000, 0x00000000 }, 1.0e-1l },
    { { 0x85000005, 0x55550000, 0x00000000 }, -5.5555e5l },
    { { 0x09990009, 0x99999999, 0x99999999 }, 9.9999999999999999e999l },
    { { 0x03210001, 0x23456789, 0x12345678 }, 1.2345678912345678e123l },
    { { 0x00000000, 0x00000000, 0x00000000 }, 0.0l },
    { { 0x80000000, 0x00000000, 0x00000000 }, -0.0l },
    { { 0x09990000, 0x00000000, 0x00000000 }, 0.0e999l },
};

int main()
{
    int ret = 0;

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const struct T *t = &tests[i];
        long double f;

        asm("fmove.p (%1),%0" : "=f"(f) : "a"(t->d));

        if (f != t->f) {
            fprintf(stderr, "Mismatch at %d: %.17Le != %.17Le\n", i, f, t->f);
            ret = 1;
        }
    }
    return ret;
}
