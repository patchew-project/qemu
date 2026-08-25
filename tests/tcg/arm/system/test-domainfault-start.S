/*
 * Vector table + minimal mode/stack init for domain-fault-ordering test.
 * (GitLab issue #4233)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
.syntax unified
.arm

.equ MODE_UND, 0x1B
.equ MODE_ABT, 0x17
.equ MODE_IRQ, 0x12
.equ MODE_FIQ, 0x11
.equ MODE_SVC, 0x13
.equ NOINT,    0xC0   /* IRQ+FIQ disabled */

.section .vectors, "ax"
.global _vectors
_vectors:
    b reset_handler
    b hang            /* undefined instruction */
    b hang            /* swi */
    b hang            /* prefetch abort */
    b data_abort_handler
    b hang            /* reserved */
    b hang            /* irq */
    b hang            /* fiq */

.section .text
reset_handler:
    msr cpsr_c, #(MODE_UND | NOINT)
    ldr sp, =und_stack_top
    msr cpsr_c, #(MODE_ABT | NOINT)
    ldr sp, =abt_stack_top
    msr cpsr_c, #(MODE_IRQ | NOINT)
    ldr sp, =irq_stack_top
    msr cpsr_c, #(MODE_FIQ | NOINT)
    ldr sp, =fiq_stack_top
    msr cpsr_c, #(MODE_SVC | NOINT)
    ldr sp, =svc_stack_top
    bl c_main
hang:
    b hang

data_abort_handler:
    mrc p15, 0, r0, c5, c0, 0   /* DFSR */
    mrc p15, 0, r1, c6, c0, 0   /* DFAR */
    bl report_fault
    b hang

.section .bss
.align 4
und_stack: .space 256
und_stack_top:
abt_stack: .space 256
abt_stack_top:
irq_stack: .space 256
irq_stack_top:
fiq_stack: .space 256
fiq_stack_top:
svc_stack: .space 4096
svc_stack_top:
