/*
 * Verify mseccfg bits USEED, SSEED, MLPE and WPRI.
 *
 * with "rv32,zkr=false,zicfilp=true" only MLPE should be set.
 *
 * Based on Gitlab 4680 testcases.
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

	li	t0, -8		// all ones except bits 2:0
	csrs	CSR_MSECCFG, t0	// mseccfg
	csrr	t1, CSR_MSECCFG

	/* WPRI fields are always read-only zero.  */
	li	t2, 0xfffff8f8	// WPRI bits 31:11 and 7:3 (RV32 low half)
	and	t3, t1, t2
	bnez	t3, _fail

	/*
	 * USEED: read-only zero if not zkr or no U-mode.
	 * SSEED: read-only zero if not zkr or no S-mode.
	 * MLPE: must be enabled since we have Zifilp present.
	 */
	srli	t3, t1, 8
	andi	t3, t3, 7	// USEED, SSEED, MLPE -> bits 1, 2, 3
	li	t2, 4
	bne	t3, t2, _fail

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
