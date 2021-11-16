/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by linux-user/safe-syscall.S
 *
 * Written by Richard Henderson <rth@twiddle.net>
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

	.global safe_syscall_base
	.global safe_syscall_start
	.global safe_syscall_end
	.type	safe_syscall_base, %function

	.cfi_sections	.debug_frame

	.text
	.syntax unified
	.arm
	.align 2

	/*
	 * This is the entry point for making a system call. The calling
	 * convention here is that of a C varargs function with the
	 * first argument an 'int *' to the signal_pending flag, the
	 * second one the system call number (as a 'long'), and all further
	 * arguments being syscall arguments (also 'long').
	 */
safe_syscall_base:
	.fnstart
	.cfi_startproc
	mov	r12, sp			/* save entry stack */
	push	{ r4, r5, r6, r7, r8, r9, r10, lr }
	.save	{ r4, r5, r6, r7, r8, r9, r10, lr }
	.cfi_adjust_cfa_offset 32
	.cfi_rel_offset r4, 0
	.cfi_rel_offset r5, 4
	.cfi_rel_offset r6, 8
	.cfi_rel_offset r7, 12
	.cfi_rel_offset r8, 16
	.cfi_rel_offset r9, 20
	.cfi_rel_offset r10, 24
	.cfi_rel_offset lr, 28

	/*
	 * The syscall calling convention isn't the same as the C one:
	 * we enter with r0 == &signal_pending
         *               r1 == &errno
	 *               r2 == syscall number
	 *               r3, [sp+0] ... [sp+16] == syscall arguments
	 *               and return the result in r0
	 * and the syscall instruction needs
	 *               r7 == syscall number
	 *               r0 ... r6 == syscall arguments
	 *               and returns the result in r0
	 * Shuffle everything around appropriately.
	 * Note the 16 bytes that we pushed to save registers.
	 */
	mov	r8, r0			/* copy signal_pending */
	mov	r9, r1			/* copy errnop */
	mov	r7, r2			/* syscall number */
	mov	r0, r3			/* syscall args */
	ldm	r12, { r1, r2, r3, r4, r5, r6 }

	/*
	 * This next sequence of code works in conjunction with the
	 * rewind_if_safe_syscall_function(). If a signal is taken
	 * and the interrupted PC is anywhere between 'safe_syscall_start'
	 * and 'safe_syscall_end' then we rewind it to 'safe_syscall_start'.
	 * The code sequence must therefore be able to cope with this, and
	 * the syscall instruction must be the final one in the sequence.
	 */
safe_syscall_start:
	/* if signal_pending is non-zero, don't do the call */
	ldr	r12, [r8]		/* signal_pending */
	tst	r12, r12
	bne	2f
	swi	0
safe_syscall_end:

	/* code path for having successfully executed the syscall */
	cmp	r0, #-4096
	bhi	0f
9:	pop	{ r4, r5, r6, r7, r8, r9, r10, pc }

	/* code path setting errno */
0:	neg	r0, r0		/* create positive errno */
1:	str	r0, [r9]	/* store errno */
	mov	r0, #-1
	b	9b

	/* code path when we didn't execute the syscall */
2:	ldr	r0, =TARGET_ERESTARTSYS
	b	1b

	.fnend
	.cfi_endproc

	.size	safe_syscall_base, .-safe_syscall_base
