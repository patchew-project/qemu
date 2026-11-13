/*
 * Test if hvip.VSTIP is writable with menvcfg.STCE and
 * henvcfg.STCE set.  See Gitlab 4593 for more info.
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

	/* Enable menvcfg.STCE and henvcfg.STCE (bit 63).  */
	li	t0, 1
	slli	t0, t0, 63
	csrs	menvcfg, t0
	csrs	henvcfg, t0

	/*
	 * write hvip.VSTIP, read hvip again and verify that
	 * VSTIP was indeed written.
	 */
	csrr	t0, hvip
	li	t1, 1
	slli	t1, t1, 6 # bit 6: VSTIP
	or	t0, t0, t1
	csrw	hvip, t0

	csrr	t1, hvip
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
