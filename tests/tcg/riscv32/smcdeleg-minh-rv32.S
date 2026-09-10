/* SPDX-License-Identifier: GPL-2.0-or-later */

/* CSR numbers for older assemblers. */
#define CSR_SISELECT         0x150
#define CSR_SIREG5           0x156
#define CSR_MENVCFGH         0x31a
#define CSR_MCYCLECFGH       0x721
#define CSR_MINSTRETCFGH     0x722

	.option	norvc
	.option	norelax

	.text
	.global	_start
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
	csrw	CSR_MCYCLECFGH, t0
	csrw	CSR_MINSTRETCFGH, t0
	li	t1, 1
	slli	t1, t1, 28		/* menvcfgh.CDE */
	csrw	CSR_MENVCFGH, t1
	li	t1, 5			/* Delegate cycle and instret. */
	csrw	mcounteren, t1

	/* Check the delegated view, then the underlying machine register. */
	li	t1, 0x40		/* siselect: cycle */
	csrw	CSR_SISELECT, t1
	csrr	t1, CSR_SIREG5
	and	t1, t1, t0
	sltu	t1, zero, t1
	or	t4, t4, t1
	csrr	t1, CSR_MCYCLECFGH
	and	t1, t1, t0
	sltu	t1, zero, t1
	xori	t1, t1, 1
	slli	t1, t1, 1
	or	t4, t4, t1

	li	t1, 0x42		/* siselect: instret */
	csrw	CSR_SISELECT, t1
	csrr	t1, CSR_SIREG5
	and	t1, t1, t0
	sltu	t1, zero, t1
	slli	t1, t1, 2
	or	t4, t4, t1
	csrr	t1, CSR_MINSTRETCFGH
	and	t1, t1, t0
	sltu	t1, zero, t1
	xori	t1, t1, 1
	slli	t1, t1, 3
	or	t4, t4, t1

	lla	a1, semiargs
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
