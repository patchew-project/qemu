/*
 * Test if amocas.d will clobber x0.  Based on Gitlab 3526.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option norvc
	.text

	.global _start

_start:
	li     t0, 5
	lla    t1, amo_target
	li     t2, 0

	/* amocas.d zero, t0, (t1) */
	.word  0x2853302f

	/* If x0 is clobbered this bnez will fail.  */
	bnez   t2, _fail

	li     a0, 0
	j      _exit

_fail:
	li    a0, 1

/* Exit through the semihosting SYS_EXIT_EXTENDED call with a0 as the code. */
_exit:
	lla	a1, semiargs
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
/* An amo target must be a valid, 8-byte aligned address.  */
amo_target:
	.dword	0x1234
