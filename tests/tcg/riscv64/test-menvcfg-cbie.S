/*
 * Test if menvcfg.CBIE/henvcfg.CBIE reserved value 10b isn't written.
 * See Gitlab 4216 for more info.
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

	/* Set menvcfg.CBIE [5:4] to 0b00.  */
	li	t0, 3
	slli	t0, t0, 4
	csrc	menvcfg, t0

	/* Now set it to 0b10.  */
	li	t1, 1
	slli	t1, t1, 5
	csrs	menvcfg, t1

	/* Read it back, rshift it, check if it's 00.  */
	csrr	s0, menvcfg
	srli	s0, s0, 4
	andi	s0, s0, 3
	bnez	s0, _fail

	/*
	 * The same constraints apply to henvcfg.CBIE, thus we're
	 * testing it like above: set it to 00b, then 10b, then
	 * see if it's zero.
	 */
	li	t0, 3
	slli	t0, t0, 4
	csrc	henvcfg, t0

	/* Now set it to 0b10.  */
	li	t1, 1
	slli	t1, t1, 5
	csrs	henvcfg, t1

	/* Read it back, rshift it, check if it's 00.  */
	csrr	s0, henvcfg
	srli	s0, s0, 4
	andi	s0, s0, 3
	bnez	s0, _fail

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
