/*
 * Test rv128 mcause update and access
 * Author: Frédéric Pétrot <frederic.petrot@univ-grenoble-alpes.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define slli(rd, rs1, imm) .insn i  0b0010011, 0b001, rd, rs1, imm & 0b1111111
#define srli(rd, rs1, imm) .insn i  0b0010011, 0b101, rd, rs1, imm & 0b1111111

    .option norvc

    .text
    .globl _start
_start:
    # Check RV128 is running
    csrr    s0, misa
    srli    (s1, s0, 126)
    li      s0, 3
    bne     s1, s0, fail

    # Minimal pmp settings
#define PMP_NAPOT 0x18
#define PMP_R     0x01
#define PMP_W     0x02
#define PMP_X     0x04

    li       s0, -1
    csrw     pmpaddr0, s0
    li       s0, PMP_NAPOT | PMP_R | PMP_W | PMP_X
    csrw     pmpcfg0, s0

    # Set MPP to supervisor
    csrr    s0, mstatus
    li      s1, ~0x1800 // mask
    and     s1, s1, s0
    li      s0, 0x0800  // supervisor
    or      s1, s1, s0
    csrw    mstatus, s1
    # Change to supervisor mode
    la      s0, 1f
    csrw    mepc, s0
    mret
1:
    # Check UXL is set to RV128
    csrr    s0, sstatus
    srli    (s1, s0, 32)
    andi    s1, s1, 0xf
    li      s0, 0x3
    bne     s1, s0, fail

    # Write some values in scause (wlrl)
    # 13 is valid for both interruptions and exceptions
    li      s0, 13
    csrw    scause, s0
    csrr    s1, scause
    bne     s1, s0, fail

    li      s1, 1
    slli    (s1, s1, 127)
    or      s1, s1, s0
    csrw    scause, s1
    csrr    s0, scause
    bne     s1, s0, fail

    # Write an illegal value, but as for the 32/64-bit versions,
    # we keep it as it is
    li      s1, -1
    csrw    scause, s1
    csrr    s0, scause
    bne     s1, s0, fail

    li      a0, 0
    j       _exit

fail:
    li      a0, 1
_exit:
    lla     a1, semiargs
    li      t0, 0x20026 # ADP_Stopped_ApplicationExit
    sd      t0, 0(a1)
    sd      a0, 8(a1)
    li      a0, 0x20    # TARGET_SYS_EXIT_EXTENDED

    # Semihosting call sequence
    .balign 16
    slli   zero, zero, 0x1f
    ebreak
    srai   zero, zero, 0x7
    j      .

    .data
    .balign 16
semiargs:
    .space  16
