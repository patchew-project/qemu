/*
 * Test reserved pmpcfg bits R and W.  Code derived from
 * the Gitlab 4820 testcase.
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
	 * Write 0x2 (R=0, W=1) in pmp7cfg (last byte of pmpcfg0,
	 * i.e. a 56 shift).  Store the result in s0.
	 */
	li	t0, 0xff
	slli	t0, t0, 56
	csrc	pmpcfg0, t0
	li	t0, 0x02
	slli	t0, t0, 56
	csrs	pmpcfg0, t0	// pmp7cfg = 0x02
	csrr	s0, pmpcfg0

	/*
	 * Likewise, now writing 0x6 (R=0, W=1, X=1) in pmp7cfg and
	 * storing the result in s1.
	 */
	li	t0, 0xff
	slli	t0, t0, 56
	csrc	pmpcfg0, t0
	li	t0, 0x06
	slli	t0, t0, 56
	csrs	pmpcfg0, t0	// pmp7cfg = 0x06
	csrr	s1, pmpcfg0

	li	t0, 0xff
	slli	t0, t0, 56
	csrc	pmpcfg0, t0

	/*
	 * rshift s0 to get to pmp7cfg and compare the first 2
	 * bits (R and W) with 0x2.  They must be clear.
	 */
	srli t1, s0, 56
	andi t1, t1, 3
	li   t2, 2
	beq  t1, t2, _fail

	/* Do the same with s1 - R and W should be clear.  */
	srli t1, s1, 56
	andi t1, t1, 3
	beq  t1, t2, _fail

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

	.balign	64
cbo_step:
	.word 0
buffer:
	.fill 64, 1, 0xa5

	.balign	16
semiargs:
	.space	16

