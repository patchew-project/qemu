/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>

static int err;

#include "hex_test.h"

static void test_predicate_read(void)
{
    unsigned old, new;

    asm volatile(
        "r0 = #0x12\n"
        "p3:0 = r0\n"
        "{\n"
        "    p0 = cmp.eq(r0,r0)\n"
        "    %[old] = p3:0\n"
        "}\n"
        "%[new] = p3:0\n"
        : [old] "=r"(old), [new] "=r"(new)
        : : "r0", "p0", "p1", "p2", "p3"
    );
    /* Packet reads old C4, then commits P0. */
    check32(old, 0x12);
    check32(new, 0xff);
}

int main(void)
{
    test_predicate_read();
    puts(err ? "FAIL" : "PASS");
    return err != 0;
}
