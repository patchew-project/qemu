/*
 * Test for writable hedeleg bits.  Based on Gitlab 4592 testcase,
 * which is derived from riscv-arch-test.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define HEDELEG_W_MASK 0xcb1fe

.option norvc
	.text
	.global _start

_start:
	lla	t0, trap
	csrw	mtvec, t0

	/*
	 * Write a full 1 mask on hedeleg, read it back, see if
	 * all the expected bits were set.
	 */
	addi	t0, zero, -1
	csrw	hedeleg, t0
	csrr	t0, hedeleg
	li	t1, HEDELEG_W_MASK
	bne	t0, t1, _fail
	li	a0, 0
	j	_exit

_fail:
	li	a0, 1
	j	_exit

trap:
	# Any trap is a fail code 1
	li	a0, 1

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
	.balign	16
semiargs:
	.space	16
