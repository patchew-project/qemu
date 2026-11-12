/*
 * Verify that mstatush is not being cleared after writing mstatus
 * (Gitlab 4488).
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define MSTATUSH_MDT 0x400

.option norvc
	.text
	.global _start

_start:
	lla	t0, trap
	csrw	mtvec, t0

	/* Write mstatush.MDT.  */
	csrr	s0, mstatush
	li	t0, MSTATUSH_MDT
	or	t1, s0, t0
	csrw	mstatush, t1

	/* Check if mstatush.MDT is indeed written.  */
	csrr	s0, mstatush
	and	t1, s0, t0
	bne	t0, t1, _fail

	/* Read mstatus, write the same stuff back to it.  */
	csrr	t1, mstatus
	csrw	mstatus, t1

	/*
	 * Make sure that we didn't clear mstatush.MDT after
	 * writing mstatus.
	 */
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
