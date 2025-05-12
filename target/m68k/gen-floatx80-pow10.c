/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Generator for floatx80-pow10.c.inc, using glibc's multi-precision
 * integer arithmetic in stdio for correct rounding.
 * Only works on x86 host, so not integrated into the build process.
 */

#include <stdio.h>
#include <float.h>

int main()
{
    for (int i = 0; i <= LDBL_MAX_10_EXP; ++i) {
        char buf[32];
        union {
            long double d;
            struct {
                unsigned l;
                unsigned m;
                unsigned short h;
            } i;
        } u = { };

        snprintf(buf, sizeof(buf), "1e%d", i);
        sscanf(buf, "%Le", &u.d);
        printf("/* %4d */ make_floatx80_init(0x%04x, 0x%08x%08x),\n",
               i, u.i.h, u.i.m, u.i.l);
    }
    return 0;
}
