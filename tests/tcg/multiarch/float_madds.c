/*
 * Fused Multiply Add (Single)
 *
 * Copyright (c) 2019 Linaro
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <fenv.h>

#include "float_helpers.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    int flag;
    char *desc;
} float_mapping;

float_mapping round_flags[] = {
    { FE_TONEAREST, "to nearest" },
#ifdef FE_UPWARD
    { FE_UPWARD, "upwards" },
#endif
#ifdef FE_DOWNWARD
    { FE_DOWNWARD, "downwards" },
#endif
    { FE_TOWARDZERO, "to zero" }
};


void print_inputs(float a, float b, float c)
{
    char *a_fmt, *b_fmt, *c_fmt;

    a_fmt = fmt_f32(a);
    b_fmt = fmt_f32(b);
    c_fmt = fmt_f32(c);

    printf("op : %s * %s + %s\n", a_fmt, b_fmt, c_fmt);

    free(a_fmt);
    free(b_fmt);
    free(c_fmt);
}

void print_result(float r, int j, int k)
{
    char *r_fmt, *flag_fmt;

    r_fmt = fmt_f32(r);
    flag_fmt = fmt_flags();

    printf("res: %s flags=%s (%d/%d)\n", r_fmt, flag_fmt, j, k);

    free(r_fmt);
    free(flag_fmt);
}

int main(int argc, char *argv[argc])
{
    int i, j, k, nums;
    float a, b, c, r;

    /* From https://bugs.launchpad.net/qemu/+bug/1841491 */
    add_f32_const(0x1.ffffffffffffcp-1022);
    add_f32_const(0x1.0000000000001p-1);
    add_f32_const(0x0.0000000000001p-1022);
    add_f32_const(0x8p-152);
    add_f32_const(0x8p-152);
    add_f32_const(0x8p-152);

    nums = get_num_f32();

    for (i = 0; i < ARRAY_SIZE(round_flags); ++i) {
        fesetround(round_flags[i].flag);
        printf("### Rounding %s\n", round_flags[i].desc);
        for (j = 0; j < nums; j++) {
            for (k = 0; k < 3; k++) {
                a = get_f32(j + ((k)%3));
                b = get_f32(j + ((k+1)%3));
                c = get_f32(j + ((k+2)%3));

                print_inputs(a, b, c);

                feclearexcept(FE_ALL_EXCEPT);

#if defined(__arm__)
                r = __builtin_fmaf(a, b, c);
#else
                r = __builtin_fmaf(a, b, c);
#endif

                print_result(r, j, k);
            }
        }
    }

    return 0;
}
