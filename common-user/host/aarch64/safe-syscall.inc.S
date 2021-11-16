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
	.type	safe_syscall_base, #function
	.type	safe_syscall_start, #function
	.type	safe_syscall_end, #function

	/*
	 * This is the entry point for making a system call. The calling
	 * convention here is that of a C varargs function with the
	 * first argument an 'int *' to the signal_pending flag, the
	 * second one the system call number (as a 'long'), and all further
	 * arguments being syscall arguments (also 'long').
	 */
safe_syscall_base:
	.cfi_startproc
	/*
         * The syscall calling convention isn't the same as the C one:
	 * we enter with x0 == *signal_pending
	 *               x1 == errno
	 *               x2 == syscall number
	 *               x3 ... x7, (stack) == syscall arguments
	 *               and return the result in x0
	 * and the syscall instruction needs
	 *               x8 == syscall number
	 *               x0 ... x6 == syscall arguments
	 *               and returns the result in x0
	 * Shuffle everything around appropriately.
	 */
	mov	x10, x0		/* signal_pending pointer */
	mov	x11, x1		/* errno pointer */
	mov	x8, x2		/* syscall number */
	mov	x0, x3		/* syscall arguments */
	mov	x1, x4
	mov	x2, x5
	mov	x3, x6
	mov	x4, x7
	ldp	x5, x6, [sp]

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
	ldr	w9, [x10]
	cbnz	w9, 2f
	svc	0x0
safe_syscall_end:

	/* code path for having successfully executed the syscall */
#if defined(__linux__)
	/* Linux kernel returns (small) negative errno. */
	cmn	x0, #4096
	b.hi	0f
#elif defined(__FreeBSD__)
	/* FreeBSD kernel returns positive errno and C bit set. */
	b.cs	1f
#else
#error "unsupported os"
#endif
	ret

	/* code path setting errno */
#ifdef __linux__
0:	neg	w0, w0			/* create positive errno */
#endif
1:	str	w0, [x11]		/* store errno */
	mov	x0, #-1
	ret

	/* code path when we didn't execute the syscall */
2:	mov	w0, #TARGET_ERESTARTSYS
	b	1b

	.cfi_endproc

	.size	safe_syscall_base, .-safe_syscall_base
