/*
 * Zl lengths, required fetches and IALIGN=16 with C/Zca disabled.
 * Run on virt with 16 MiB RAM: 0x80000000 <= address < 0x81000000.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
    .option norvc
    .option norelax
    .text
    .global _start

    /* s0: faulting PC; s1: cause; s2: tval; s3: resume PC. */
    .macro expect_exception cause, tval
    li s1, \cause
    li s2, \tval
    lla s3, 1f
    jr s0
1:
    .endm

_start:
    lla t0, trap_handler
    csrw mtvec, t0
    csrw mie, zero
    li s4, 0
    csrr t0, misa
    andi t0, t0, 4
    bnez t0, failed

    /* JALR to a 48-bit candidate at RAM end; D must not be fetched. */
    li s0, 0x80fffffa
    lla t0, insn48
    lhu t1, 0(t0)
    sh t1, 0(s0)
    lhu t1, 2(t0)
    sh t1, 2(s0)
    lhu t1, 4(t0)
    sh t1, 4(s0)
    fence.i
#if __riscv_xlen == 64
    expect_exception 2, 0x5a692468003f
#else
    expect_exception 2, 0x2468003f
#endif

    /* Undefined C still stops after A/B/C, with no instruction bits. */
    li t0, 0x5a7f
    sh t0, 4(s0)
    fence.i
    expect_exception 2, 0

    lla s0, insn64
#if __riscv_xlen == 64
    expect_exception 2, 0x9abc5a632468001f
#else
    expect_exception 2, 0x2468001f
#endif

    /* A/B present, C missing: fault at the first inaccessible parcel. */
    li s0, 0x80fffffc
    li t0, 0x2468003f
    sw t0, 0(s0)
    fence.i
    expect_exception 1, 0x81000000

    /* A/B/C present, D missing; C requests eight bytes. */
    li s0, 0x80fffffa
    lla t0, insn64
    lhu t1, 0(t0)
    sh t1, 0(s0)
    lhu t1, 2(t0)
    sh t1, 2(s0)
    lhu t1, 4(t0)
    sh t1, 4(s0)
    fence.i
    expect_exception 1, 0x81000000

    /* JAL and a word-aligned BEQ must reach halfword targets. */
    jal zero, half_jal
    j failed
    .balign 4, 0
    .2byte 0
half_jal:
    j branch_entry
    .balign 4, 0
branch_entry:
    beq zero, zero, half_branch
    j failed
    .balign 4, 0
    .2byte 0
half_branch:
    lla t0, half_return
    andi t1, t0, 3
    li t2, 2
    bne t1, t2, failed
    csrw mepc, t0
    csrr t1, mepc
    bne t0, t1, failed
    li t1, 0x1800
    csrs mstatus, t1
    mret
    .balign 4, 0
    .2byte 0
half_return:
    lla s0, compressed
    expect_exception 2, 1
    li t0, 6
    bne s4, t0, failed
    j passed

    .balign 4, 0
trap_handler:
    csrr t0, mcause
    bne t0, s1, failed
    csrr t0, mepc
    bne t0, s0, failed
    csrr t0, mtval
    bne t0, s2, failed
    addi s4, s4, 1
    csrw mepc, s3
    mret

    /* A alone would select the opposite length under the old encoding. */
insn48:
    .2byte 0x003f, 0x2468, 0x5a69
insn64:
    .2byte 0x001f, 0x2468, 0x5a63, 0x9abc
compressed:
    .2byte 0x0001, 0

    .balign 4, 0
passed:
    li a0, 4
    lla a1, pass
    .balign 16
    slli zero, zero, 31
    ebreak
    srai zero, zero, 7
    li a0, 0
    j exit
failed:
    li a0, 1
exit:
    lla a1, semiargs
#if __riscv_xlen == 64
    sd a0, 8(a1)
#else
    sw a0, 4(a1)
#endif
    li a0, 0x20
    .balign 16
    slli zero, zero, 31
    ebreak
    srai zero, zero, 7
    j .

    .data
    .balign 8
semiargs:
#if __riscv_xlen == 64
    .dword 0x20026, 0
#else
    .word 0x20026, 0
#endif
pass:
    .asciz "Zl PASS\n"
