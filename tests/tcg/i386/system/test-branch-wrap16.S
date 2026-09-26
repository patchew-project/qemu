/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A 16-bit near branch must truncate EIP to 16 bits.
 *
 * The test jumps from IP 0xFFF0 to IP 0x0020 in 16-bit code. The jump
 * crosses 0xFFFF. For correct 16-bit behaviour the CPU should wrap the
 * target to 0x0020, rather than branching to 0x10020.
 *
 * TCG used to skip the wrap when the untruncated target (CS base +
 * 0x10020) was on the same guest page as the jump. That only happens
 * when the CS base is not page aligned, so the code segment starts at
 * a page boundary plus 0x10.
 */

#define SEL_CODE32   0x08
#define SEL_CODE16   0x18

        .data

/* 0x08 and 0x10 must match boot.S, which loaded the data selectors */
        .balign 8
gdt16:
        .quad 0
        .word 0xFFFF, 0
        .byte 0, 0x9b, 0xCF, 0
        .word 0xFFFF, 0
        .byte 0, 0x93, 0xCF, 0
gdt16_code16:
        /* 16-bit code, 64 KiB limit; main fills in the base */
        .word 0xFFFF, 0
        .byte 0, 0x9b, 0x00, 0
gdt16_end:

gdtr16:
        .word gdt16_end - gdt16 - 1
        .long gdt16

fail_msg:
        .asciz "FAIL: branch left EIP untruncated\n"
pass_msg:
        .asciz "PASS\n"

        .section .text16, "ax"
        .code16

        /*
         * Page aligned plus 0x10. A larger alignment breaks the
         * multiboot load in boot.S.
         */
        .balign 0x1000
        .skip 0x10
wrap16_cs_base:

        .skip 0x10
wrap16_target:
        /* jmp lands here when EIP wraps to 0x0020 */
        ljmpl $SEL_CODE32, $test_passed

        .org wrap16_cs_base + 0xfff0
wrap16_entry:
        /*
         * The problematic branch, which should wrap EIP:
         * 16-bit relative 'jmp target', hand-assembled because clang rejects a
         * rel16 displacement outside [-0x8000, 0x7FFF]. But, in 16-bit code, any
         * 16-bit displacement is valid. GAS correctly accepts any 16-bit offset.
         */
        .byte 0xe9                              /* jmp rel16 opcode */
        .word wrap16_target - (. + 2)           /* cw: 16-bit offset */

        .org wrap16_cs_base + 0x10020
wrap16_fail:
        /* jmp lands here when EIP does not wrap (0x10020) */
        ljmpl $SEL_CODE32, $test_failed

        .text
        .code32
        .globl main
main:
        movl $wrap16_cs_base, %eax
        movw %ax, gdt16_code16 + 2
        shrl $16, %eax
        movb %al, gdt16_code16 + 4
        movb %ah, gdt16_code16 + 7
        lgdt gdtr16

        ljmpw $SEL_CODE16, $0xFFF0

test_passed:
        pushl $pass_msg
        call ml_printf
        addl $4, %esp
        xorl %eax, %eax
        ret

test_failed:
        pushl $fail_msg
        call ml_printf
        addl $8, %esp
        movl $1, %eax
        ret
