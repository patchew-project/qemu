/* SPDX-License-Identifier: GPL-2.0-or-later */

/* RV32 delegated PMU registers, in both RV32 and RV64 emulators. */

/* CSR numbers for older assemblers. */
#define CSR_SISELECT         0x150
#define CSR_SIREG            0x151
#define CSR_SIREG2           0x152
#define CSR_SIREG4           0x155
#define CSR_SIREG5           0x156
#define CSR_MENVCFGH         0x31a
#define CSR_MHPMEVENT3H      0x723

	.option	norvc
	.option	norelax

	.text
	.global	_start
_start:
	/* Unexpected exceptions report the current check number. */
	li	t4, 1
	lla	t0, fail
	csrw	mtvec, t0
	li	t0, 8			/* Stop HPM3 so reads are exact. */
	csrw	mcountinhibit, t0
	csrw	mcounteren, t0
	li	t0, 1 << 28		/* menvcfgh.CDE */
	csrw	CSR_MENVCFGH, t0
	li	t0, 0x43		/* siselect: counter 3 */
	csrw	CSR_SISELECT, t0
	li	t0, 0x12345678
	csrw	mhpmcounter3h, t0
	li	t0, 1			/* HW_CPU_CYCLES */
	csrw	mhpmevent3, t0
	li	t0, 0xc0000000		/* OF | MINH */
	csrw	CSR_MHPMEVENT3H, t0

	/* sireg writes the low half without changing the high half. */
	li	t0, 0x2468ace0
	csrw	CSR_SIREG, t0
	csrr	t1, CSR_SIREG
	bne	t0, t1, fail
	csrr	t1, mhpmcounter3
	bne	t0, t1, fail
	li	t0, 0x12345678
	csrr	t1, mhpmcounter3h
	bne	t0, t1, fail

	/* sireg4 writes the high half without changing the low half. */
	li	t4, 2
	li	t0, 0xfedcba98
	csrw	CSR_SIREG4, t0
	csrr	t1, CSR_SIREG4
	bne	t0, t1, fail
	csrr	t1, mhpmcounter3h
	bne	t0, t1, fail
	li	t0, 0x2468ace0
	csrr	t1, mhpmcounter3
	bne	t0, t1, fail

	/* sireg5 clears OF and sets SINH, but cannot change machine MINH. */
	li	t4, 3
	li	t0, 0x20000000		/* SINH */
	csrw	CSR_SIREG5, t0
	csrr	t1, CSR_SIREG5
	bne	t0, t1, fail
	li	t0, 0x60000000		/* MINH | SINH */
	csrr	t1, CSR_MHPMEVENT3H
	bne	t0, t1, fail
	li	t0, 1
	csrr	t1, mhpmevent3
	bne	t0, t1, fail

	/* sireg2 changes the event without changing those high-half bits. */
	li	t4, 4
	li	t0, 2			/* HW_INSTRUCTIONS */
	csrw	CSR_SIREG2, t0
	csrr	t1, mhpmevent3
	bne	t0, t1, fail
	li	t0, 0x60000000
	csrr	t1, CSR_MHPMEVENT3H
	bne	t0, t1, fail

	/* Event replacement must also preserve OF when it is set. */
	li	t4, 5
	li	t0, 0xe0000000		/* OF | MINH | SINH */
	csrw	CSR_MHPMEVENT3H, t0
	li	t0, 1			/* HW_CPU_CYCLES */
	csrw	mhpmevent3, t0
	li	t0, 2			/* HW_INSTRUCTIONS */
	csrw	CSR_SIREG2, t0
	csrr	t1, mhpmevent3
	bne	t0, t1, fail
	li	t0, 0xe0000000
	csrr	t1, CSR_MHPMEVENT3H
	bne	t0, t1, fail

	/* Selecting event zero must also leave the high half unchanged. */
	li	t4, 6
	csrw	CSR_SIREG2, zero
	csrr	t1, mhpmevent3
	bnez	t1, fail
	csrr	t1, CSR_MHPMEVENT3H
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
