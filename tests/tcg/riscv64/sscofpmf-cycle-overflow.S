/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/* UINT64_MAX - 4095 leaves 4096 cycle increments until overflow. */
	csrw	0x323, zero		/* mhpmevent3 */
	li	t0, -4096
	csrw	0xb03, t0		/* mhpmcounter3 */
	li	t0, 1
	csrw	0x323, t0		/* mhpmevent3: cycles */
	csrr	t1, mcycle

1:
	csrr	t0, 0x323
	beqz	t0, fail
	li	t2, 1
	slli	t2, t2, 63
	and	t0, t0, t2
	bnez	t0, pass

	/*
	 * Allow 16384 cycles for OF to become visible. With shift=3, scaling
	 * the 4096-cycle distance twice would delay it to about 32768 cycles.
	 */
	csrr	t0, mcycle
	sub	t0, t0, t1
	li	t2, 16384
	bltu	t0, t2, 1b

fail:
	li	a0, 1
	j	exit

pass:
	li	a0, 0

exit:
	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
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
	.space	16
