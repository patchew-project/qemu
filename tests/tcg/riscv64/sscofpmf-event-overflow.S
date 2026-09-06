/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Non-fixed event overflow with and without Sscofpmf. */

#ifndef EXPECT_SSCOFPMF
#define EXPECT_SSCOFPMF 1
#endif

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/* Unexpected exceptions report the current check number. */
	li	t4, 1
	lla	t0, exit
	csrw	mtvec, t0
	csrw	mie, zero
	csrw	mip, zero
	csrw	mcountinhibit, zero
	csrw	0x323, zero		/* mhpmevent3 */
	li	t0, -1
	csrw	0xb03, t0		/* mhpmcounter3 */
	li	t0, 0x10019		/* DTLB read miss */
	csrw	0x323, t0

	/* One load after flushing the TLB must wrap the counter to zero. */
	sfence.vma
	lla	t0, first_page
	lw	t1, 0(t0)
	csrr	t0, 0xb03
	bnez	t0, exit

	/* Only Sscofpmf turns that wrap into OF and LCOFIP. */
	li	t4, 2
	csrr	t0, 0x323
	srli	t0, t0, 63
	li	t1, EXPECT_SSCOFPMF
	bne	t0, t1, exit
	li	t4, 3
	csrr	t0, mip
	srli	t0, t0, 13
	andi	t0, t0, 1
	bne	t0, t1, exit

	/* Clearing LCOFIP alone must not re-enable overflow notification. */
	li	t0, 1 << 13
	csrc	mip, t0
	li	t0, -1
	csrw	0xb03, t0
	sfence.vma
	lla	t0, second_page
	lw	t1, 0(t0)
	li	t4, 4
	csrr	t0, 0xb03
	bnez	t0, exit
	li	t4, 5
	csrr	t0, 0x323
	srli	t0, t0, 63
	li	t1, EXPECT_SSCOFPMF
	bne	t0, t1, exit
	li	t4, 6
	csrr	t0, mip
	li	t1, 1 << 13
	and	t0, t0, t1
	bnez	t0, exit

	/* Overflow notification must not stop event counting. */
	sfence.vma
	lla	t0, third_page
	lw	t1, 0(t0)
	li	t4, 7
	csrr	t0, 0xb03
	li	t1, 1
	bne	t0, t1, exit
	li	t4, 0

exit:
	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	t4, 8(a1)
	li	a0, 0x20		/* TARGET_SYS_EXIT_EXTENDED */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	4096
first_page:
	.word	0
	.balign	4096
second_page:
	.word	0
	.balign	4096
third_page:
	.word	0
	.balign	16
semiargs:
	.space	16
