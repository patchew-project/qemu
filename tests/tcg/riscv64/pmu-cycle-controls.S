/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Check cycle-counter behavior across filter and inhibit control writes. */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/*
	 * Failure bits:
	 * 0: enabling MINH discards the previously accumulated value
	 * 1: mcycle changes while M-mode is filtered
	 * 2: disabling MINH adds the filtered interval
	 * 3: mcycle does not resume after disabling MINH
	 * 4: writing mhpmevent3 advances mcycle while CY is set
	 * 5: mcycle does not resume after clearing CY
	 */
	li	t4, 0
	csrw	mcountinhibit, zero
	csrw	0x321, zero		/* mcyclecfg */

	/* Filtering M-mode must not discard the value accumulated so far. */
	csrr	s0, mcycle
	.rept	64
	nop
	.endr
	li	t0, 1
	slli	t0, t0, 62		/* MINH */
	csrw	0x321, t0
	csrr	s1, mcycle
	sltu	t1, s0, s1
	xori	t1, t1, 1
	or	t4, t4, t1
	.rept	128
	nop
	.endr
	csrr	s2, mcycle
	xor	t1, s1, s2
	sltu	t1, zero, t1
	slli	t1, t1, 1
	or	t4, t4, t1

	/* Removing the filter must not add the inhibited interval. */
	csrw	0x321, zero
	csrr	s3, mcycle
	sub	t1, s3, s2
	li	t2, 64
	sltu	t1, t1, t2
	xori	t1, t1, 1
	slli	t1, t1, 2
	or	t4, t4, t1
	.rept	128
	nop
	.endr
	csrr	t1, mcycle
	sltu	t1, s3, t1
	xori	t1, t1, 1
	slli	t1, t1, 3
	or	t4, t4, t1

	/* Changing HPM3's event must leave mcycle stopped while CY is set. */
	li	t0, 1			/* mcountinhibit.CY */
	csrw	mcountinhibit, t0
	csrr	s4, mcycle
	.rept	128
	nop
	.endr
	li	t0, 1			/* HW_CPU_CYCLES */
	csrw	0x323, t0		/* mhpmevent3; rebuilds PMU timer */
	csrr	s5, mcycle
	xor	t1, s4, s5
	sltu	t1, zero, t1
	slli	t1, t1, 4
	or	t4, t4, t1
	csrw	mcountinhibit, zero
	csrr	s6, mcycle
	.rept	128
	nop
	.endr
	csrr	t1, mcycle
	sltu	t1, s6, t1
	xori	t1, t1, 1
	slli	t1, t1, 5
	or	t4, t4, t1
	csrw	0x323, zero
	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	t4, 8(a1)
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
