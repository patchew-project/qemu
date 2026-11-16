/*
 * Verify mseccfg bits USEED, SSEED, MLPE and WPRI.  Also verifies
 * if mseccfgh exists and it's read-only zero.
 *
 * With "rv32,u=off,h=off,s=off,zkr=on,zicfilp=off" we expect no
 * bits being written in mseccfg.
 *
 * Based on Gitlab 4680 testcase.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define CSR_MSECCFG 0x747
#define CSR_MSECCFGH 0x757

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
	 * MLPE: read-only zero if no Zifilp present.
	 */
	srli	t3, t1, 8
	andi	t3, t3, 7	// USEED, SSEED, MLPE -> bits 1, 2, 3
	bnez	t3, _fail

	/*
	 * mseccfgh reads the higher half of mseccfg, which contains
	 * only mseccfg.PMM.  This bit is read-only zero in RV32,
	 * thus mseccfgh must be always zeroed.  Write a full 1s
	 * mask in it and verify.
	 */
	li	t0, -1
	csrw	CSR_MSECCFGH, t0
	csrr	t0, CSR_MSECCFGH
	bnez	t0, _fail

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
