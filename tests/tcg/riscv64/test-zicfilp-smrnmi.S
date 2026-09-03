/*
 * Test that an M-mode exception taken with mnstatus.NMIE clear saves ELP in
 * mstatus.MPELP, so that it is restored by MRET.  MNPELP is reserved for the
 * actual RNMI path, which returns with MNRET.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norvc

	.equ	CSR_MNSTATUS, 0x744
	.equ	CSR_MSECCFG, 0x747
	.equ	MNSTATUS_NMIE, 1 << 3
	.equ	MNSTATUS_MNPELP, 1 << 9
	.equ	MSECCFG_MLPE, 1 << 10
	.equ	MSTATUS_MPELP, 1 << 41
	.equ	EXCP_INST_ACCESS_FAULT, 1
	.equ	EXCP_SW_CHECK, 18
	.equ	SW_CHECK_FCFI_TVAL, 2

	.text
	.global	_start
_start:
	/* Verify that NMIE is clear before exercising the exception path. */
	csrr	t0, CSR_MNSTATUS
	andi	t0, t0, MNSTATUS_NMIE
	bnez	t0, fail_nmie

	/* Enable landing-pad checks in M-mode, then start a new TB. */
	li	t0, MSECCFG_MLPE
	csrs	CSR_MSECCFG, t0
	j	tracked_jump

tracked_jump:
	/* Make a Zicfilp-tracked indirect jump to an address that faults. */
	li	t1, 0
	jalr	zero, 0(t1)

	/*
	 * The instruction access fault should have redirected to the handler.
	 */
	li	a0, 1
	j	_exit

fail_nmie:
	li	a0, 6
	j	_exit

	/* Must match rnmi-exception-vector in Makefile.softmmu-target. */
	.org	0x100
rnmi_exception:
	csrr	t0, mcause
	li	t1, EXCP_INST_ACCESS_FAULT
	beq	t0, t1, handle_fetch_fault
	li	t1, EXCP_SW_CHECK
	beq	t0, t1, handle_sw_check

	li	a0, 4
	j	_exit

handle_fetch_fault:
	/* This is an M-mode exception and will return with MRET. */
	csrr	t0, mstatus
	li	t1, MSTATUS_MPELP
	and	t0, t0, t1
	beqz	t0, fail_mpelp

	csrr	t0, CSR_MNSTATUS
	andi	t0, t0, MNSTATUS_MNPELP
	bnez	t0, fail_mnpelp

	lla	t0, resume_non_lpad
	csrw	mepc, t0
	mret

resume_non_lpad:
	/* MRET must restore ELP, so this instruction must not retire. */
	li	a0, 1
	j	_exit

handle_sw_check:
	csrr	t0, mtval
	li	t1, SW_CHECK_FCFI_TVAL
	bne	t0, t1, fail_tval

	li	a0, 0
	j	_exit

fail_mpelp:
	li	a0, 2
	j	_exit

fail_mnpelp:
	li	a0, 3
	j	_exit

fail_tval:
	li	a0, 5

_exit:
	lla	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */

	/* Semihosting call sequence. */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	16
