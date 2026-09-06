/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/*
	 * Failure bits:
	 * 0: delegated cyclecfgh exposes MINH
	 * 1: reading delegated cyclecfgh clears mcyclecfgh.MINH
	 * 2: delegated instretcfgh exposes MINH
	 * 3: reading delegated instretcfgh clears minstretcfgh.MINH
	 */
	li	t4, 0
	li	t0, 1
	slli	t0, t0, 30		/* MINH in the high half */
	csrw	0x721, t0		/* mcyclecfgh */
	csrw	0x722, t0		/* minstretcfgh */
	li	t1, 1
	slli	t1, t1, 28		/* menvcfgh.CDE */
	csrw	0x31a, t1
	li	t1, 5			/* delegate cycle and instret */
	csrw	mcounteren, t1

	li	t1, 0x40
	csrw	0x150, t1		/* siselect: cycle */
	csrr	t1, 0x156		/* sireg5: cyclecfgh */
	and	t1, t1, t0
	sltu	t1, zero, t1
	or	t4, t4, t1
	csrr	t1, 0x721
	and	t1, t1, t0
	sltu	t1, zero, t1
	xori	t1, t1, 1
	slli	t1, t1, 1
	or	t4, t4, t1

	li	t1, 0x42
	csrw	0x150, t1		/* siselect: instret */
	csrr	t1, 0x156		/* sireg5: instretcfgh */
	and	t1, t1, t0
	sltu	t1, zero, t1
	slli	t1, t1, 2
	or	t4, t4, t1
	csrr	t1, 0x722
	and	t1, t1, t0
	sltu	t1, zero, t1
	xori	t1, t1, 1
	slli	t1, t1, 3
	or	t4, t4, t1

	la	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sw	t0, 0(a1)
	sw	t4, 4(a1)
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
	.space	8
