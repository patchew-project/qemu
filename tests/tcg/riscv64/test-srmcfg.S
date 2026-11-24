/*
 * RISC-V Ssqosid srmcfg CSR test: check the WARL fields of srmcfg, that
 * mstateen0.SRMCFG is writable, and that an S-mode access to srmcfg
 * raises an illegal instruction exception while mstateen0.SRMCFG is
 * clear and succeeds once it is set. A VS-mode access raises a virtual
 * instruction exception while mstateen0.SRMCFG is set and an illegal
 * instruction exception while it is clear.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.equ CSR_SRMCFG,		0x181
.equ CSR_MSTATEEN0,		0x30c
.equ SRMCFG_RCID_MCID,		0x0FFF0FFF
.equ MSTATEEN0_SRMCFG,		(1 << 55)
.equ MSTATUS_MPP,		0x1800
.equ MSTATUS_MPP_S,		0x0800
.equ MSTATUS_MPV,		(1 << 39)
.equ CAUSE_ILLEGAL_INST,	2
.equ CAUSE_ECALL_S,		9
.equ CAUSE_ECALL_VS,		10
.equ CAUSE_VIRT_INST,		22
.equ PMPCFG_NAPOT_RWX,		0x1f
.equ SRMCFG_TEST_VALUE,		0x00550033	/* MCID 0x55, RCID 0x33 */

	.option norvc
	.text

	.global _start

_start:
	la	t0, trap_handler
	csrw	mtvec, t0

	/* Let S-mode access all of memory. */
	li	t0, -1
	csrw	pmpaddr0, t0
	li	t0, PMPCFG_NAPOT_RWX
	csrw	pmpcfg0, t0

	/* Only the RCID and MCID fields of srmcfg are writable. */
	li	t0, -1
	csrw	CSR_SRMCFG, t0
	csrr	t1, CSR_SRMCFG
	li	t2, SRMCFG_RCID_MCID
	bne	t1, t2, _fail

	/* mstateen0.SRMCFG must not be read-only zero. */
	li	t0, MSTATEEN0_SRMCFG
	csrw	CSR_MSTATEEN0, t0
	csrr	t1, CSR_MSTATEEN0
	and	t1, t1, t0
	beqz	t1, _fail

	/*
	 * With mstateen0.SRMCFG clear an S-mode access to srmcfg must raise
	 * an illegal instruction exception.
	 */
	csrw	CSR_MSTATEEN0, zero
	la	t0, smode_access_blocked
	jal	enter_smode
	la	t0, trap_mcause
	ld	t1, 0(t0)
	li	t2, CAUSE_ILLEGAL_INST
	bne	t1, t2, _fail

	/*
	 * With mstateen0.SRMCFG set an S-mode write to srmcfg must not trap
	 * and must read back.
	 */
	li	t0, MSTATEEN0_SRMCFG
	csrw	CSR_MSTATEEN0, t0
	la	t0, trap_mcause
	sd	zero, 0(t0)
	la	t0, smode_access_allowed
	jal	enter_smode
	la	t0, trap_mcause
	ld	t1, 0(t0)
	bnez	t1, _fail
	la	t0, smode_value
	ld	t1, 0(t0)
	li	t2, SRMCFG_TEST_VALUE
	bne	t1, t2, _fail

	/*
	 * With mstateen0.SRMCFG set a VS-mode access to srmcfg must raise a
	 * virtual instruction exception.
	 */
	la	t0, smode_access_blocked
	jal	enter_vsmode
	la	t0, trap_mcause
	ld	t1, 0(t0)
	li	t2, CAUSE_VIRT_INST
	bne	t1, t2, _fail

	/*
	 * With mstateen0.SRMCFG clear the same VS-mode access must raise an
	 * illegal instruction exception.
	 */
	csrw	CSR_MSTATEEN0, zero
	la	t0, smode_access_blocked
	jal	enter_vsmode
	la	t0, trap_mcause
	ld	t1, 0(t0)
	li	t2, CAUSE_ILLEGAL_INST
	bne	t1, t2, _fail

	li	a0, 0
	j	_exit

_fail:
	li	a0, 1

/* Exit through the semihosting SYS_EXIT_EXTENDED call with a0 as the code. */
_exit:
	la	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

/*
 * Run the code at t0 in S-mode. That code ends with an ecall, which the
 * trap handler turns into a return to M-mode at the instruction after
 * the ecall, so the S-mode code returns to our caller with ra intact.
 */
enter_smode:
	li	t1, MSTATUS_MPP
	csrc	mstatus, t1
	li	t1, MSTATUS_MPP_S
	csrs	mstatus, t1
	csrw	mepc, t0
	mret

/* Same as enter_smode, with mstatus.MPV set so that mret enters VS-mode. */
enter_vsmode:
	li	t1, MSTATUS_MPP
	csrc	mstatus, t1
	li	t1, MSTATUS_MPP_S | MSTATUS_MPV
	csrs	mstatus, t1
	csrw	mepc, t0
	mret

/* The csrr is expected to trap. The handler skips it and the nop. */
smode_access_blocked:
	csrr	t1, CSR_SRMCFG
	nop
	ecall
	ret

smode_access_allowed:
	li	t1, SRMCFG_TEST_VALUE
	csrw	CSR_SRMCFG, t1
	csrr	t1, CSR_SRMCFG
	la	t2, smode_value
	sd	t1, 0(t2)
	ecall
	ret

	.balign	4
trap_handler:
	csrr	t4, mcause
	li	t5, CAUSE_ECALL_S
	beq	t4, t5, 1f
	li	t5, CAUSE_ECALL_VS
	beq	t4, t5, 1f
	li	t5, CAUSE_ILLEGAL_INST
	beq	t4, t5, 3f
	li	t5, CAUSE_VIRT_INST
	bne	t4, t5, 2f

3:
	/* Record the cause and skip the trapping insn and the one after it. */
	la	t5, trap_mcause
	sd	t4, 0(t5)
	csrr	t4, mepc
	addi	t4, t4, 8
	csrw	mepc, t4
	mret

2:
	/* Any other trap is a failure: exit from M-mode. */
	li	t5, MSTATUS_MPP
	csrs	mstatus, t5
	la	t4, _fail
	csrw	mepc, t4
	mret

1:
	/* ecall from S or VS-mode: return to M-mode after the ecall. */
	li	t5, MSTATUS_MPP
	csrs	mstatus, t5
	csrr	t4, mepc
	addi	t4, t4, 4
	csrw	mepc, t4
	mret

	.data
	.balign	8
semiargs:
	.space	16
trap_mcause:
	.space	8
smode_value:
	.space	8
