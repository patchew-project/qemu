/* SPDX-License-Identifier: GPL-2.0-or-later */

/* RV32 delegated counter accesses must also work in the RV64 emulator. */

	.option	norvc
	.option	norelax

	.text
	.global _start
_start:
	/* Unexpected exceptions report the current check number. */
	li	t4, 1
	lla	t0, fail
	csrw	mtvec, t0
	li	t0, 8			/* Stop HPM3 so reads are exact. */
	csrw	mcountinhibit, t0
	csrw	mcounteren, t0
	li	t0, 1 << 28		/* menvcfgh.CDE */
	csrw	0x31a, t0
	li	t0, 0x43		/* siselect: counter 3 */
	csrw	0x150, t0
	li	t0, 0x12345678
	csrw	0xb83, t0		/* mhpmcounter3h */
	li	t0, 1			/* HW_CPU_CYCLES */
	csrw	0x323, t0
	li	t0, 0xc0000000		/* OF | MINH */
	csrw	0x723, t0

	/* sireg writes the low half without changing the high half. */
	li	t0, 0x2468ace0
	csrw	0x151, t0
	csrr	t1, 0x151
	bne	t0, t1, fail
	csrr	t1, 0xb03
	bne	t0, t1, fail
	li	t0, 0x12345678
	csrr	t1, 0xb83
	bne	t0, t1, fail

	/* sireg4 writes the high half without changing the low half. */
	li	t4, 2
	li	t0, 0xfedcba98
	csrw	0x155, t0
	csrr	t1, 0x155
	bne	t0, t1, fail
	csrr	t1, 0xb83
	bne	t0, t1, fail
	li	t0, 0x2468ace0
	csrr	t1, 0xb03
	bne	t0, t1, fail

	/* sireg5 clears OF and sets SINH, but cannot change machine MINH. */
	li	t4, 3
	li	t0, 0x20000000		/* SINH */
	csrw	0x156, t0
	csrr	t1, 0x156
	bne	t0, t1, fail
	li	t0, 0x60000000		/* MINH | SINH */
	csrr	t1, 0x723
	bne	t0, t1, fail
	li	t0, 1
	csrr	t1, 0x323
	bne	t0, t1, fail

	/* sireg2 changes the event without changing those high-half bits. */
	li	t4, 4
	li	t0, 2			/* HW_INSTRUCTIONS */
	csrw	0x152, t0
	csrr	t1, 0x323
	bne	t0, t1, fail
	li	t0, 0x60000000
	csrr	t1, 0x723
	bne	t0, t1, fail

	li	t0, 0x5555		/* FINISHER_PASS */
	j	finish
fail:
	slli	t0, t4, 16
	li	t1, 0x3333		/* FINISHER_FAIL with check number */
	or	t0, t0, t1
finish:
	li	t1, 0x100000		/* virt test device */
	sw	t0, 0(t1)
	j	.
