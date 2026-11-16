/*
 * Test if mseccfg.PMM is cleared correctly.  From Gitlab 4818:
 *
 * "Smmpm: mseccfg.PMM (bits 33:32) is a WARL field whose legal
 *  values are 00, 10 and 11.  Write 11, then clear it with csrc:
 *  it must read 00 (00 is the reset value, so it is supported).
 *  Pass (exit 0): PMM reads 00 after the clear."
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

	/* Write (11) to PMM (bits 33:32).  */
	li	t0, 3
	slli	t0, t0, 32
	csrs	CSR_MSECCFG, t0

	csrr	s0, CSR_MSECCFG

	/* Clear PMM.  */
	csrc	CSR_MSECCFG, t0

	/* Read mseccfg again.  PMM must read 00.  */
	csrr	s1, CSR_MSECCFG
	srli	t1, s1, 32
	andi	t1, t1, 3
	snez	a0, t1

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
