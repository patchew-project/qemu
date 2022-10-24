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

    DO_MIPS32_i(addiu, 0, 0x0000, 0x00000000, 0x00000000);
    DO_MIPS32_i(addiu, 1, 0x0001, 0x00000001, 0x00000002);
    DO_MIPS32_i(addiu, 2, 0x0003, 0x00000002, 0x00000005);
    DO_MIPS32_i(addiu, 3, 0x0007, 0x00000004, 0x0000000B);
    DO_MIPS32_i(addiu, 4, 0x000F, 0x00000008, 0x00000017);
    DO_MIPS32_i(addiu, 5, 0x001F, 0x00000010, 0x0000002F);
    DO_MIPS32_i(addiu, 6, 0x003F, 0x00000020, 0x0000005F);
    DO_MIPS32_i(addiu, 7, 0x007F, 0x00000040, 0x000000BF);
    DO_MIPS32_i(addiu, 8, 0x00FF, 0x00000080, 0x0000017F);
    DO_MIPS32_i(addiu, 9, 0x01FF, 0x00000100, 0x000002FF);
    DO_MIPS32_i(addiu, 10, 0x03FF, 0x00000200, 0x000005FF);
    DO_MIPS32_i(addiu, 11, 0x07FF, 0x00000400, 0x00000BFF);
    DO_MIPS32_i(addiu, 12, 0x0FFF, 0x00000800, 0x000017FF);
    DO_MIPS32_i(addiu, 13, 0x1FFF, 0x00001000, 0x00002FFF);
    DO_MIPS32_i(addiu, 14, 0x3FFF, 0x00002000, 0x00005FFF);
    DO_MIPS32_i(addiu, 15, 0x7FFF, 0x00004000, 0x0000BFFF);
    DO_MIPS32_i(addiu, 16, -1, 0x00008000, 0x00007FFF);
    DO_MIPS32_i(addiu, 17, 0x0001, 0x00000000, 0x00000001);
    DO_MIPS32_i(addiu, 18, -1, 0x80000001, 0x80000000);
    DO_MIPS32_i(addiu, 19, -3, 0xC0000003, 0xC0000000);
    DO_MIPS32_i(addiu, 20, -7, 0xE0000007, 0xE0000000);
    DO_MIPS32_i(addiu, 21, -15, 0xF000000F, 0xF0000000);
    DO_MIPS32_i(addiu, 22, -31, 0xF800001F, 0xF8000000);
    DO_MIPS32_i(addiu, 23, -63, 0xFC00003F, 0xFC000000);
    DO_MIPS32_i(addiu, 24, -127, 0xFE00007F, 0xFE000000);
    DO_MIPS32_i(addiu, 25, -255, 0xFF0000FF, 0xFF000000);
    DO_MIPS32_i(addiu, 26, -511, 0xFF8001FF, 0xFF800000);
    DO_MIPS32_i(addiu, 27, -1023, 0xFFC003FF, 0xFFC00000);
    DO_MIPS32_i(addiu, 28, -2047, 0xFFE007FF, 0xFFE00000);
    DO_MIPS32_i(addiu, 29, -4095, 0xFFF00FFF, 0xFFF00000);
    DO_MIPS32_i(addiu, 30, -8191, 0xFFF81FFF, 0xFFF80000);
    DO_MIPS32_i(addiu, 31, 0xC001, 0xFFFC3FFF, 0xFFFC0000);
    DO_MIPS32_i(addiu, 32, 0x8001, 0xFFFE7FFF, 0xFFFE0000);
    DO_MIPS32_i(addiu, 33, 0x0001, 0xFFFFFFFF, 0x00000000);
    DO_MIPS32_i(addiu, 34, 0x5555, 0x00000000, 0x00005555);
    DO_MIPS32_i(addiu, 35, 0x5555, 0x55555555, 0x5555AAAA);
    DO_MIPS32_i(addiu, 36, 0x5555, 0xAAAAAAAA, 0xAAAAFFFF);
    DO_MIPS32_i(addiu, 37, 0x5555, 0xFFFFFFFF, 0x00005554);
    DO_MIPS32_i(addiu, 38, 0x0000, 0xAAAAAAAA, 0xAAAAAAAA);
    DO_MIPS32_i(addiu, 39, 0x5555, 0xAAAAAAAA, 0xAAAAFFFF);
    DO_MIPS32_i(addiu, 40, 0xAAAA, 0xAAAAAAAA, 0xAAAA5554);
    DO_MIPS32_i(addiu, 41, -1, 0xAAAAAAAA, 0xAAAAAAA9);
    DO_MIPS32_i(addiu, 42, 0x0001, 0x7FFFFFFF, 0x80000000);
    DO_MIPS32_i(addiu, 43, 0x7FFF, 0x7FFFFFFF, 0x80007FFE);
    DO_MIPS32_i(addiu, 44, -1, 0x80000000, 0x7FFFFFFF);
    DO_MIPS32_i(addiu, 45, 0x8000, 0x80000000, 0x7FFF8000);
    DO_MIPS32_i(addiu, 46, 0x555F, 0x7FFFAAAA, 0x80000009);
    DO_MIPS32_i(addiu, 47, 0xAAAA, 0x7FFF5555, 0x7FFEFFFF);
    DO_MIPS32_i(addiu, 48, 0x0002, 0x7FFFFFFF, 0x80000001);
    DO_MIPS32_i(addiu, 49, 0x0004, 0x7FFFFFFF, 0x80000003);
    DO_MIPS32_i(addiu, 50, 0x0008, 0x7FFFFFFF, 0x80000007);
    DO_MIPS32_i(addiu, 51, 0x0010, 0x7FFFFFFF, 0x8000000F);
    DO_MIPS32_i(addiu, 52, 0x0020, 0x7FFFFFFF, 0x8000001F);
    DO_MIPS32_i(addiu, 53, 0x0040, 0x7FFFFFFF, 0x8000003F);
    DO_MIPS32_i(addiu, 54, 0x0080, 0x7FFFFFFF, 0x8000007F);
    DO_MIPS32_i(addiu, 55, 0x0100, 0x7FFFFFFF, 0x800000FF);
    DO_MIPS32_i(addiu, 56, 0x0200, 0x7FFFFFFF, 0x800001FF);
    DO_MIPS32_i(addiu, 57, 0x0400, 0x7FFFFFFF, 0x800003FF);
    DO_MIPS32_i(addiu, 58, 0x0800, 0x7FFFFFFF, 0x800007FF);
    DO_MIPS32_i(addiu, 59, 0x1000, 0x7FFFFFFF, 0x80000FFF);
    DO_MIPS32_i(addiu, 60, 0x2000, 0x7FFFFFFF, 0x80001FFF);
    DO_MIPS32_i(addiu, 61, 0x4000, 0x7FFFFFFF, 0x80003FFF);

    printf("%s: PASS: %d, FAIL: %d\n", __FILE__, pass_count, fail_count);

    if (fail_count) {
        ret = -1;
    }

    return ret;
}
