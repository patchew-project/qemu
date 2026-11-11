/*
 * Test if a HVIP read is returning 0 for all bits it can't
 * write.  Based on Gitlab 3313 testcase.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
	.option norvc
	.text

	.global _start

_start:
	li    t0, 2
	csrs  mip, t0
	csrr  t1, hvip
	bnez  t1, _fail

	li    a0, 0
	j     _exit

_fail:
	li    a0, 1

/* Exit through the semihosting SYS_EXIT_EXTENDED call with a0 as the code. */
_exit:
	la	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	8
semiargs:
	.space	16
