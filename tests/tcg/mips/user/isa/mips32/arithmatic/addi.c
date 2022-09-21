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

    DO_MIPS32_i(addi, 0, 0x0000, 0x00000000, 0x00000000);
    DO_MIPS32_i(addi, 1, 0x0001, 0x00000001, 0x00000002);
    DO_MIPS32_i(addi, 2, 0x0003, 0x00000002, 0x00000005);
    DO_MIPS32_i(addi, 3, 0x0007, 0x00000004, 0x0000000B);
    DO_MIPS32_i(addi, 4, 0x000F, 0x00000008, 0x00000017);
    DO_MIPS32_i(addi, 5, 0x001F, 0x00000010, 0x0000002F);
    DO_MIPS32_i(addi, 6, 0x003F, 0x00000020, 0x0000005F);
    DO_MIPS32_i(addi, 7, 0x007F, 0x00000040, 0x000000BF);
    DO_MIPS32_i(addi, 8, 0x00FF, 0x00000080, 0x0000017F);
    DO_MIPS32_i(addi, 9, 0x01FF, 0x00000100, 0x000002FF);
    DO_MIPS32_i(addi, 10, 0x03FF, 0x00000200, 0x000005FF);
    DO_MIPS32_i(addi, 11, 0x07FF, 0x00000400, 0x00000BFF);
    DO_MIPS32_i(addi, 12, 0x0FFF, 0x00000800, 0x000017FF);
    DO_MIPS32_i(addi, 13, 0x1FFF, 0x00001000, 0x00002FFF);
    DO_MIPS32_i(addi, 14, 0x3FFF, 0x00002000, 0x00005FFF);
    DO_MIPS32_i(addi, 15, 0x7FFF, 0x00004000, 0x0000BFFF);
    DO_MIPS32_i(addi, 16, 0xFFFF, 0x00008000, 0x00007FFF);
    DO_MIPS32_i(addi, 17, 0x0001, 0x00000000, 0x00000001);
    DO_MIPS32_i(addi, 18, 0xFFFF, 0x80000001, 0x80000000);
    DO_MIPS32_i(addi, 19, 0xFFFD, 0xC0000003, 0xC0000000);
    DO_MIPS32_i(addi, 20, 0xFFF9, 0xE0000007, 0xE0000000);
    DO_MIPS32_i(addi, 21, 0xFFF1, 0xF000000F, 0xF0000000);
    DO_MIPS32_i(addi, 22, 0xFFE1, 0xF800001F, 0xF8000000);
    DO_MIPS32_i(addi, 23, 0xFFC1, 0xFC00003F, 0xFC000000);
    DO_MIPS32_i(addi, 24, 0xFF81, 0xFE00007F, 0xFE000000);
    DO_MIPS32_i(addi, 25, 0xFF01, 0xFF0000FF, 0xFF000000);
    DO_MIPS32_i(addi, 26, 0xFE01, 0xFF8001FF, 0xFF800000);
    DO_MIPS32_i(addi, 27, 0xFC01, 0xFFC003FF, 0xFFC00000);
    DO_MIPS32_i(addi, 28, 0xF801, 0xFFE007FF, 0xFFE00000);
    DO_MIPS32_i(addi, 29, 0xF001, 0xFFF00FFF, 0xFFF00000);
    DO_MIPS32_i(addi, 30, 0xE001, 0xFFF81FFF, 0xFFF80000);
    DO_MIPS32_i(addi, 31, 0xC001, 0xFFFC3FFF, 0xFFFC0000);
    DO_MIPS32_i(addi, 32, 0x8001, 0xFFFE7FFF, 0xFFFE0000);
    DO_MIPS32_i(addi, 33, 0x0001, 0xFFFFFFFF, 0x00000000);
    DO_MIPS32_i(addi, 34, 0x5555, 0x00000000, 0x00005555);
    DO_MIPS32_i(addi, 35, 0x5555, 0x55555555, 0x5555AAAA);
    DO_MIPS32_i(addi, 36, 0x5555, 0xAAAAAAAA, 0xAAAAFFFF);
    DO_MIPS32_i(addi, 37, 0x5555, 0xFFFFFFFF, 0x00005554);
    DO_MIPS32_i(addi, 38, 0x0000, 0xAAAAAAAA, 0xAAAAAAAA);
    DO_MIPS32_i(addi, 39, 0x5555, 0xAAAAAAAA, 0xAAAAFFFF);
    DO_MIPS32_i(addi, 40, 0xAAAA, 0xAAAAAAAA, 0xAAAA5554);
    DO_MIPS32_i(addi, 41, 0xFFFF, 0xAAAAAAAA, 0xAAAAAAA9);

    printf("%s: PASS: %d, FAIL: %d\n", __FILE__, pass_count, fail_count);

    if (fail_count) {
        ret = -1;
    }

    return ret;
}
