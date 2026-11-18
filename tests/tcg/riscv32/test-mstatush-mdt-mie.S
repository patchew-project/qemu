/*
 * Verify that when mstatush.MDT = 1 MIE is read only zero (Gitlab 4827).
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define MSTATUS_MIE 0x8
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

	/* Read mstatus, set MIE, write it back.  */
	csrr	t1, mstatus
	ori     t1, t1, MSTATUS_MIE
	csrw	mstatus, t1

	/* Make sure that MIE is not set.  */
	csrr	t1, mstatus
	andi	t1, t1, MSTATUS_MIE
	bnez	t1, _fail

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
