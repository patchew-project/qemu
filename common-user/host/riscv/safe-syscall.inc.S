/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by linux-user/safe-syscall.S
 *
 * Written by Richard Henderson <rth@twiddle.net>
 * Copyright (C) 2018 Linaro, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

	.global safe_syscall_base
	.global safe_syscall_start
	.global safe_syscall_end
	.type	safe_syscall_base, @function
	.type	safe_syscall_start, @function
	.type	safe_syscall_end, @function

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
	 * The syscall calling convention is nearly the same as C:
	 * we enter with a0 == &signal_pending
	 *               a1 == &errno
	 *               a2 == syscall number
	 *               a3 ... a7, [sp] == syscall arguments
	 *               and return the result in a0
	 * and the syscall instruction needs
	 *               a7 == syscall number
	 *               a0 ... a5 == syscall arguments
	 *               and returns the result in a0
	 * Shuffle everything around appropriately.
	 */
	mv	t0, a0		/* signal_pending pointer */
	mv	t1, a1		/* errno pointer */
	mv	t2, a2		/* syscall number */
	mv	a0, a3		/* syscall arguments */
	mv	a1, a4
	mv	a2, a5
	mv	a3, a6
	mv	a4, a7
#if __riscv_xlen == 32
	lw	a5, 0(sp)
#else
	ld	a5, 0(sp)
#endif
	mv	a7, t2

	/*
	 * This next sequence of code works in conjunction with the
	 * rewind_if_safe_syscall_function(). If a signal is taken
	 * and the interrupted PC is anywhere between 'safe_syscall_start'
	 * and 'safe_syscall_end' then we rewind it to 'safe_syscall_start'.
	 * The code sequence must therefore be able to cope with this, and
	 * the syscall instruction must be the final one in the sequence.
	 */
safe_syscall_start:
	/* If signal_pending is non-zero, don't do the call */
	lw	t2, 0(t0)
	bnez	t2, 2f
	scall
safe_syscall_end:

	/* code path for having successfully executed the syscall */
	li	t2, -4096
	bgtu	a0, t2, 0f
	ret

	/* code path setting errno */
0:	neg	a0, a0			/* create positive errno */
1:	sw	a0, 0(t1)		/* store errno */
	li	a0, -1
	ret

	/* code path when we didn't execute the syscall */
2:	li	a0, QEMU_ERESTARTSYS
	j	1b

	.cfi_endproc

	.size	safe_syscall_base, .-safe_syscall_base
