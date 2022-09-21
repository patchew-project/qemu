/*
 * MIPS instruction test case
 *
 *  Copyright (c) 2022 Jiaxun Yang
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <sys/time.h>
#include <stdint.h>

#include "../../../../include/test_utils_32.h"

int main(void)
{
    int ret = 0;
    int pass_count = 0;
    int fail_count = 0;

    DO_MIPS32_r2_s(multu, 0, 0, 0, 0x00000000, 0x00000000, 0x00000000, 0x00000000);
    DO_MIPS32_r2_s(multu, 1, 0, 0, 0x00000001, 0x00000001, 0x00000000, 0x00000001);
    DO_MIPS32_r2_s(multu, 2, 0, 0, 0x00000002, 0x00000003, 0x00000000, 0x00000006);
    DO_MIPS32_r2_s(multu, 3, 0, 0, 0x00000004, 0x00000007, 0x00000000, 0x0000001C);
    DO_MIPS32_r2_s(multu, 4, 0, 0, 0x00000008, 0x0000000F, 0x00000000, 0x00000078);
    DO_MIPS32_r2_s(multu, 5, 0, 0, 0x00000010, 0x0000001F, 0x00000000, 0x000001F0);
    DO_MIPS32_r2_s(multu, 6, 0, 0, 0x00000020, 0x0000003F, 0x00000000, 0x000007E0);
    DO_MIPS32_r2_s(multu, 7, 0, 0, 0x00000040, 0x0000007F, 0x00000000, 0x00001FC0);
    DO_MIPS32_r2_s(multu, 8, 0, 0, 0x00000080, 0x000000FF, 0x00000000, 0x00007F80);
    DO_MIPS32_r2_s(multu, 9, 0, 0, 0x00000100, 0x000001FF, 0x00000000, 0x0001FF00);
    DO_MIPS32_r2_s(multu, 10, 0, 0, 0x00000200, 0x000003FF, 0x00000000, 0x0007FE00);
    DO_MIPS32_r2_s(multu, 11, 0, 0, 0x00000400, 0x000007FF, 0x00000000, 0x001FFC00);
    DO_MIPS32_r2_s(multu, 12, 0, 0, 0x00000800, 0x00000FFF, 0x00000000, 0x007FF800);
    DO_MIPS32_r2_s(multu, 13, 0, 0, 0x00001000, 0x00001FFF, 0x00000000, 0x01FFF000);
    DO_MIPS32_r2_s(multu, 14, 0, 0, 0x00002000, 0x00003FFF, 0x00000000, 0x07FFE000);
    DO_MIPS32_r2_s(multu, 15, 0, 0, 0x00004000, 0x00007FFF, 0x00000000, 0x1FFFC000);
    DO_MIPS32_r2_s(multu, 16, 0, 0, 0x00008000, 0x0000FFFF, 0x00000000, 0x7FFF8000);
    DO_MIPS32_r2_s(multu, 17, 0, 0, 0x00010000, 0x0001FFFF, 0x00000001, 0xFFFF0000);
    DO_MIPS32_r2_s(multu, 18, 0, 0, 0x00020000, 0x0003FFFF, 0x00000007, 0xFFFE0000);
    DO_MIPS32_r2_s(multu, 19, 0, 0, 0x00040000, 0x0007FFFF, 0x0000001F, 0xFFFC0000);
    DO_MIPS32_r2_s(multu, 20, 0, 0, 0x00080000, 0x000FFFFF, 0x0000007F, 0xFFF80000);
    DO_MIPS32_r2_s(multu, 21, 0, 0, 0x00100000, 0x001FFFFF, 0x000001FF, 0xFFF00000);
    DO_MIPS32_r2_s(multu, 22, 0, 0, 0x00200000, 0x003FFFFF, 0x000007FF, 0xFFE00000);
    DO_MIPS32_r2_s(multu, 23, 0, 0, 0x00400000, 0x007FFFFF, 0x00001FFF, 0xFFC00000);
    DO_MIPS32_r2_s(multu, 24, 0, 0, 0x00800000, 0x00FFFFFF, 0x00007FFF, 0xFF800000);
    DO_MIPS32_r2_s(multu, 25, 0, 0, 0x01000000, 0x01FFFFFF, 0x0001FFFF, 0xFF000000);
    DO_MIPS32_r2_s(multu, 26, 0, 0, 0x02000000, 0x03FFFFFF, 0x0007FFFF, 0xFE000000);
    DO_MIPS32_r2_s(multu, 27, 0, 0, 0x04000000, 0x07FFFFFF, 0x001FFFFF, 0xFC000000);
    DO_MIPS32_r2_s(multu, 28, 0, 0, 0x08000000, 0x0FFFFFFF, 0x007FFFFF, 0xF8000000);
    DO_MIPS32_r2_s(multu, 29, 0, 0, 0x10000000, 0x1FFFFFFF, 0x01FFFFFF, 0xF0000000);
    DO_MIPS32_r2_s(multu, 30, 0, 0, 0x20000000, 0x3FFFFFFF, 0x07FFFFFF, 0xE0000000);
    DO_MIPS32_r2_s(multu, 31, 0, 0, 0x40000000, 0x7FFFFFFF, 0x1FFFFFFF, 0xC0000000);
    DO_MIPS32_r2_s(multu, 32, 0, 0, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000);
    DO_MIPS32_r2_s(multu, 33, 0, 0, 0x00000000, 0x55555555, 0x00000000, 0x00000000);
    DO_MIPS32_r2_s(multu, 34, 0, 0, 0x55555555, 0x55555555, 0x1C71C71C, 0x38E38E39);
    DO_MIPS32_r2_s(multu, 35, 0, 0, 0xAAAAAAAA, 0x55555555, 0x38E38E38, 0x71C71C72);
    DO_MIPS32_r2_s(multu, 36, 0, 0, 0xFFFFFFFF, 0x55555555, 0x55555554, 0xAAAAAAAB);
    DO_MIPS32_r2_s(multu, 37, 0, 0, 0xAAAAAAAA, 0x00000000, 0x00000000, 0x00000000);
    DO_MIPS32_r2_s(multu, 38, 0, 0, 0xAAAAAAAA, 0x55555555, 0x38E38E38, 0x71C71C72);
    DO_MIPS32_r2_s(multu, 39, 0, 0, 0xAAAAAAAA, 0xAAAAAAAA, 0x71C71C70, 0xE38E38E4);
    DO_MIPS32_r2_s(multu, 40, 0, 0, 0xAAAAAAAA, 0xFFFFFFFF, 0xAAAAAAA9, 0x55555556);
    DO_MIPS32_r2_s(multu, 41, 0, 0, 0x7FFFFFFF, 0x7FFFFFFF, 0x3FFFFFFF, 0x00000001);
    DO_MIPS32_r2_s(multu, 42, 0, 0, 0x7FFFFFFF, 0x80000000, 0x3FFFFFFF, 0x80000000);
    DO_MIPS32_r2_s(multu, 43, 0, 0, 0x7FFFFFFF, 0xFFFFFFFF, 0x7FFFFFFE, 0x80000001);
    DO_MIPS32_r2_s(multu, 44, 0, 0, 0x80000000, 0x7FFFFFFF, 0x3FFFFFFF, 0x80000000);
    DO_MIPS32_r2_s(multu, 45, 0, 0, 0x80000000, 0x80000000, 0x40000000, 0x00000000);
    DO_MIPS32_r2_s(multu, 46, 0, 0, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000);
    DO_MIPS32_r2_s(multu, 47, 0, 0, 0xFFFFFFFF, 0x7FFFFFFF, 0x7FFFFFFE, 0x80000001);
    DO_MIPS32_r2_s(multu, 48, 0, 0, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x80000000);
    DO_MIPS32_r2_s(multu, 49, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0x00000001);

    printf("%s: PASS: %d, FAIL: %d\n", __FILE__, pass_count, fail_count);

    if (fail_count) {
        ret = -1;
    }

    return ret;
}
