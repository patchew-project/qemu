/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define MAGIC_SYSCALL 4096
#define CALLBACK_BRIDGE_OP_START 1
#define CALLBACK_BRIDGE_OP_RESUME 2

    .text
    .hidden callback_qsort_resume_trampoline
    .globl callback_qsort
    .type callback_qsort, @function
callback_qsort:
    /*
     * callback_qsort(base, nmemb, elem_size, cmp_fn) arrives in
     * %rdi, %rsi, %rdx, and %rcx. Shuffle those into the x86_64 Linux
     * syscall ABI so START sees: op, base, nmemb, elem_size, cmp_fn,
     * trampoline.
     */
    mov %rcx, %r8
    mov %rdx, %r10
    mov %rsi, %rdx
    lea callback_qsort_resume_trampoline(%rip), %r9
    mov %rdi, %rsi
    mov $CALLBACK_BRIDGE_OP_START, %rdi
    mov $MAGIC_SYSCALL, %rax
    syscall
    ret

    .globl callback_qsort_resume_trampoline
    .type callback_qsort_resume_trampoline, @function
callback_qsort_resume_trampoline:
    /* Sign-extend the guest comparator result into the RESUME argument. */
    movslq %eax, %rsi
    mov $CALLBACK_BRIDGE_OP_RESUME, %rdi
    mov $MAGIC_SYSCALL, %rax
    syscall
    ret
