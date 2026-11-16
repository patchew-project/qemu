/*
 * Test if writing a reserved mseccfg.PMM value reads back a
 * legal value. From Gitlab 4818:
 *
 * "Smmpm: write the reserved value 01 to mseccfg.PMM. It must
 *  read back a legal value (00, 10 or 11),  and an M-mode load
 *  must then work. Pass (exit 0)."
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define CSR_MSECCFG 0x747

.option norvc
	.text
	.global _start

_start:
	lla	t0, trap
	csrw	mtvec, t0

	li	t0, 1
	slli	t0, t0, 32
	csrs	CSR_MSECCFG, t0
	csrr	s0, CSR_MSECCFG

	srli	s1, s0, 32
	andi	s1, s1, 3

	li	t0, 1
	beq	s1, t0, _fail	// 01 stored: fail

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
