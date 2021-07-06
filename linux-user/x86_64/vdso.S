/*
 * x86-64 linux replacement vdso.
 *
 * Copyright 2021 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>

	.globl	__vdso_clock_gettime
	.type	__vdso_clock_gettime, @function
	.balign	16
	.cfi_startproc
__vdso_clock_gettime:
	mov	$__NR_clock_gettime, %eax
	syscall
	ret
	.cfi_endproc
	.size	__vdso_clock_gettime, . - __vdso_clock_gettime

clock_gettime = __vdso_clock_gettime
	.weak	clock_gettime

	.globl	__vdso_clock_getres
	.type	__vdso_clock_getres, @function
	.balign	16
	.cfi_startproc
__vdso_clock_getres:
	mov	$__NR_clock_getres, %eax
	syscall
	ret
	.cfi_endproc
	.size	__vdso_clock_getres, . - __vdso_clock_getres

clock_getres = __vdso_clock_getres
	.weak	clock_getres

	.globl	__vdso_gettimeofday
	.type	__vdso_gettimeofday, @function
	.balign	16
	.cfi_startproc
__vdso_gettimeofday:
	mov	$__NR_gettimeofday, %eax
	syscall
	ret
	.cfi_endproc
	.size	__vdso_gettimeofday, . - __vdso_gettimeofday

gettimeofday = __vdso_gettimeofday
	.weak	gettimeofday


	.globl	__vdso_time
	.type	__vdso_time, @function
	.balign	16
	.cfi_startproc
__vdso_time:
	mov	$__NR_time, %eax
	syscall
	ret
	.cfi_endproc
	.size	__vdso_time, . - __vdso_time

time = __vdso_time
	.weak	time


	.globl	__vdso_getcpu
	.type	__vdso_getcpu, @function
	.balign	16
	.cfi_startproc
__vdso_getcpu:
	/*
         * ??? There is no syscall number for this allocated on x64.
	 * We can handle this several ways:
         *
	 * (1) Invent a syscall number for use within qemu.
         *     It should be easy enough to pick a number that
         *     is well out of the way of the kernel numbers.
         *
         * (2) Force the emulated cpu to support the rdtscp insn,
	 *     and initialize the TSC_AUX value the appropriate value.
         *
	 * (3) Pretend that we're always running on cpu 0.
         *
	 * This last is the one that's implemented here, with the
	 * tiny bit of extra code to support rdtscp in place.
         */
	xor	%ecx, %ecx		/* rdtscp w/ tsc_aux = 0 */

	/* if (cpu != NULL) *cpu = (ecx & 0xfff); */
	test	%rdi, %rdi
	jz	1f
	mov	%ecx, %eax
	and	$0xfff, %eax
	mov	%eax, (%rdi)

	/* if (node != NULL) *node = (ecx >> 12); */
1:	test	%rsi, %rsi
	jz	2f
	shr	$12, %ecx
	mov	%ecx, (%rsi)

2:	xor	%eax, %eax
	ret
	.cfi_endproc
	.size	__vdso_getcpu, . - __vdso_getcpu

getcpu = __vdso_getcpu
	.weak	getcpu

/*
 * ??? Perhaps add elf notes.  E.g.
 *
 * #include <linux/elfnote.h>
 * ELFNOTE_START(Linux, 0, "a")
 *   .long LINUX_VERSION_CODE
 * ELFNOTE_END
 *
 * but what version number would we set for QEMU?
 */
