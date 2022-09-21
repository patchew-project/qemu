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

    DO_MIPS32_i(sltiu, 0, 0x0000, 0x00000000, 0);
    DO_MIPS32_i(sltiu, 1, 0x0001, 0x00000001, 0);
    DO_MIPS32_i(sltiu, 2, 0x0003, 0x00000002, 1);
    DO_MIPS32_i(sltiu, 3, 0x0007, 0x00000005, 1);
    DO_MIPS32_i(sltiu, 4, 0x000F, 0x0000000B, 1);
    DO_MIPS32_i(sltiu, 5, 0x001F, 0x00000017, 1);
    DO_MIPS32_i(sltiu, 6, 0x003F, 0x0000002F, 1);
    DO_MIPS32_i(sltiu, 7, 0x007F, 0x0000005F, 1);
    DO_MIPS32_i(sltiu, 8, 0x00FF, 0x000000BF, 1);
    DO_MIPS32_i(sltiu, 9, 0x01FF, 0x0000017F, 1);
    DO_MIPS32_i(sltiu, 10, 0x03FF, 0x000002FF, 1);
    DO_MIPS32_i(sltiu, 11, 0x07FF, 0x000005FF, 1);
    DO_MIPS32_i(sltiu, 12, 0x0FFF, 0x00000BFF, 1);
    DO_MIPS32_i(sltiu, 13, 0x1FFF, 0x000017FF, 1);
    DO_MIPS32_i(sltiu, 14, 0x3FFF, 0x00002FFF, 1);
    DO_MIPS32_i(sltiu, 15, 0x7FFF, 0x00005FFF, 1);
    DO_MIPS32_i(sltiu, 16, 0xFFFF, 0x0000BFFF, 1);
    DO_MIPS32_i(sltiu, 17, 0x5555, 0x00000000, 1);
    DO_MIPS32_i(sltiu, 18, 0xFFFF, 0x7FFFFFFF, 1);
    DO_MIPS32_i(sltiu, 19, 0x8000, 0x7FFFFFFF, 1);

    printf("%s: PASS: %d, FAIL: %d\n", __FILE__, pass_count, fail_count);

    if (fail_count) {
        ret = -1;
    }

    return ret;
}
