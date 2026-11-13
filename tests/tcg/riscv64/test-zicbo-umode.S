/*
 * Test if cbo instructions are executable without S mode
 * available.  Code derived from the Gitlab 4596 testcase.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define CSR_MENVCFG 0x30a
#define MENVCFG_CB  ((1 << 7) | (1 << 6) | (1 << 4))  // CBZE | CBCFE | CBIE=01

.option norvc
	.text
	.global _start

_start:
	lla	t0, trap
	csrw	mtvec, t0

	/*
	 * PMP entry 0: NAPOT over all of memory, RWX, so U-mode
	 * can fetch and access data.
	 */
	li	t0, -1
	csrw	pmpaddr0, t0
	li	t0, 0x1f
	csrw	pmpcfg0, t0

	/*
	 * Enable the CBO instructions for U-mode and report what
	 * menvcfg reads back.
	 */
	li	t0, MENVCFG_CB
	csrs	CSR_MENVCFG, t0
	csrr	s1, CSR_MENVCFG

	// mret to U-mode at user_code (MPP = 0)
	li	t0, 3 << 11
	csrc	mstatus, t0
	la	t0, user_code
	csrw	mepc, t0
	la	s2, cbo_step
	mret

user_code:
	la 	a0, buffer       // address of buffer
	li	t1, 1
	sw	t1, 0(s2)
	cbo.zero	(a0)
	li	t1, 2
	sw	t1, 0(s2)
	cbo.clean	(a0)
	li	t1, 3
	sw	t1, 0(s2)
	cbo.flush	(a0)
	li	t1, 4
	sw	t1, 0(s2)
	cbo.inval	(a0)
	ecall

trap:
	csrr s3, mcause
	li t0, 8                   // environment call from U-mode
	bne s3, t0, _fail
	li	a0, 0
	j	_exit

_fail:
	li	a0, 1
	j	_exit

# Exit code in a0
_exit:
	lla	a1, semiargs
	li	t0, 0x20026	# ADP_Stopped_ApplicationExit
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	# TARGET_SYS_EXIT_EXTENDED

	# Semihosting call sequence
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data

	.balign	64
cbo_step:
	.word 0
buffer:
	.fill 64, 1, 0xa5

	.balign	16
semiargs:
	.space	16

