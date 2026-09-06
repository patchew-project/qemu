/* SPDX-License-Identifier: GPL-2.0-or-later */

/* An LPAD fault must be deliverable with HPM instruction counting enabled. */

	.option	norvc
	.option	norelax

	.text
	.global	_start
_start:
	lla	t0, trap
	csrw	mtvec, t0
	li	t0, 2		/* HW_INSTRUCTIONS */
	csrw	mhpmevent3, t0
	li	t0, 1 << 10	/* mseccfg.MLPE */
	csrs	0x747, t0

	/* x7[31:12] = 0 does not match the nonzero LPAD label. */
	li	t2, 0
	lla	a0, target
	jalr	ra, a0, 0
	j	fail

	.balign	4
target:
	.word	0x00001017	/* lpad 1 */
	/*
	 * Keep the LPAD check inside a multi-instruction TB. Its exception
	 * helper must not read icount before leaving generated code.
	 */
	.rept	16
	nop
	.endr
	j	fail

trap:
	csrr	t0, mcause
	li	t1, 18		/* Software-check exception */
	bne	t0, t1, fail
	csrr	t0, mtval
	li	t1, 2		/* Landing-pad fault */
	bne	t0, t1, fail
	csrr	t0, mepc
	lla	t1, target
	bne	t0, t1, fail
	li	a0, 0
	j	exit

fail:
	li	a0, 1

exit:
	lla	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */

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
