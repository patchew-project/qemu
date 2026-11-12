/* SPDX-License-Identifier: GPL-2.0-or-later */

	.option	norvc

	.text
	.global	_start
_start:
	lla	t0, trap
	csrw	mtvec, t0

	/*
	 * The first CSR read retires after obtaining s0.  The ecall does not
	 * retire, so the trap handler must observe a difference of one.
	 */
	csrr	s0, minstret
	ecall
	sub	t0, s1, s0
	li	t1, 1
	bne	t0, t1, fail

	/*
	 * minstret and a counter selecting HW_INSTRUCTIONS must both exclude
	 * ECALL, so they must contain the same number of retired instructions.
	 */
	li	t0, 12		/* mcountinhibit.IR | mcountinhibit.HPM3 */
	csrs	mcountinhibit, t0
	csrw	minstret, zero
	csrw	mhpmcounter3, zero
	li	t0, 2		/* RISCV_PMU_EVENT_HW_INSTRUCTIONS */
	csrw	mhpmevent3, t0
	lla	t0, trap_hpm
	csrw	mtvec, t0
	li	t0, 12
	csrc	mcountinhibit, t0
	ecall
	bne	s3, s4, fail

	li	a0, 0
	j	_exit

trap:
	csrr	s1, minstret
	j	trap_check

trap_hpm:
	li	t0, 12
	csrs	mcountinhibit, t0
	csrr	s3, minstret
	csrr	s4, mhpmcounter3

trap_check:
	csrr	t0, mcause
	li	t1, 11		/* Environment call from M-mode */
	bne	t0, t1, fail

	csrr	t0, mepc
	addi	t0, t0, 4
	csrw	mepc, t0
	mret

fail:
	li	a0, 1

_exit:
	lla	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */

	/* Semihosting call sequence */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	16
