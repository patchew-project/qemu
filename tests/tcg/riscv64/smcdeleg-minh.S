/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/*
	 * Failure bits:
	 * 0: delegated cyclecfg exposes MINH
	 * 1: reading delegated cyclecfg clears mcyclecfg.MINH
	 * 2: delegated instretcfg exposes MINH
	 * 3: reading delegated instretcfg clears minstretcfg.MINH
	 */
	li	t4, 0
	li	t0, 1
	slli	t0, t0, 62		/* MINH */
	csrw	0x321, t0		/* mcyclecfg */
	csrw	0x322, t0		/* minstretcfg */
	li	t1, 1
	slli	t1, t1, 60		/* menvcfg.CDE */
	csrw	0x30a, t1
	li	t1, 5			/* delegate cycle and instret */
	csrw	mcounteren, t1

	li	t1, 0x40
	csrw	0x150, t1		/* siselect: cycle */
	csrr	t1, 0x152		/* sireg2: cyclecfg */
	and	t1, t1, t0
	sltu	t1, zero, t1
	or	t4, t4, t1
	csrr	t1, 0x321		/* mcyclecfg: MINH remains set */
	and	t1, t1, t0
	sltu	t1, zero, t1
	xori	t1, t1, 1
	slli	t1, t1, 1
	or	t4, t4, t1

	li	t1, 0x42
	csrw	0x150, t1		/* siselect: instret */
	csrr	t1, 0x152		/* sireg2: instretcfg */
	and	t1, t1, t0
	sltu	t1, zero, t1
	slli	t1, t1, 2
	or	t4, t4, t1
	csrr	t1, 0x322		/* minstretcfg: MINH remains set */
	and	t1, t1, t0
	sltu	t1, zero, t1
	xori	t1, t1, 1
	slli	t1, t1, 3
	or	t4, t4, t1

	lla	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	t4, 8(a1)
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
