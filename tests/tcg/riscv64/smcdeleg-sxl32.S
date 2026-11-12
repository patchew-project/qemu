/* SPDX-License-Identifier: GPL-2.0-or-later */

/* CSR numbers for older assemblers. */
#define CSR_SISELECT         0x150
#define CSR_SIREG            0x151
#define CSR_SIREG2           0x152
#define CSR_SIREG4           0x155
#define CSR_SIREG5           0x156
#define CSR_MENVCFG          0x30a
#define CSR_MSTATEEN0        0x30c
#define CSR_MCYCLECFG        0x321
#define CSR_MINSTRETCFG      0x322

/*
 * Exercise delegated PMU registers with MXLEN=64 and SXLEN=32.
 * Build separately for counter low halves, counter high halves and configs
 * so a failure in one access path does not hide failures in the others.
 */

	.option	norvc
	.option	norelax

	/* RV64 'li' can emit ADDIW, which is illegal in the RV32 blocks. */
	.macro	load32 reg, value
	lui	\reg, %hi(\value)
	addi	\reg, \reg, %lo(\value)
	.endm

	/* Enter RV32 S-mode; the ECALL trap below returns to M-mode. */
	.macro	enter_s32 label
	li	a0, 0
	li	a5, 9			/* Expected exception: S-mode ECALL. */
	li	t0, 3 << 11
	csrc	mstatus, t0
	li	t0, 1 << 11
	csrs	mstatus, t0		/* MPP=S */
	lla	t0, \label
	csrw	mepc, t0
	mret
	.endm

	/* Each alias must be 64-bit in M-mode, even while SXLEN is 32. */
	.macro	check_m64_alias csr
	li	t0, 0x1234567811223344
	csrw	CSR_SIREG, t0
	csrr	t1, \csr
	bne	t0, t1, fail
	csrr	t1, CSR_SIREG
	bne	t0, t1, fail

	/* High-half aliases must still trap when the current XLEN is 64. */
	li	a0, 0
	li	a5, 2			/* Expect illegal instruction. */
	li	s9, 0
	csrr	t0, CSR_SIREG4
	csrr	t0, CSR_SIREG5
	li	t0, 2
	bne	s9, t0, fail		/* Both accesses must have trapped. */
	.endm

	.macro	check_counter index, csr
	li	s10, \index + 1		/* Counter-specific failure code. */
	li	t0, 0x40 + \index
	csrw	CSR_SISELECT, t0
	check_m64_alias \csr
	enter_s32 .Ls_counter\@
.Ls_counter\@:
#ifdef TEST_HIGH_HALF
	csrr	t0, CSR_SIREG4		/* sireg4: original high half */
	load32 t1, 0x12345678
	bne	t0, t1, .Ls_fail\@
	load32 t0, 0x07654321
	csrw	CSR_SIREG4, t0
	csrr	t1, CSR_SIREG4
	bne	t0, t1, .Ls_fail\@
	/* A high-half write must preserve the low half. */
	csrr	t0, CSR_SIREG
	load32 t1, 0x11223344
#else
	csrr	t0, CSR_SIREG		/* sireg: original low half */
	load32 t1, 0x11223344
	bne	t0, t1, .Ls_fail\@
	load32 t0, 0x55667788
	csrw	CSR_SIREG, t0
	csrr	t1, CSR_SIREG
#endif
	bne	t0, t1, .Ls_fail\@
	li	a0, 0
	j	.Ls_done\@
.Ls_fail\@:
	mv	a0, s10
.Ls_done\@:
	ecall

	/* Read the complete machine counter to check the unwritten half. */
	csrr	t0, \csr
#ifdef TEST_HIGH_HALF
	li	t1, 0x0765432111223344
#else
	li	t1, 0x1234567855667788
#endif
	bne	t0, t1, fail
	.endm

	.macro	check_config index, csr, old_low, new_low
	li	s10, \index + 1
	li	t0, 0x40 + \index
	csrw	CSR_SISELECT, t0
	li	t0, 0x6000000000000000 | \old_low /* MINH | SINH */
	csrw	\csr, t0
	enter_s32 .Ls_config\@
.Ls_config\@:
	csrr	t0, CSR_SIREG2		/* sireg2: config/selector low half */
	li	t1, \old_low
	bne	t0, t1, .Lcfg_fail\@
	li	t0, \new_low
	csrw	CSR_SIREG2, t0
	csrr	t1, CSR_SIREG2
	bne	t0, t1, .Lcfg_fail\@

	/* Low-half writes preserve the high half; MINH reads as zero. */
	csrr	t0, CSR_SIREG5
	li	t1, 0x20000000		/* SINH, without MINH */
	bne	t0, t1, .Lcfg_fail\@
	li	t0, 0x10000000		/* Replace SINH with UINH. */
	csrw	CSR_SIREG5, t0
	csrr	t1, CSR_SIREG5
	bne	t0, t1, .Lcfg_fail\@
	csrr	t0, CSR_SIREG2
	li	t1, \new_low
	bne	t0, t1, .Lcfg_fail\@
	li	a0, 0
	j	.Lcfg_done\@
.Lcfg_fail\@:
	mv	a0, s10
.Lcfg_done\@:
	ecall

	/* Both the low half and the machine-only MINH bit must be retained. */
	csrr	t0, \csr
	li	t1, 0x5000000000000000 | \new_low /* MINH | UINH */
	bne	t0, t1, fail
	.endm

	.text
	.global	_start
_start:
	li	s10, 31			/* Setup failure. */
	lla	t0, trap
	csrw	mtvec, t0
	csrw	medeleg, zero
	csrw	mie, zero
	li	t0, -1
	/* Freeze counters for exact comparisons. */
	csrw	mcountinhibit, t0
	csrw	mcounteren, t0
	csrw	pmpaddr0, t0
	li	t0, 0x1f		/* Allow S-mode access to RAM. */
	csrw	pmpcfg0, t0
	li	t0, 1 << 60
	csrw	CSR_MENVCFG, t0		/* CDE */
	/* mstateen0: allow indirect CSRs. */
	csrw	CSR_MSTATEEN0, t0

	/* Select RV32 for lower privilege modes; M-mode remains RV64. */
	csrr	t0, mstatus
	li	t1, (3 << 34) | (3 << 32)
	not	t1, t1
	and	t0, t0, t1
	li	t1, (1 << 34) | (1 << 32)
	or	t0, t0, t1
	csrw	mstatus, t0
	csrr	t0, mstatus
	srli	t0, t0, 34
	andi	t0, t0, 3
	li	t1, 1
	bne	t0, t1, fail

#ifdef TEST_CONFIG
	check_config 0, CSR_MCYCLECFG, 0, 0
	check_config 2, CSR_MINSTRETCFG, 0, 0
	/* mhpmevent3: cycles -> instructions */
	check_config 3, mhpmevent3, 1, 2
#else
	li	t0, 1
	csrw	mhpmevent3, t0		/* HPM3 counts cycles. */
	li	t0, 2
	csrw	mhpmevent4, t0		/* HPM4 counts instructions. */
	/* HPM5 has no selected event. */
	csrw	mhpmevent5, zero
	check_counter 0, mcycle
	check_counter 2, minstret
	check_counter 3, mhpmcounter3
	check_counter 4, mhpmcounter4
	check_counter 5, mhpmcounter5
#endif
	li	a0, 0
	j	exit

	.balign	4
trap:
	csrr	t0, mcause
	bne	t0, a5, fail
	bnez	a0, exit
	addi	s9, s9, 1
	csrr	t0, mepc
	/* Zero-extend the RV32 trap PC for physical addressing in M-mode. */
	slli	t0, t0, 32
	srli	t0, t0, 32
	addi	t0, t0, 4
	csrw	mepc, t0
	li	t0, 3 << 11
	csrs	mstatus, t0		/* Resume in M-mode. */
	mret

fail:
	mv	a0, s10
exit:
	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
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
