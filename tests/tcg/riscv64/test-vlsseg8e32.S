#
# QEMU RISC-V Vector Strided Load Instruction testcase
#
# Copyright (c) 2025 Chao Liu chao.liu@yeah.net
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
	.option	norvc

	.section .data
	.align 4
source_data:
	.asciz "Test the vssseg8e32 insn by copy 64b and verifying correctness."
	.equ source_len, 64

	.text
	.global _start
_start:
	lla	t0, trap
	csrw	mtvec, t0

enable_rvv:

	li	x15, 0x800000000024112d
	csrw	0x301, x15
	li	x1, 0x2200
	csrr	x2, mstatus
	or	x2, x2, x1
	csrw	mstatus, x2

rvv_test_func:
	la	a0, source_data
	li	a1, 0x80020000
	vsetivli	zero, 1, e32, m1, ta, ma
	li	t0, 64

	vlsseg8e32.v	v0, (a0), t0
	addi	a0, a0, 32
	vlsseg8e32.v	v8, (a0), t0

	vssseg8e32.v	v0, (a1), t0
	addi	a1, a1, 32
	vssseg8e32.v	v8, (a1), t0

compare_start:
	la	a0, source_data
	li	a1, 0x80020000
	li	t0, 0
	li	t1, source_len

compare_loop:
	# when t0 >= len, compare end
	bge	 t0, t1, compare_done

	lb	t2, 0(a0)
	lb	t3, 0(a1)
	bne	t2, t3, compare_fail

	addi	a0, a0, 1
	addi	a1, a1, 1
	addi	t0, t0, 1
	j	compare_loop

compare_done:
	# compare ok, return 0
	li	a0, 0
	j	_exit

compare_fail:
	# compare failed, return 2
	li	a0, 2
	j	_exit

trap:
	# When an instruction traps, compare it to the insn in memory.
	csrr	t0, mepc
	csrr	t1, mtval
	lwu	t2, 0(t0)
	bne	t1, t2, fail

	# Skip the insn and continue.
	addi	t0, t0, 4
	csrw	mepc, t0
	mret

fail:
	li	a0, 1

# Exit code in a0
_exit:
	lla	a1, semiargs
	li	t0, 0x20026	# ADP_Stopped_ApplicationExit
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	# TARGET_SYS_EXIT_EXTENDED

	# Semihosting call sequence
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	16
