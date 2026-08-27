/*
 * Semihosting Tests - RiscV64 Helper
 *
 * Copyright (c) 2021, 2024
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>

static inline uintptr_t __semi_call(uintptr_t type, uintptr_t arg0)
{
    register uintptr_t t asm("a0") = type;
    register uintptr_t a0 asm("a1") = arg0;
    asm(".option push\n\t"
        ".option norvc\n\t"
        ".balign 16\n\t"
        "slli zero, zero, 0x1f\n\t"
        "ebreak\n\t"
        "srai zero, zero, 0x7\n\t"
        ".option pop\n\t"
        : "+r" (t)          /* Output: read as input, written as return value */
        : "r" (a0)          /* Input: arg0 */
        : "memory");        /* Clobber: may modify memory, depending on type */
    return t;
}
