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
    csrr    s0, misa
    srli    (s1, s0, 126)
    li      s0, 3
    bne     s1, s0, fail

    # Check UXL and SXL are set to RV128 too
    csrr    s0, mstatus
    srli    (s1, s0, 32)
    andi    s1, s1, 0xf
    li      s0, 0xf
    bne     s1, s0, fail

    # Write some values in mcause (wlrl)
    # 13 is valid for both interruptions and exceptions
    li      s0, 13
    csrw    mcause, s0
    csrr    s1, mcause
    bne     s1, s0, fail

    li      s1, 1
    slli    (s1, s1, 127)
    or      s1, s1, s0
    csrw    mcause, s1
    csrr    s0, mcause
    bne     s1, s0, fail

    # Write an illegal value, but as for the 32/64-bit versions,
    # we keep it as it is
    li      s1, -1
    csrw    mcause, s1
    csrr    s0, mcause
    bne     s1, s0, fail

    # Set the trap vector
    la       s0, tvecall
    csrw     mtvec, s0
    # Do an ecall to make sure mcause is properly set
    ecall
    # Set the trap vector
    la       s0, tvbreak
    csrw     mtvec, s0
    # Do an ecall to make sure mcause is properly set
    ebreak
    li      a0, 0
    j       _exit

    # mtvec bit 1:0 indicate mode, 00 is Direct, what
    # we want in this simple test
    .align 4
tvecall:
    csrr    s0, mcause
    li      s1, 11 # ecall from m-mode
    bne     s1, s0, fail
    csrr    s0, mepc
    addi    s0, s0, 4
    csrw    mepc, s0
    mret

    .align 4
tvbreak:
    csrr    s0, mcause
    li      s1, 3 # break
    bne     s1, s0, fail
    csrr    s0, mepc
    addi    s0, s0, 4
    csrw    mepc, s0
    mret
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
