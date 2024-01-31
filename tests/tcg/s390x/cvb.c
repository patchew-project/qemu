/*
 * Test the CONVERT TO DECIMAL instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

static int32_t cvb(uint64_t x)
{
    uint32_t ret;

    asm("cvb %[ret],%[x]" : [ret] "=r" (ret) : [x] "R" (x));

    return ret;
}

static int64_t cvbg(__uint128_t x)
{
    int64_t ret;

    asm("cvbg %[ret],%[x]" : [ret] "=r" (ret) : [x] "T" (x));

    return ret;
}

int main(void)
{
    __uint128_t m = (((__uint128_t)0x9223372036854775) << 16) | 0x8070;

    assert(cvb(0xc) == 0);
    assert(cvb(0x1c) == 1);
    assert(cvb(0x25594c) == 25594);
    assert(cvb(0x1d) == -1);
    assert(cvb(0x2147483647c) == 0x7fffffff);
    assert(cvb(0x2147483647d) == -0x7fffffff);

    assert(cvbg(0xc) == 0);
    assert(cvbg(0x1c) == 1);
    assert(cvbg(0x25594c) == 25594);
    assert(cvbg(0x1d) == -1);
    assert(cvbg(m | 0xc) == 0x7fffffffffffffff);
    assert(cvbg(m | 0xd) == -0x7fffffffffffffff);

    return EXIT_SUCCESS;
}
