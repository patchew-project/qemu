/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
.option norvc
.text
.global _start
_start:
    # Argument block for SYS_OPEN (each arg is 4 bytes in RV32)
    la      a1, open_args
    la      t0, test_filename
    sw      t0, 0(a1)          # arg0: pointer to filename
    li      t0, 4              # arg1: "w" (write, create, truncate)
    sw      t0, 4(a1)
    li      t0, -1             # arg2: auto length (-1 = 0xffffffff)
    sw      t0, 8(a1)

    # SYS_OPEN semihosting call
    li      a0, 0x01
    .balign 16
    slli    zero, zero, 0x1f
    ebreak
    srai    zero, zero, 0x7

    # Check result: a0 should be a valid file descriptor (not -1)
    li      t0, -1
    beq     a0, t0, fail

    # Success
    li      a0, 0
    j       exit

fail:
    li      a0, 1

exit:
    # Extended semihosting exit
    la      a1, semiargs
    li      t0, 0x20026        # ADP_Stopped_ApplicationExit
    sw      t0, 0(a1)
    sw      a0, 4(a1)
    li      a0, 0x20           # TARGET_SYS_EXIT_EXTENDED
    .balign 16
    slli    zero, zero, 0x1f
    ebreak
    srai    zero, zero, 0x7

    j       .

.data
.balign 16
test_filename:
    .string "testfile.txt"
open_args:
    .space 12                  # 3 * 4 bytes
semiargs:
    .space 8                   # 2 * 4 bytes
