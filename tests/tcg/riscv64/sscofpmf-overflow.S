/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/* Program hpmcounter3 while no event is selected. */
	csrw	0x323, zero		/* mhpmevent3 */
	li	t0, -256
	csrw	0xb03, t0		/* mhpmcounter3 */

	/* Start counting retired instructions with overflow enabled. */
	li	t0, 2
	csrw	0x323, t0

	/* Cross the 64-bit unsigned overflow boundary. */
	.rept	1024
	nop
	.endr

	/* OF must be sticky and LCOFIP must pend even with LCOFIE clear. */
	li	t4, 0
	csrr	t0, 0x323
	srli	t1, t0, 63
	xori	t1, t1, 1
	or	t4, t4, t1

	csrr	t0, mip
	li	t1, 1 << 13
	and	t0, t0, t1
	sltu	t0, zero, t0
	xori	t0, t0, 1
	or	t4, t4, t0

	/* The counter wraps and continues counting after overflow. */
	csrr	t0, 0xb03
	li	t1, -256
	sltu	t0, t0, t1
	xori	t0, t0, 1
	or	t4, t4, t0

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
