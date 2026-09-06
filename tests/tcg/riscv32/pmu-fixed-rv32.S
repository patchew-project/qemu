/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/*
	 * Failure bits:
	 * 0: selecting cycles changes the initialized high half
	 * 1: a low-half write discards a carry into the visible high half
	 * 2: the high-half read does not match the value just written
	 */
	li	t4, 0
	csrw	0x323, zero		/* mhpmevent3 */
	csrw	0xb03, zero		/* mhpmcounter3 */
	li	t0, 1
	csrw	0xb83, t0		/* mhpmcounter3h */
	li	t0, 1
	csrw	0x323, t0		/* mhpmevent3: cycles */

	/* Starting the counter must preserve its initialized high half. */
	csrr	t0, 0xc83		/* hpmcounter3h */
	li	t1, 1
	xor	t0, t0, t1
	sltu	t0, zero, t0
	or	t4, t4, t0

	/*
	 * Start 256 cycles below 2 << 32.  With -icount shift=0, the
	 * following instructions carry into the visible high half.  A
	 * low-half write must replace only bits 31:0 and preserve that carry.
	 */
	csrw	0x323, zero		/* mhpmevent3 */
	li	t0, -256
	csrw	0xb03, t0		/* mhpmcounter3 */
	li	t0, 1
	csrw	0xb83, t0		/* mhpmcounter3h */
	li	t0, 1
	csrw	0x323, t0		/* mhpmevent3: cycles */
	.rept	512
	nop
	.endr
	li	t0, 0x1234
	csrw	0xb03, t0
	csrr	t0, 0xc83
	li	t1, 2
	xor	t0, t0, t1
	sltu	t0, zero, t0
	slli	t0, t0, 1
	or	t4, t4, t0

	/*
	 * Restart with a small low half so no carry can affect this check.
	 * Writing 2 to the running counter's high half must read back as 2.
	 */
	csrw	0x323, zero		/* mhpmevent3 */
	li	t0, 0x1234
	csrw	0xb03, t0		/* mhpmcounter3 */
	li	t0, 1
	csrw	0xb83, t0		/* mhpmcounter3h */
	li	t0, 1
	csrw	0x323, t0		/* mhpmevent3: cycles */
	li	t0, 2
	csrw	0xb83, t0
	csrr	t0, 0xc83
	li	t1, 2
	xor	t0, t0, t1
	sltu	t0, zero, t0
	slli	t0, t0, 2
	or	t4, t4, t0

	la	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sw	t0, 0(a1)
	sw	t4, 4(a1)
	li	a0, 0x20		/* TARGET_SYS_EXIT_EXTENDED */

	/* Semihosting call sequence. */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	8
