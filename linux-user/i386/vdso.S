/*
 * i386 linux replacement vdso.
 *
 * Copyright 2021 Linaro, Ltd.
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>

	.balign	16
__kernel_vsyscall:
	.cfi_startproc
	int	$0x80
	ret
	.cfi_endproc

	.globl	__kernel_vsyscall
	.type	__kernel_vsyscall, @function
	.size	__kernel_vsyscall, . - __kernel_vsyscall

	.balign	16
	.cfi_startproc
__vdso_clock_gettime:
	mov	$__NR_clock_gettime, %eax
	int	$0x80
	ret
	.cfi_endproc

	.globl	__vdso_clock_gettime
	.type	__vdso_clock_gettime, @function
	.size	__vdso_clock_gettime, . - __vdso_clock_gettime

	.balign	16
	.cfi_startproc
__vdso_clock_gettime64:
	mov	$__NR_clock_gettime64, %eax
	int	$0x80
	ret
	.cfi_endproc

	.globl	__vdso_clock_gettime64
	.type	__vdso_clock_gettime64, @function
	.size	__vdso_clock_gettime64, . - __vdso_clock_gettime64

	.balign	16
	.cfi_startproc
__vdso_clock_getres:
	mov	$__NR_clock_getres, %eax
	int	$0x80
	ret
	.cfi_endproc

	.globl	__vdso_clock_getres
	.type	__vdso_clock_getres, @function
	.size	__vdso_clock_getres, . - __vdso_clock_getres

	.balign	16
	.cfi_startproc
__vdso_gettimeofday:
	mov	$__NR_gettimeofday, %eax
	int	$0x80
	ret
	.cfi_endproc

	.globl	__vdso_gettimeofday
	.type	__vdso_gettimeofday, @function
	.size	__vdso_gettimeofday, . - __vdso_gettimeofday

	.balign	16
	.cfi_startproc
__vdso_time:
	mov	$__NR_time, %eax
	int	$0x80
	ret
	.cfi_endproc

	.globl	__vdso_time
	.type	__vdso_time, @function
	.size	__vdso_time, . - __vdso_time

	/*
	 * While this frame is marked as a signal frame, that only applies
	 * to how this return address is handled for the outer frame.
	 * The return address that arrived here, from the inner frame, is
	 * not marked as a signal frame and so the unwinder still tries to
	 * subtract 1 to examine the presumed call insn.  Thus we must
	 * extend the unwind info to a nop before the start.
	 */

	.balign 16
	.cfi_startproc simple
	.cfi_signal_frame

	/*
	 * For convenience, put the cfa just above eip in sigcontext,
	 * and count offsets backward from there.  Re-compute the cfa
	 * in the several contexts we have for signal unwinding.
         * This is far simpler than the DW_CFA_expression form that
         * the kernel uses, and is equally correct.
	 */
#define IA32_SIGCONTEXT_cfa           60
#define IA32_RT_SIGFRAME_sigcontext  164

	.cfi_def_cfa	%esp, IA32_SIGCONTEXT_cfa + 4
	.cfi_offset	%eip, -4
			/* err, -8 */
			/* trapno, -12 */
	.cfi_offset	%eax, -16
	.cfi_offset	%ecx, -20
	.cfi_offset	%edx, -24
	.cfi_offset	%ebx, -28
	.cfi_offset	%esp, -32
	.cfi_offset	%ebp, -36
	.cfi_offset	%esi, -40
	.cfi_offset	%edi, -44

	nop
__kernel_sigreturn:
	popl	%eax	/* pop sig */
	.cfi_adjust_cfa_offset -4
	movl	$__NR_sigreturn, %eax
	int	$0x80

	.globl	__kernel_sigreturn
	.type	__kernel_sigreturn, @function
	.size	__kernel_sigreturn, . - __kernel_sigreturn

	.cfi_adjust_cfa_offset IA32_RT_SIGFRAME_sigcontext - 4
	nop
__kernel_rt_sigreturn:
	movl	$__NR_rt_sigreturn, %eax
	int	$0x80

	.globl	__kernel_rt_sigreturn
	.type	__kernel_rt_sigreturn, @function
	.size	__kernel_rt_sigreturn, . - __kernel_rt_sigreturn
	.cfi_endproc

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
