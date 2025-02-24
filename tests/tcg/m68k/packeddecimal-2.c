/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test packed decimal real conversion from long double, dynamic k-factor */

#include <stdio.h>
#include <float.h>

struct T {
    unsigned int d[3];
    long double lf;
    int kfactor;
};

static const struct T tests[] = {
    { { 0x00000001, 0x00000000, 0x00000000 }, 1.0e0l, 0 },
    { { 0x00100001, 0x00000000, 0x00000000 }, 1.0e10l, 0 },
    { { 0x41000001, 0x00000000, 0x00000000 }, 1.0e-1l, 0 },
    { { 0x85000005, 0x55550000, 0x00000000 }, -5.5555e5l, 5 },
    { { 0x45000005, 0x55550000, 0x00000000 }, 5.5555e-5l, 5 },
    { { 0x05000002, 0x22220000, 0x00000000 }, 2.2222e5, 99 },
    { { 0x05000002, 0x22220000, 0x00000000 }, 2.2222e5, 5 },
    { { 0x05000002, 0x20000000, 0x00000000 }, 2.2222e5, 2 },
    { { 0x02394001, 0x18973149, 0x53572318 }, LDBL_MAX, 17 },
    { { 0x42394001, 0x68105157, 0x15560468 }, LDBL_MIN, 17 },
    { { 0x41594001, 0x82259976, 0x59412373 }, LDBL_TRUE_MIN, 17 },
};

int main()
{
    int ret = 0;

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const struct T *t = &tests[i];
        unsigned int out[3];

        asm("fmove.p %1,(%0),%2"
            : : "a"(out), "f"(t->lf), "d"(t->kfactor) : "memory");

        if (out[0] != t->d[0] || out[1] != t->d[1] || out[2] != t->d[2]) {
            fprintf(stderr, "Mismatch at %d: %08x%08x%08x != %08x%08x%08x\n",
                    i, out[0], out[1], out[2],
                    t->d[0], t->d[1], t->d[2]);
            ret = 1;
        }
    }
    return ret;
}
