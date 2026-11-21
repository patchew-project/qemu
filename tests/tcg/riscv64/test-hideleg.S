/*
 * Test if bits 63:16 from hideleg becomes writable when
 * they shouldn't.  Derived from Gitlab 4814.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define CSR_MVIEN 0x308
#define CSR_HIDELEG 0x603

.option norvc
	.text
	.global _start

_start:
	lla	t0, trap
	csrw	mtvec, t0

	/* Mask for 63:16.  */
	li	t0, -1
	slli	s0, t0, 16

	/* mideleg = 0, mvien = 0: hideleg bits 63:16 should be zero.  */
	li	t0, 0
	csrw	mideleg, t0
	csrw	CSR_MVIEN, t0
	li	t1, -1
	csrw	CSR_HIDELEG, t1

	csrr	t1, CSR_HIDELEG
	and	t1, t1, s0
	bnez	t1, _fail

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
