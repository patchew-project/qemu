/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Shared harness for the Smmpt (Supervisor Memory Protection Table) system
 * tests, per SMMTT specification v0.4.9.
 *
 * This header is meant to be #included by the per-mode Smmpt test programs
 * (Smmpt34 on RV32, Smmpt43/52/64 on RV64).  All the mode-independent logic
 * lives here so each test only has to build its mode-specific MPT and invoke
 * RUN_LEAF_CHECKS.
 *
 * The tests run in M-mode and use the MPRV trick (mstatus.MPRV=1,
 * mstatus.MPP=S, satp=Bare so PA==VA) to subject explicit data accesses to
 * MPT checks while the instruction stream keeps running in M-mode (M-mode
 * fetches bypass the MPT).  The data-access width is irrelevant to the MPT
 * check, so the harness uses lw/sw and works unmodified on RV32 and RV64.
 *
 * The MPT XWR field uses the standard bit order R=bit0, W=bit1, X=bit2
 * (matching QEMU PAGE_READ/PAGE_WRITE/PAGE_EXEC), so Read+Write is 0b011=3.
 * Per the spec the exception is reported for the original access type: a load
 * violation raises "load access fault" (cause 5) and a store violation raises
 * "store/AMO access fault" (cause 7).
 *
 * Execute permission is not exercised: the MPRV trick only redirects data
 * accesses, while instruction fetch stays in M-mode and bypasses the MPT.
 * X-bearing encodings are therefore checked only for their load/store
 * behaviour (e.g. X-only denies loads, R+X denies stores).
 *
 * The tests expect an MPT laid out so that these supervisor physical
 * addresses resolve as follows (identical across all modes):
 *
 *   0x8050_0000..0x8050_7000  non-NAPOT leaf, XWR[pi] tuples (LEAF_ALL_XWR):
 *                               pi0 011 (RW)   pi1 000 (no access)
 *                               pi2 001 (R)    pi3 101 (R+X)
 *                               pi4 111 (RWX)  pi5 010 (reserved)
 *                               pi6 110 (rsvd) pi7 100 (X only)
 *   0x8051_0000               invalid leaf (V=0)
 *   0x8052_0000               leaf with a reserved bit set
 *   0x8060_0000               NAPOT leaf with a valid G
 *   0x8080_0000               NAPOT leaf with a reserved G
 *
 * On any mismatch the test exits (via semihosting) with a non-zero code that
 * identifies the failing check; a successful run exits with 0.
 */

	/* mstatus bits (same positions on RV32 and RV64) */
	.equ	MSTATUS_MPP_S,	(1 << 11)	/* MPP = 01 (Supervisor) */
	.equ	MSTATUS_MPP_M,	(3 << 11)	/* MPP mask */
	.equ	MSTATUS_MPRV,	(1 << 17)

	/* Expected fault causes */
	.equ	CAUSE_LOAD_ACCESS_FAULT,	5
	.equ	CAUSE_STORE_ACCESS_FAULT,	7

	.equ	EXPECT_OK,	0
	.equ	EXPECT_FAULT,	1

	/*
	 * Non-NAPOT leaf value carrying all eight XWR encodings in pi0..pi7:
	 *   V|L | pi0=011 pi1=000 pi2=001 pi3=101
	 *         pi4=111 pi5=010 pi6=110 pi7=100
	 * The XWR tuples occupy bits [31:8], valid for both the 4-byte (RV32)
	 * and 8-byte (RV64) leaf formats.
	 */
	.equ	LEAF_ALL_XWR,	0x997A4303

	/*
	 * Switch the effective privilege of data accesses to S-mode so that
	 * they are subject to MPT checks (MPP=S, MPRV=1).  Uses t0 only.
	 */
	.macro	ENTER_MPRV
	li	t0, MSTATUS_MPP_M
	csrc	mstatus, t0		/* clear MPP */
	li	t0, MSTATUS_MPP_S
	csrs	mstatus, t0		/* MPP = S */
	li	t0, MSTATUS_MPRV
	csrs	mstatus, t0		/* enable MPRV */
	.endm

	.macro	EXIT_MPRV
	li	t0, MSTATUS_MPRV
	csrc	mstatus, t0		/* disable MPRV */
	.endm

	/*
	 * Perform an MPT-checked load/store from \addr and verify the outcome.
	 *   \eflt   : EXPECT_OK or EXPECT_FAULT
	 *   \ecause : expected mcause when a fault is expected
	 *   \code   : exit code reported if the check fails
	 */
	.macro	TEST_LOAD addr, eflt, ecause, code
	li	a3, \addr
	jal	do_load
	li	a4, \eflt
	li	a5, \ecause
	li	a6, \code
	jal	check
	.endm

	.macro	TEST_STORE addr, eflt, ecause, code
	li	a3, \addr
	li	t3, 0x1234
	jal	do_store
	li	a4, \eflt
	li	a5, \ecause
	li	a6, \code
	jal	check
	.endm

	/*
	 * MFENCE.PA (funct7=0b0011001) must not fault in M-mode.  Reuses the
	 * generic check subroutine (expects no fault).
	 */
	.macro	TEST_MFENCE_PA code
	li	s0, 0
	.insn	r 0x73, 0, 0x19, x0, x0, x0
	li	a4, EXPECT_OK
	li	a6, \code
	jal	check
	.endm

	/*
	 * Run the full set of MPT permission and structural checks against the
	 * shared address layout described above.
	 */
	.macro	RUN_LEAF_CHECKS
	/* pi0 RW: load and store both allowed */
	TEST_LOAD  0x80500000, EXPECT_OK,    0,                        2
	TEST_STORE 0x80500000, EXPECT_OK,    0,                        3
	/* pi1 no-access: load faults (cause 5), store faults (cause 7) */
	TEST_LOAD  0x80501000, EXPECT_FAULT, CAUSE_LOAD_ACCESS_FAULT,  4
	TEST_STORE 0x80501000, EXPECT_FAULT, CAUSE_STORE_ACCESS_FAULT, 5
	/* pi2 R-only: load allowed, store faults */
	TEST_LOAD  0x80502000, EXPECT_OK,    0,                        6
	TEST_STORE 0x80502000, EXPECT_FAULT, CAUSE_STORE_ACCESS_FAULT, 7
	/* pi3 R+X: load allowed, store faults */
	TEST_LOAD  0x80503000, EXPECT_OK,    0,                        8
	TEST_STORE 0x80503000, EXPECT_FAULT, CAUSE_STORE_ACCESS_FAULT, 9
	/* pi4 RWX: load and store both allowed */
	TEST_LOAD  0x80504000, EXPECT_OK,    0,                        10
	TEST_STORE 0x80504000, EXPECT_OK,    0,                        11
	/* pi5/pi6 reserved encodings (010/110): load faults (cause 5) */
	TEST_LOAD  0x80505000, EXPECT_FAULT, CAUSE_LOAD_ACCESS_FAULT,  12
	TEST_LOAD  0x80506000, EXPECT_FAULT, CAUSE_LOAD_ACCESS_FAULT,  13
	/* pi7 X-only: load faults (cause 5) */
	TEST_LOAD  0x80507000, EXPECT_FAULT, CAUSE_LOAD_ACCESS_FAULT,  14
	/* invalid leaf (V=0): load faults (cause 5) */
	TEST_LOAD  0x80510000, EXPECT_FAULT, CAUSE_LOAD_ACCESS_FAULT,  15
	/* leaf with a reserved bit set: load faults (cause 5) */
	TEST_LOAD  0x80520000, EXPECT_FAULT, CAUSE_LOAD_ACCESS_FAULT,  16
	/* NAPOT leaf, valid G, RW: load and store both allowed */
	TEST_LOAD  0x80600000, EXPECT_OK,    0,                        17
	TEST_STORE 0x80600000, EXPECT_OK,    0,                        18
	/* NAPOT leaf with a reserved G: load faults (cause 5) */
	TEST_LOAD  0x80800000, EXPECT_FAULT, CAUSE_LOAD_ACCESS_FAULT,  19
	.endm

	/*
	 * Emit the shared subroutines, trap handler, semihosting exit and data.
	 * Invoke once, after the test's _start code.
	 */
	.macro	SMMPT_HARNESS
	/*
	 * do_load / do_store: perform one MPT-checked access from a3.
	 * On return, s0 = 1 if the access faulted (else 0) and s1 = mcause.
	 * do_store uses the value in t3.  lw/sw keep these width-agnostic.
	 */
	.balign	4
do_load:
	li	s0, 0
	li	s1, 0
	ENTER_MPRV
	lw	t4, 0(a3)		/* MPT-checked load */
	EXIT_MPRV
	ret

do_store:
	li	s0, 0
	li	s1, 0
	ENTER_MPRV
	sw	t3, 0(a3)		/* MPT-checked store */
	EXIT_MPRV
	ret

	/*
	 * check: verify the outcome of the last access.
	 *   a4 = expected fault (0/1), a5 = expected cause, a6 = fail code.
	 * Returns on success; exits with a6 on mismatch.
	 */
	.balign	4
check:
	beqz	a4, 1f
	beqz	s0, 2f			/* expected a fault but none occurred */
	bne	s1, a5, 2f		/* faulted with the wrong cause */
	ret
1:
	bnez	s0, 2f			/* unexpected fault */
	ret
2:
	mv	a0, a6
	j	_exit

	/*
	 * M-mode trap handler.  Records that a fault occurred (s0=1) and the
	 * mcause (s1), skips the faulting 4-byte instruction, disables MPRV
	 * and returns.  Only clobbers t5/t6 besides s0/s1.
	 */
	.balign	4
mtrap:
	csrr	t5, mcause
	li	s0, 1
	mv	s1, t5
	csrr	t6, mepc
	addi	t6, t6, 4		/* skip the faulting instruction */
	csrw	mepc, t6
	li	t5, MSTATUS_MPRV
	csrc	mstatus, t5		/* ensure MPRV is off on return */
	mret

	/* Exit via semihosting (ADP_Stopped_ApplicationExit) */
_exit:
#if __riscv_xlen == 32
	/*
	 * On RV32 the SYS_EXIT_EXTENDED parameter block cannot be used: when
	 * this test runs on qemu-system-riscv64 with a 32-bit CPU the block
	 * pointer is sign-extended (0xffffffff_8xxx_xxxx) and the semihosting
	 * argument read faults.  Use the plain SYS_EXIT, which takes the exit
	 * reason directly in a1: a successful run (a0==0) reports
	 * ADP_Stopped_ApplicationExit and any failure reports a non-zero
	 * reason (reported by QEMU as exit status 1).
	 */
	li	a1, 0x20026		/* ADP_Stopped_ApplicationExit */
	beqz	a0, 1f
	mv	a1, a0			/* non-zero reason -> exit status 1 */
1:
	li	a0, 0x18		/* TARGET_SYS_EXIT */
#else
	lla	a1, semiargs
	li	t0, 0x20026
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20		/* TARGET_SYS_EXIT_EXTENDED */
#endif

	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

#if __riscv_xlen != 32
	.data
	.balign	8
semiargs:
	.space	16
#endif
	.endm
