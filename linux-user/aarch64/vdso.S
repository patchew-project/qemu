/*
 * aarch64 linux replacement vdso.
 *
 * Copyright 2021 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>

/* ??? These are in include/elf.h, which is not ready for inclusion in asm. */
#define NT_GNU_PROPERTY_TYPE_0  5
#define GNU_PROPERTY_AARCH64_FEATURE_1_AND      0xc0000000
#define GNU_PROPERTY_AARCH64_FEATURE_1_BTI      (1U << 0)
#define GNU_PROPERTY_AARCH64_FEATURE_1_PAC      (1U << 1)

#define GNU_PROPERTY_AARCH64_FEATURE_1_DEFAULT \
    (GNU_PROPERTY_AARCH64_FEATURE_1_BTI | GNU_PROPERTY_AARCH64_FEATURE_1_PAC)

	.text

/*
 * TODO: The kernel makes a big deal of turning off the .cfi directives,
 * because they cause libgcc to crash, but that's because they're wrong.
 *
 * For now, elide the unwind info for __kernel_rt_sigreturn and rely on
 * the libgcc fallback routine as we have always done.  This requires
 * that the code sequence used be exact.
 */
	.align	4
__kernel_rt_sigreturn:
	/* No BTI C insn here -- we arrive via RET. */
	mov	x8, #__NR_rt_sigreturn
	svc	#0

	.globl	__kernel_rt_sigreturn
	.type	__kernel_rt_sigreturn, %function
	.size	__kernel_rt_sigreturn, . - __kernel_rt_sigreturn

	.align	4
__kernel_gettimeofday:
	.cfi_startproc
	bti	c
	mov	x8, #__NR_gettimeofday
	svc	#0
	ret
	.cfi_endproc

	.globl	__kernel_gettimeofday
	.type	__kernel_gettimeofday, %function
	.size	__kernel_gettimeofday, . - __kernel_gettimeofday

	.align	4
__kernel_clock_gettime:
	.cfi_startproc
	bti	c
	mov	x8, #__NR_clock_gettime
	svc	#0
	ret
	.cfi_endproc

	.globl	__kernel_clock_gettime
	.type	__kernel_clock_gettime, %function
	.size	__kernel_clock_gettime, . - __kernel_clock_gettime

	.align	4
__kernel_clock_getres:
	.cfi_startproc
	bti	c
	mov	x8, #__NR_clock_getres
	svc	#0
	ret
	.cfi_endproc

	.globl	__kernel_clock_getres
	.type	__kernel_clock_getres, %function
	.size	__kernel_clock_getres, . - __kernel_clock_getres
