/*
 * Test mret to S-mode when PMP is implemented but no PMP entry is
 * configured.
 *
 * The mret must succeed and the first S-mode fetch must fail [1][2].
 * The trap is therefore taken from S-mode, with mcause 1, mepc and
 * mtval equal to the mret target [3][4] and mstatus.MPP equal to S
 * [5].
 *
 * Exit code is 0 on success, otherwise the number of the failed check.
 *
 * [1] Privileged ISA v1.12, 3.7.1 "Physical Memory Protection CSRs",
 *     Priority and Matching Logic
 * [2] Privileged ISA v1.12, 3.3.2 "Trap-Return Instructions"
 * [3] Privileged ISA v1.12, 3.1.14 "Machine Exception Program Counter"
 * [4] Privileged ISA v1.12, 3.1.16 "Machine Trap Value Register"
 * [5] Privileged ISA v1.12, 3.1.6.1 "Privilege and Global
 *     Interrupt-Enable Stack in mstatus register"
 *
 * Privileged ISA v1.12 (ratified, 2021-12-03):
 * https://github.com/riscv/riscv-isa-manual/releases/tag/Priv-v1.12
 * Current source, same wording:
 * https://github.com/riscv/riscv-isa-manual/blob/4877e41a/src/priv/machine.adoc
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norvc

	.text
	.global _start
_start:
	/* Set up machine trap vector, handle everything in M-mode */
	lla	t0, machine_trap_handler
	csrw	mtvec, t0
	csrw	medeleg, zero

	/* mstatus.MPP = S */
	li	t0, 0x1800
	csrc	mstatus, t0
	li	t0, 0x0800
	csrs	mstatus, t0

	/* Return to S-mode at `target` with no PMP entry configured */
	lla	t0, target
	csrw	mepc, t0
	mret

target:
	/* Should be unreachable with no PMP */
	li	a0, 5
	j	_exit

machine_trap_handler:
	/* 1: mcause is instruction access fault */
	li	a0, 1
	csrr	t0, mcause
	li	t1, 1
	bne	t0, t1, _exit

	/* 2: mepc == target, not mret in _start */
	li	a0, 2
	csrr	t0, mepc
	lla	t1, target
	bne	t0, t1, _exit

	/* 3: mtval == target because aligned instructions */
	li	a0, 3
	csrr	t0, mtval
	bne	t0, t1, _exit

	/* 4: trap from S-mode */
	li	a0, 4
	csrr	t0, mstatus
	srli	t0, t0, 11
	andi	t0, t0, 3
	li	t1, 1
	bne	t0, t1, _exit

	li	a0, 0
	j	_exit

/* Exit with semihosting */
_exit:
	lla	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */

	/* Semihosting call sequence */
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	8
semiargs:
	.space	16
