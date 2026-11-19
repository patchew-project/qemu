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

int main(void)
{
    test_usr_mask();
    puts(err ? "FAIL" : "PASS");
    return err != 0;
}
