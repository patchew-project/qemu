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

    DO_MIPS32_r(sltu, 0, 0x00000000, 0x00000000, 0);
    DO_MIPS32_r(sltu, 1, 0x00000001, 0x00000001, 0);
    DO_MIPS32_r(sltu, 2, 0x00000002, 0x00000003, 1);
    DO_MIPS32_r(sltu, 3, 0x00000005, 0x00000007, 1);
    DO_MIPS32_r(sltu, 4, 0x0000000B, 0x0000000F, 1);
    DO_MIPS32_r(sltu, 5, 0x00000017, 0x0000001F, 1);
    DO_MIPS32_r(sltu, 6, 0x0000002F, 0x0000003F, 1);
    DO_MIPS32_r(sltu, 7, 0x0000005F, 0x0000007F, 1);
    DO_MIPS32_r(sltu, 8, 0x000000BF, 0x000000FF, 1);
    DO_MIPS32_r(sltu, 9, 0x0000017F, 0x000001FF, 1);
    DO_MIPS32_r(sltu, 10, 0x000002FF, 0x000003FF, 1);
    DO_MIPS32_r(sltu, 11, 0x000005FF, 0x000007FF, 1);
    DO_MIPS32_r(sltu, 12, 0x00000BFF, 0x00000FFF, 1);
    DO_MIPS32_r(sltu, 13, 0x000017FF, 0x00001FFF, 1);
    DO_MIPS32_r(sltu, 14, 0x00002FFF, 0x00003FFF, 1);
    DO_MIPS32_r(sltu, 15, 0x00005FFF, 0x00007FFF, 1);
    DO_MIPS32_r(sltu, 16, 0x0000BFFF, 0x0000FFFF, 1);
    DO_MIPS32_r(sltu, 17, 0x00017FFF, 0x0001FFFF, 1);
    DO_MIPS32_r(sltu, 18, 0x0002FFFF, 0x0003FFFF, 1);
    DO_MIPS32_r(sltu, 19, 0x0005FFFF, 0x0007FFFF, 1);
    DO_MIPS32_r(sltu, 20, 0x000BFFFF, 0x000FFFFF, 1);
    DO_MIPS32_r(sltu, 21, 0x0017FFFF, 0x001FFFFF, 1);
    DO_MIPS32_r(sltu, 22, 0x002FFFFF, 0x003FFFFF, 1);
    DO_MIPS32_r(sltu, 23, 0x005FFFFF, 0x007FFFFF, 1);
    DO_MIPS32_r(sltu, 24, 0x00BFFFFF, 0x00FFFFFF, 1);
    DO_MIPS32_r(sltu, 25, 0x017FFFFF, 0x01FFFFFF, 1);
    DO_MIPS32_r(sltu, 26, 0x02FFFFFF, 0x03FFFFFF, 1);
    DO_MIPS32_r(sltu, 27, 0x05FFFFFF, 0x07FFFFFF, 1);
    DO_MIPS32_r(sltu, 28, 0x0BFFFFFF, 0x0FFFFFFF, 1);
    DO_MIPS32_r(sltu, 29, 0x17FFFFFF, 0x1FFFFFFF, 1);
    DO_MIPS32_r(sltu, 30, 0x2FFFFFFF, 0x3FFFFFFF, 1);
    DO_MIPS32_r(sltu, 31, 0x5FFFFFFF, 0x7FFFFFFF, 1);
    DO_MIPS32_r(sltu, 32, 0xBFFFFFFF, 0xFFFFFFFF, 1);

    printf("%s: PASS: %d, FAIL: %d\n", __FILE__, pass_count, fail_count);

    if (fail_count) {
        ret = -1;
    }

    return ret;
}
