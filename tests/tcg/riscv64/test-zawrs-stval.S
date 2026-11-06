/*
 * Test that WRS.NTO writes the instruction encoding to stval when
 * mstatus.TW causes an illegal-instruction exception in S-mode.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norvc

	.text
	.global	_start
_start:
	/* Unexpected traps to M-mode fail the test. */
	lla	t0, machine_trap
	csrw	mtvec, t0

	/* Delegate illegal-instruction exceptions to S-mode. */
	li	t0, 1 << 2
	csrs	medeleg, t0

	lla	t0, supervisor_trap
	csrw	stvec, t0

	/* Give S-mode read, write and execute access to all memory. */
	li	t0, -1
	csrw	pmpaddr0, t0
	li	t0, 0xf
	csrw	pmpcfg0, t0

	/* Set mstatus.TW and select S-mode for mret. */
	li	t0, 3 << 11
	csrc	mstatus, t0
	li	t0, (1 << 21) | (1 << 11)
	csrs	mstatus, t0

	lla	t0, supervisor_start
	csrw	mepc, t0
	mret

supervisor_start:
	/* WRS.NTO must trap because mstatus.TW is set. */
	li	a0, 1
wrs_nto:
	.word	0x00d00073
	j	_exit

machine_trap:
	li	a0, 2
	j	_exit

supervisor_trap:
	li	a0, 3

	csrr	t0, scause
	li	t1, 2
	bne	t0, t1, _exit

	csrr	t0, sepc
	lla	t1, wrs_nto
	bne	t0, t1, _exit

	csrr	t1, stval
	lwu	t2, 0(t0)
	bne	t1, t2, _exit
	li	t2, 0x00d00073
	bne	t1, t2, _exit

	li	a0, 0

_exit:
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
