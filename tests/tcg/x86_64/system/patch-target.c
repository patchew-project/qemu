/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2025, Rowan Hart <rowanbhart@gmail.com>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * This test target increments a value 100 times. The patcher converts the
 * inc instruction to a nop, so it only increments the value once.
 *
 */
#include <minilib.h>

int main(void)
{
    ml_printf("Running test...\n");
#if defined(__x86_64__)
    ml_printf("Testing insn memory read/write...\n");
    unsigned int x = 0;
    for (int i = 0; i < 100; i++) {
        asm volatile (
            "inc %[x]"
            : [x] "+a" (x)
        );
    }
    ml_printf("Value: %d\n", x);
#else
    #error "This test is only valid for x86_64 architecture."
#endif
    return 0;
}
