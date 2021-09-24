/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by linux-user/safe-syscall.S
 *
 * Ported to LoongArch by WANG Xuerui <git@xen0n.name>
 *
 * Based on safe-syscall.inc.S code for every other architecture,
 * originally written by Richard Henderson <rth@twiddle.net>
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
	 * We return a long which is the syscall's return value, which
	 * may be negative-errno on failure. Conversion to the
	 * -1-and-errno-set convention is done by the calling wrapper.
	 */
safe_syscall_base:
	.cfi_startproc
	/*
	 * The syscall calling convention is nearly the same as C:
	 * we enter with a0 == *signal_pending
	 *               a1 == syscall number
	 *               a2 ... a7 == syscall arguments
	 *               and return the result in a0
	 * and the syscall instruction needs
	 *               a7 == syscall number
	 *               a0 ... a5 == syscall arguments
	 *               and returns the result in a0
	 * Shuffle everything around appropriately.
	 */
	move	$t0, $a0	/* signal_pending pointer */
	move	$t1, $a1	/* syscall number */
	move	$a0, $a2	/* syscall arguments */
	move	$a1, $a3
	move	$a2, $a4
	move	$a3, $a5
	move	$a4, $a6
	move	$a5, $a7
	move	$a7, $t1

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
	ld.w	$t1, $t0, 0
	bnez	$t1, 0f
	syscall	0
safe_syscall_end:
	/* code path for having successfully executed the syscall */
	jr	$ra

0:
	/* code path when we didn't execute the syscall */
	li.w	$a0, -TARGET_ERESTARTSYS
	jr	$ra
	.cfi_endproc

	.size	safe_syscall_base, .-safe_syscall_base
