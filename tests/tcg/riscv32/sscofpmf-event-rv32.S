/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Low-half selector writes preserve OF; high-half writes can clear it. */

	.option	norvc
	.option	norelax

	.macro	check_selector_write low_csr, high_csr, first_failure
	li	t4, \first_failure
	li	t0, 8			/* mcountinhibit.HPM3 */
	csrs	mcountinhibit, t0
	li	t1, 1 << 29		/* SINH: still count in M-mode. */
	csrw	0x723, t1		/* mhpmevent3h: OF is clear. */
	li	t1, 2			/* HW_INSTRUCTIONS */
	csrw	0x323, t1		/* mhpmevent3 */
	/* Set the counter to UINT64_MAX while inhibited. */
	li	t2, -1
	csrw	0xb03, t2		/* mhpmcounter3 */
	csrw	0xb83, t2		/* mhpmcounter3h */
	li	t3, 1 << 13		/* mip.LCOFIP */
	csrc	mip, t3

	/*
	 * With icount, this write accounts for the first increment after
	 * enabling HPM3. The counter wraps, setting OF. Updating bits 31:0
	 * must preserve both that OF and the existing SINH in bits 63:32.
	 */
	csrc	mcountinhibit, t0
	csrw	\low_csr, t1
	csrr	t2, 0x723
	li	t1, 0xa0000000		/* OF | SINH */
	bne	t1, t2, exit
	csrs	mcountinhibit, t0

	li	t4, \first_failure + 1
	csrr	t2, mip
	and	t2, t2, t3
	beqz	t2, exit

	/* Explicitly writing the high half must still be able to clear OF. */
	li	t4, \first_failure + 2
	li	t1, 1 << 29		/* Keep SINH, clear OF. */
	csrw	\high_csr, t1
	csrr	t2, 0x723
	bne	t1, t2, exit

	/* The high-half write must not change the selected event. */
	li	t4, \first_failure + 3
	csrr	t2, 0x323
	li	t1, 2
	bne	t1, t2, exit
	.endm

	.text
	.global _start
_start:
	/* Unexpected exceptions report the check in progress. */
	li	t4, 9
	lla	t0, exit
	csrw	mtvec, t0
	li	t0, 1 << 28		/* menvcfgh.CDE */
	csrw	0x31a, t0
	li	t0, 8
	csrw	mcounteren, t0		/* Delegate counter 3. */
	li	t0, 0x43
	csrw	0x150, t0		/* siselect: counter 3 */

	/* Checks 1-4 use machine CSRs; checks 5-8 use delegated aliases. */
	check_selector_write 0x323, 0x723, 1	/* mhpmevent3, mhpmevent3h */
	check_selector_write 0x152, 0x156, 5	/* sireg2, sireg5 */
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
