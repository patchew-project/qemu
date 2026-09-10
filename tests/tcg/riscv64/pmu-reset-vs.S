/* SPDX-License-Identifier: GPL-2.0-or-later */

/* CSR number for older assemblers. */
#define CSR_MCYCLECFG        0x321

	.option	norvc
	.option	norelax

	.equ	MSTATUS_MPP_S,  (1 << 11)
	.equ	MSTATUS_MPP,    (3 << 11)
	.equ	MSTATUS_SPP,    (1 << 8)
	.equ	HSTATUS_SPV,    (1 << 7)
	.equ	SIFIVE_TEST,    0x100000
	.equ	MTIMECMP,       0x2004000
	.equ	FINISHER_RESET, 0x7777

	.text
	.global _start
_start:
	/* Preserve the reset marker and saved count in MTIMECMP. */
	li	t0, MTIMECMP
	ld	t1, 0(t0)
	srli	t2, t1, 48
	li	t3, 0xa5a5
	beq	t2, t3, after_reset

	/* Count VS only, then remember the value before entering VS-mode. */
	li	t0, 0x1d		/* MINH | SINH | UINH | VUINH */
	slli	t0, t0, 58
	csrw	CSR_MCYCLECFG, t0
	csrr	t1, mcycle
	slli	t1, t1, 16
	srli	t1, t1, 16
	li	t2, 0xa5a5
	slli	t2, t2, 48
	or	t1, t1, t2
	li	t0, MTIMECMP
	sd	t1, 0(t0)
	li	t0, -1
	csrw	pmpaddr0, t0
	li	t0, 0x1f		/* RWX, NAPOT */
	csrw	pmpcfg0, t0

	/* Enter HS-mode first. */
	csrr	t0, mstatus
	li	t1, MSTATUS_MPP
	not	t1, t1
	and	t0, t0, t1
	li	t1, MSTATUS_MPP_S
	or	t0, t0, t1
	csrw	mstatus, t0
	lla	t0, hs_enter
	csrw	mepc, t0
	mret

hs_enter:
	li	t0, HSTATUS_SPV
	csrs	hstatus, t0
	li	t0, MSTATUS_SPP
	csrs	sstatus, t0
	lla	t0, vs_reset
	csrw	sepc, t0
	sret

vs_reset:
	/* These VS-mode cycles must remain in mcycle after reset. */
	li	t0, 128
1:
	addi	t0, t0, -1
	bnez	t0, 1b

	li	t0, SIFIVE_TEST
	li	t1, FINISHER_RESET
	sw	t1, 0(t0)
	j	.

after_reset:
	csrr	t0, mcycle
	slli	t1, t1, 16		/* Clear the MTIMECMP marker. */
	srli	t1, t1, 16
	addi	t1, t1, 128
	bgeu	t1, t0, fail
	li	t3, 0
	j	exit

fail:
	li	t3, 1

exit:
	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	t3, 8(a1)
	li	a0, 0x20		/* TARGET_SYS_EXIT_EXTENDED */

	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	16
