/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <asm/unistd.h>

        .text
        .globl  _start
_start:
        .option norvc

        li      a0, 4
        la      a1, sa
        li      a2, 0
        li      a3, 8
        li      a7, __NR_rt_sigaction
        scall

        .word   0xe0030453

        li      a0, 1
        j       exit

pass:
        li      a0, 0

exit:
        li      a7, __NR_exit
        scall

        .data
        .type   sa, @object
        .size   sa, 32
sa:
        .dword  pass
        .zero   24
