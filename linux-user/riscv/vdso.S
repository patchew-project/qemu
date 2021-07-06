/*
 * RISC-V linux replacement vdso.
 *
 * Copyright 2021 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>
#include <asm/errno.h>

.macro syscall nr
	li	a7, \nr
	ecall
.endm

	.text
	.balign	16
__vdso_gettimeofday:
	.cfi_startproc
#ifdef __NR_gettimeofday
	syscall __NR_gettimeofday
	ret
#else
	/* No gettimeofday, fall back to clock_gettime64. */
	beq	a1, zero, 1f
	sw	zero, 0(a1)	/* tz->tz_minuteswest = 0 */
	sw	zero, 4(a1)	/* tz->tz_dsttime = 0 */
1:	addi	sp, sp, -32
	.cfi_adjust_cfa_offset 32
	sw	a0, 16(sp)	/* save tv */
	mv	a0, sp
	syscall __NR_clock_gettime64
	lw	t0, 0(sp)	/* timespec.tv_sec.low */
	lw	t1, 4(sp)	/* timespec.tv_sec.high */
	lw	t2, 8(sp)	/* timespec.tv_nsec.low */
	lw	a1, 16(sp)	/* restore tv */
	addi	sp, sp, 32
	.cfi_adjust_cfa_offset -32
	bne	a0, zero, 9f	/* syscall error? */
	li	a0, -EOVERFLOW
	bne	t1, zero, 9f	/* y2038? */
	li	a0, 0
	li	t3, 1000
	divu	t2, t2, t3	/* nsec -> usec */
	sw	t0, 0(a1)	/* tz->tv_sec */
	sw	t2, 4(a1)	/* tz->tv_usec */
9:	ret
#endif
	.cfi_endproc

	.globl	__vdso_gettimeofday
	.type	__vdso_gettimeofday, %function
	.size	__vdso_gettimeofday, . - __vdso_gettimeofday

	.balign	16
__vdso_clock_gettime:
	.cfi_startproc
#ifdef __NR_clock_gettime
	syscall __NR_clock_gettime
#else
	syscall __NR_clock_gettime64
#endif
	ret
	.cfi_endproc

	.globl	__vdso_clock_gettime
	.type	__vdso_clock_gettime, %function
	.size	__vdso_clock_gettime, . - __vdso_clock_gettime

	.balign	16
__vdso_clock_getres:
	.cfi_startproc
#ifdef __NR_clock_getres
	syscall __NR_clock_getres
#else
	syscall __NR_clock_getres_time64
#endif
	ret
	.cfi_endproc

	.globl	__vdso_clock_getres
	.type	__vdso_clock_getres, %function
	.size	__vdso_clock_getres, . - __vdso_clock_getres

	.balign	16
__vdso_getcpu:
	.cfi_startproc
	syscall __NR_getcpu
	ret
	.cfi_endproc

	.globl	__vdso_getcpu
	.type	__vdso_getcpu, %function
	.size	__vdso_getcpu, . - __vdso_getcpu

	.balign	16
__vdso_flush_icache:
	.cfi_startproc
	/* qemu does not need to flush the icache */
	li	a0, 0
	ret
	.cfi_endproc

	.globl	__vdso_flush_icache
	.type	__vdso_flush_icache, %function
	.size	__vdso_flush_icache, . - __vdso_flush_icache

/*
 * Start the unwind info at least one instruction before the signal
 * trampoline, because the unwinder will assume we are returning
 * after a call site.
 */

	.cfi_startproc simple
	.cfi_signal_frame

#if __riscv_xlen == 32
# define offsetof_uc_mcontext	0x120
#else
# define offsetof_uc_mcontext	0x130
#endif
#define sizeof_reg		(__riscv_xlen / 4)
#define sizeof_freg		8
#define offsetof_freg0		(sizeof_reg * 32)

	.cfi_def_cfa	2, offsetof_uc_mcontext

	/* Return address */
	.cfi_return_column 64
	.cfi_offset	64, 0			/* pc */

	/* Integer registers */
	.cfi_offset	1, 1 * sizeof_reg	/* r1 (ra) */
	.cfi_offset	2, 2 * sizeof_reg	/* r2 (sp) */
	.cfi_offset	3, 3 * sizeof_reg
	.cfi_offset	4, 4 * sizeof_reg
	.cfi_offset	5, 5 * sizeof_reg
	.cfi_offset	6, 6 * sizeof_reg
	.cfi_offset	7, 7 * sizeof_reg
	.cfi_offset	8, sizeof_reg * 8
	.cfi_offset	9, 9 * sizeof_reg
	.cfi_offset	10, 10 * sizeof_reg
	.cfi_offset	11, 11 * sizeof_reg
	.cfi_offset	12, 12 * sizeof_reg
	.cfi_offset	13, 13 * sizeof_reg
	.cfi_offset	14, 14 * sizeof_reg
	.cfi_offset	15, 15 * sizeof_reg
	.cfi_offset	16, 16 * sizeof_reg
	.cfi_offset	17, 17 * sizeof_reg
	.cfi_offset	18, 18 * sizeof_reg
	.cfi_offset	19, 19 * sizeof_reg
	.cfi_offset	20, 20 * sizeof_reg
	.cfi_offset	21, 21 * sizeof_reg
	.cfi_offset	22, 22 * sizeof_reg
	.cfi_offset	23, 23 * sizeof_reg
	.cfi_offset	24, 24 * sizeof_reg
	.cfi_offset	25, 25 * sizeof_reg
	.cfi_offset	26, 26 * sizeof_reg
	.cfi_offset	27, 27 * sizeof_reg
	.cfi_offset	28, 28 * sizeof_reg
	.cfi_offset	29, 29 * sizeof_reg
	.cfi_offset	30, 30 * sizeof_reg
	.cfi_offset	31, 31 * sizeof_reg	/* r31 */

	.cfi_offset	32, offsetof_freg0			/* f0 */
	.cfi_offset	33, offsetof_freg0 + 1 * sizeof_freg	/* f1 */
	.cfi_offset	34, offsetof_freg0 + 2 * sizeof_freg
	.cfi_offset	35, offsetof_freg0 + 3 * sizeof_freg
	.cfi_offset	36, offsetof_freg0 + 4 * sizeof_freg
	.cfi_offset	37, offsetof_freg0 + 5 * sizeof_freg
	.cfi_offset	38, offsetof_freg0 + 6 * sizeof_freg
	.cfi_offset	39, offsetof_freg0 + 7 * sizeof_freg
	.cfi_offset	40, offsetof_freg0 + 8 * sizeof_freg
	.cfi_offset	41, offsetof_freg0 + 9 * sizeof_freg
	.cfi_offset	42, offsetof_freg0 + 10 * sizeof_freg
	.cfi_offset	43, offsetof_freg0 + 11 * sizeof_freg
	.cfi_offset	44, offsetof_freg0 + 12 * sizeof_freg
	.cfi_offset	45, offsetof_freg0 + 13 * sizeof_freg
	.cfi_offset	46, offsetof_freg0 + 14 * sizeof_freg
	.cfi_offset	47, offsetof_freg0 + 15 * sizeof_freg
	.cfi_offset	48, offsetof_freg0 + 16 * sizeof_freg
	.cfi_offset	49, offsetof_freg0 + 17 * sizeof_freg
	.cfi_offset	50, offsetof_freg0 + 18 * sizeof_freg
	.cfi_offset	51, offsetof_freg0 + 19 * sizeof_freg
	.cfi_offset	52, offsetof_freg0 + 20 * sizeof_freg
	.cfi_offset	53, offsetof_freg0 + 21 * sizeof_freg
	.cfi_offset	54, offsetof_freg0 + 22 * sizeof_freg
	.cfi_offset	55, offsetof_freg0 + 23 * sizeof_freg
	.cfi_offset	56, offsetof_freg0 + 24 * sizeof_freg
	.cfi_offset	57, offsetof_freg0 + 25 * sizeof_freg
	.cfi_offset	58, offsetof_freg0 + 26 * sizeof_freg
	.cfi_offset	59, offsetof_freg0 + 27 * sizeof_freg
	.cfi_offset	60, offsetof_freg0 + 28 * sizeof_freg
	.cfi_offset	61, offsetof_freg0 + 29 * sizeof_freg
	.cfi_offset	62, offsetof_freg0 + 30 * sizeof_freg
	.cfi_offset	63, offsetof_freg0 + 31 * sizeof_freg	/* f31 */

	nop

__vdso_rt_sigreturn:
	syscall	__NR_rt_sigreturn
	.cfi_endproc

	.globl	__vdso_rt_sigreturn
	.type	__vdso_rt_sigreturn, %function
	.size	__vdso_rt_sigreturn, . - __vdso_rt_sigreturn
