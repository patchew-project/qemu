/*
 * RISC-V boot code for multiarch test suite
 *
 * Copyright 2026 Tenstorrent USA Inc
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define MSTATUS_VS          0x00000600
#define MSTATUS_FS          0x00006000

.section .text._start
        .global _start
        .type _start, @function
_start:
        /* Enable FPU and vector unit */
        li      t0, (MSTATUS_FS | MSTATUS_VS)
        csrs    mstatus, t0
        la      sp, __stack_top
        call    main

exit:
        /* exit with main()'s return code */
        call    _exit
