/*
 * Copyright (C) 2017, Linaro
 * Author: Alex Bennée <alex.bennee@linaro.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "fpu/softfloat.h"

typedef struct {
    float_status initial_status;
    float16 in;
    float16 out;
    uint8_t final_exception_flags;
} f16_test_data;

static void test_f16_round_to_int(void)
{
    int i;
    float16 out;
    float_status flags, *fp = &flags;
    f16_test_data test_data[] = {
        { { /* defaults */ }, 0x87FF, 0x8000 },
        { { /* defaults */ }, 0xE850, 0xE850 },
        { { /* defaults */ }, 0x0000, 0x0000 },
        { { /* defaults */ }, 0x857F, 0x8000 },
        { { /* defaults */ }, 0x74FB, 0x74FB },
        /* from risu 3b4:       4ef98945        frintp  v5.8h, v10.8h */
        { { .float_detect_tininess = 1, .float_rounding_mode = 2}, 0x06b1, 0x3c00, 0 },
        { { .float_detect_tininess = 1, .float_rounding_mode = 2}, 0x6966, 0x6966, 0 },
        { { .float_detect_tininess = 1, .float_rounding_mode = 2}, 0x83c0, 0x8000, 0 },
        { { .float_detect_tininess = 1, .float_rounding_mode = 2}, 0xa619, 0x8000, 0 },
        { { .float_detect_tininess = 1, .float_rounding_mode = 2}, 0x9cf4, 0x8000, 0 },
        { { .float_detect_tininess = 1, .float_rounding_mode = 2}, 0xee11, 0xee11, 0 },
        { { .float_detect_tininess = 1, .float_rounding_mode = 2}, 0xee5c, 0xee5c, 0 },
        { { .float_detect_tininess = 1, .float_rounding_mode = 2}, 0x8004, 0x8000, 0 }
    };

    for (i = 0; i < ARRAY_SIZE(test_data); ++i) {
        flags = test_data[i].initial_status;
        out = float16_round_to_int(test_data[i].in, fp);

        if (!(test_data[i].out == out)) {
            fprintf(stderr, "%s[%d]: expected %#04x got %#04x\n",
                    __func__, i, test_data[i].out, out);
            g_test_fail();
        }
    }
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/softfloat/f16/round_to_int", test_f16_round_to_int);
    return g_test_run();
}
