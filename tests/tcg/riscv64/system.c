/*
 * RISC-V semihosting support for multiarch test suite
 *
 * Copyright 2026 Tenstorrent USA Inc
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "semicall.h"

#define SYS_WRITEC      0x3
#define SYS_EXIT        0x18

#define ADP_Stopped_ApplicationExit 0x20026

void __sys_outc(char c)
{
    __semi_call(SYS_WRITEC, (uintptr_t)&c);
}

void _exit(int code)
{
    const uintptr_t args[2] = { ADP_Stopped_ApplicationExit, (uintptr_t)code };

    __semi_call(SYS_EXIT, (uintptr_t)args);

    /* Hang if QEMU fails to exit */
    while (1) {
        asm volatile("wfi");
    }
}
