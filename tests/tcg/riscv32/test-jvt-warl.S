/*
 * Verify jvt.mode WARL normalization and cm.jt dispatch.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option norvc
	.text
	.global _start
_start:
	lla	t0, trap
	csrw	mtvec, t0
	lla	t0, jvt_table
	ori	t1, t0, 0x3f
	csrw	0x017, t1
	csrr	t2, 0x017
	bne	t0, t2, fail
	.2byte	0xa002		/* cm.jt index 0 */

fail:
	li	a0, 1
	j	_exit

target:
	li	a0, 0

_exit:
	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sw	t0, 0(a1)
	sw	a0, 4(a1)
	li	a0, 0x20		/* TARGET_SYS_EXIT_EXTENDED */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

trap:
	li	a0, 1
	j	_exit

	.balign	64
jvt_table:
	.word	target

	.data
	.balign	8
semiargs:
	.space	8
