/* SPDX-License-Identifier: GPL-2.0-or-later */

/* RV32 sireg2 writes replace only the low half of a delegated selector. */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/* Any unexpected exception fails with the current check number. */
	li	t4, 1
	lla	t0, exit
	csrw	mtvec, t0
	li	t0, 8			/* mcountinhibit.HPM3 */
	csrs	mcountinhibit, t0
	li	t0, 0xe0000000		/* OF | MINH | SINH */
	csrw	0x723, t0		/* mhpmevent3h */
	li	t0, 1			/* HW_CPU_CYCLES */
	csrw	0x323, t0		/* mhpmevent3 */

	li	t0, 1 << 28		/* menvcfgh.CDE */
	csrw	0x31a, t0
	li	t0, 8			/* Delegate counter 3. */
	csrw	mcounteren, t0
	li	t0, 0x43
	csrw	0x150, t0		/* siselect: counter 3 */

	/* Replace the event without changing OF or the privilege filters. */
	li	t0, 2			/* HW_INSTRUCTIONS */
	csrw	0x152, t0		/* sireg2: hpmevent3 */
	li	t4, 2
	csrr	t1, 0x323
	bne	t0, t1, exit
	li	t4, 3
	li	t0, 0xe0000000
	csrr	t1, 0x723
	bne	t0, t1, exit

	/* Selecting event zero must also leave the high half unchanged. */
	li	t4, 4
	csrw	0x152, zero
	li	t4, 5
	csrr	t1, 0x323
	bnez	t1, exit
	li	t4, 6
	csrr	t1, 0x723
	bne	t0, t1, exit
	li	t4, 0

	.balign	4
exit:
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
