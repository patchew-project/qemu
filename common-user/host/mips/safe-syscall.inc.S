/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by linux-user/safe-syscall.S
 *
 * Written by Richard Henderson <rth@twiddle.net>
 * Copyright (C) 2021 Linaro, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "sys/regdef.h"
#include "sys/asm.h"

	.text
	.set	nomips16
	.set	noreorder

	.global safe_syscall_start
	.global safe_syscall_end
	.type	safe_syscall_start, @function
	.type	safe_syscall_end, @function

	/*
	 * This is the entry point for making a system call. The calling
	 * convention here is that of a C varargs function with the
	 * first argument an 'int *' to the signal_pending flag, the
	 * second one the system call number (as a 'long'), and all further
	 * arguments being syscall arguments (also 'long').
	 */

#if _MIPS_SIM == _ABIO32
/* Do not allocate a stack frame and store into the parameter space. */
#define FRAME   0
#define ERRNOP  4
#else
/* Allocate a stack frame and store into the first allocated slot. */
#define FRAME   16
#define ERRNOP  0
#endif

NESTED(safe_syscall_base, FRAME, ra)
	.cfi_startproc
#if _MIPS_SIM == _ABIO32
	/*
	 * The syscall calling convention is nearly the same as C:
	 * we enter with a0 == &signal_pending
	 *               a1 == &errno
	 *               a2 == syscall number
	 *               a3, stack == syscall arguments
	 *               and return the result in a0
	 * and the syscall instruction needs
	 *               v0 == syscall number
	 *               a0 ... a3, stack == syscall arguments
	 *               and returns the result in v0
	 * Shuffle everything around appropriately.
	 */
	move	t0, a0		/* signal_pending pointer */
	sw	a1, ERRNOP(sp)	/* errno pointer */
	move	v0, a2		/* syscall number */
	move	a0, a3		/* syscall arguments */
	lw	a1, 16(sp)
	lw	a2, 20(sp)
	lw	a3, 24(sp)
	lw	t4, 28(sp)
	lw	t5, 32(sp)
	lw	t6, 40(sp)
	lw	t7, 44(sp)
	sw	t4, 16(sp)
	sw	t5, 20(sp)
	sw	t6, 24(sp)
	sw	t7, 28(sp)
#else
	PTR_ADDIU sp, sp, -FRAME
	.cfi_adjust_cfa_offset FRAME

	/*
	 * The syscall calling convention is nearly the same as C:
	 * we enter with a0 == &signal_pending
	 *               a1 == &errno
	 *               a2 == syscall number
	 *               a3 ... a7, stack == syscall arguments
	 *               and return the result in a0
	 * and the syscall instruction needs
	 *               v0 == syscall number
	 *               a0 ... a5 == syscall arguments
	 *               and returns the result in v0
	 * Shuffle everything around appropriately.
	 */
	move	t0, a0		/* signal_pending pointer */
	PTR_S	a1, ERRNOP(sp)	/* save errno pointer */
	move	v0, a2		/* syscall number */
	move	a0, a3		/* syscall arguments */
	move	a1, a4
	move	a2, a5
	move	a3, a6
	move	a4, a7
	ld	a5, 16(sp)
#endif

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
	lw	t1, 0(t0)
	bnez	t1, 0f
	 nop
	syscall
safe_syscall_end:

	/* code path for having successfully executed the syscall */
	bnez	a3, 1f
	 nop
	jr	ra
	 PTR_ADDIU sp, sp, FRAME

	/* code path when we didn't execute the syscall */
0:	li	v0, QEMU_ERESTARTSYS

	/* code path setting errno */
1:	PTR_L	t0, ERRNOP(sp)
	sw	v0, 0(t0)		/* store errno */
	li	v0, -1
	jr	ra
	 PTR_ADDIU sp, sp, FRAME

	.cfi_endproc
END(safe_syscall_base)
