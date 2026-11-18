/*
 * Test if mstatus.MPP and mnstatus.MNPP will write reserved values.
 * See Gitlab 4826 for more info.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define CSR_MNSTATUS 0x744

.option norvc
	.text
	.global _start

_start:
	lla	t0, trap
	csrw	mtvec, t0

	/* Set mstatus.MPP [12:11] and mnstatus.MNPP to 11b  */
	li	t0, 0b11
	slli	t0, t0, 11
	csrs	mstatus, t0
	csrs	CSR_MNSTATUS, t0

	/* Set mstatus.MPP to reserved value 10b by clearing bit 11. */
	li	t1, 1
	slli	t1, t1, 11
	csrs	mstatus, t1

	/* mstatus.MPP must be equal to 11b (ignoring the last write).  */
	csrr	s0, mstatus
	srli	s0, s0, 11
	li	t2, 3
	and	s0, s0, t2
	bne	s0, t2, _fail

	/* Set mnstatus.MNPP to reserved value 10b by clearing bit 11. */
	csrs	CSR_MNSTATUS, t1

	/* mnstatus.MNPP must be equal to 11b (ignoring the last write).  */
	csrr	s0, CSR_MNSTATUS
	srli	s0, s0, 11
	and	s0, s0, t2
	bne	s0, t2, _fail

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
