/*
 * Test that mstatush.MPELP is writable (Gitlab 4220).
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define MSTATUSH_MPELP 0x200

.option norvc
	.text
	.global _start

_start:
	lla	t0, trap
	csrw	mtvec, t0

	csrr	s0, mstatush
	li	t0, MSTATUSH_MPELP
	or	t1, s0, t0
	csrw	mstatush, t1

	csrr	s0, mstatush
	and	t1, s0, t0
	bne	t0, t1, _fail

	j	_pass

trap:
	j	_fail

_pass:
	li	t0, 0x100000
	li	t1, 0x5555	/* FINISHER_PASS */
	sw	t1, 0(t0)
	j	.

_fail:
	li	t0, 0x100000
	li	t1, 0x13333	/* status = FINISHER_FAIL, code = 1 */
	sw	t1, 0(t0)
	j	.
