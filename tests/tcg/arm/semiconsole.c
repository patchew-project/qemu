/*
 * linux-user semihosting console
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdint.h>

#define SYS_READC  0x7

uintptr_t __semi_call(uintptr_t type, uintptr_t arg0)
{
#if defined(__arm__)
    register uintptr_t t asm("r0") = type;
    register uintptr_t a0 asm("r1") = arg0;
    asm("svc 0xab"
        : "=r" (t)
        : "r" (t), "r" (a0));
#else
    register uintptr_t t asm("x0") = type;
    register uintptr_t a0 asm("x1") = arg0;
    asm("hlt 0xf000"
        : "=r" (t)
        : "r" (t), "r" (a0));
#endif

    return t;
}

int main(void)
{
    char c;

    printf("Semihosting Console Test\n");
    printf("hit X to exit:");

    do {
        c = __semi_call(SYS_READC, 0);
        printf("got '%c'\n", c);
    } while (c != 'X');

    return 0;
}
