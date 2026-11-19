/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdint.h>

static int err;

#include "hex_test.h"

static void test_usr_mask(void)
{
    unsigned old, value;

    asm volatile(
        "%[old] = usr\n"
        "r0 = #-1\n"
        "usr = r0\n"
        "%[value] = usr\n"
        "usr = %[old]\n"
        : [old] "=&r"(old), [value] "=&r"(value)
        : : "r0"
    );
    /* V85 USR writable bits. */
    check32(value, 0x3fffff7f);
}

static void test_call_priority(void)
{
    unsigned lr;

    asm volatile(
        "r12 = r31\n"
        "r31 = ##0x12345678\n"
        "r0 = #0\n"
        "p0 = cmp.eq(r0,r0)\n"
        "{\n"
        "    if (p0) jump 1f\n"
        "    call 2f\n"
        "}\n"
        "2:\n"
        "r31 = #0\n"
        "1:\n"
        "%[lr] = r31\n"
        "r31 = r12\n"
        : [lr] "=&r"(lr)
        : : "r0", "r12", "r31", "p0"
    );
    /* Losing call preserves LR. */
    check32(lr, 0x12345678);
}

int main(void)
{
    test_usr_mask();
    test_call_priority();
    puts(err ? "FAIL" : "PASS");
    return err != 0;
}
