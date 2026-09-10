/* SPDX-License-Identifier: GPL-2.0-or-later */

/* CSR numbers for older assemblers. */
#define CSR_MINSTRETCFG      0x322
#define CSR_MINSTRETCFGH     0x722

/* RV32 writes to minstretcfg must preserve minstretcfgh. */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/*
	 * Use complementary patterns to check both set and clear xINH bits.
	 * All fields in the low half are WPRI, so write zero there.
	 * Exit status 1 or 2 identifies the pattern that was not preserved.
	 */
	li	t4, 1
	li	t0, 0x54000000		/* MINH, UINH, VUINH */
	csrw	CSR_MINSTRETCFGH, t0
	csrw	CSR_MINSTRETCFG, zero
	csrr	t1, CSR_MINSTRETCFGH
	bne	t0, t1, exit

	li	t4, 2
	li	t0, 0x28000000		/* SINH, VSINH */
	csrw	CSR_MINSTRETCFGH, t0
	csrw	CSR_MINSTRETCFG, zero
	csrr	t1, CSR_MINSTRETCFGH
	bne	t0, t1, exit
	li	t4, 0

exit:
	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sw	t0, 0(a1)
	sw	t4, 4(a1)
	li	a0, 0x20		/* TARGET_SYS_EXIT_EXTENDED */

	/* Semihosting call sequence. */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	8
