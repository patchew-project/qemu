/*
 * Test for writable hgatp bits.  See Gitlab 4591 for reference.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.option norvc
	.text
	.global _start

_start:
	lla	t0, trap
	csrw	mtvec, t0

	/*
	 * Write in HGATP the same value written in Gitlab 4591
	 * testcase (0x8c00000000000003).
	 */
	li	t0, 0x8c
	slli	t0, t0, 56
	addi	t0, t0, 3
	csrw	hgatp, t0

	/* hgatp expected is 0x8000000000000000. */
	li	t0, 0x8
	slli	t0, t0, 60
	csrr	t1, hgatp

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

