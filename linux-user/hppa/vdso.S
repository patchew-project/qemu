/*
 * hppa linux kernel vdso replacement.
 *
 * Copyright 2021 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>

	.text

#define sizeof_rt_sigframe		696
#define offsetof_sigcontext		152
#define offsetof_sigcontext_gr		offsetof_sigcontext + 4
#define offsetof_sigcontext_fr		offsetof_sigcontext_gr + 32 * 4
#define offsetof_sigcontext_iasq	offsetof_sigcontext_fr + 32 * 8
#define offsetof_sigcontext_iaoq	offsetof_sigcontext_iasq + 8
#define offsetof_sigcontext_sar		offsetof_sigcontext_iaoq + 8

	/*
	 * While this frame is marked as a signal frame, that only applies
	 * to how this return address is handled for the outer frame.
	 * The return address that arrived here, from the inner frame, is
	 * not marked as a signal frame and so the unwinder still tries to
	 * subtract 1 to examine the presumed call insn.  Thus we must
	 * extend the unwind info to a nop before the start.
	 */

	.cfi_startproc simple
	.cfi_signal_frame

	/* Compare pa32_fallback_frame_state from libgcc. */

	/* Record the size of the stack frame. */
	.cfi_def_cfa	30, -sizeof_rt_sigframe

	/* Record save offset of general registers. */
	.cfi_offset	1, offsetof_sigcontext_gr + 1 * 4
	.cfi_offset	2, offsetof_sigcontext_gr + 2 * 4
	.cfi_offset	3, offsetof_sigcontext_gr + 3 * 4
	.cfi_offset	4, offsetof_sigcontext_gr + 4 * 4
	.cfi_offset	5, offsetof_sigcontext_gr + 5 * 4
	.cfi_offset	6, offsetof_sigcontext_gr + 6 * 4
	.cfi_offset	7, offsetof_sigcontext_gr + 7 * 4
	.cfi_offset	8, offsetof_sigcontext_gr + 8 * 4
	.cfi_offset	9, offsetof_sigcontext_gr + 9 * 4
	.cfi_offset	10, offsetof_sigcontext_gr + 10 * 4
	.cfi_offset	11, offsetof_sigcontext_gr + 11 * 4
	.cfi_offset	12, offsetof_sigcontext_gr + 12 * 4
	.cfi_offset	13, offsetof_sigcontext_gr + 13 * 4
	.cfi_offset	14, offsetof_sigcontext_gr + 14 * 4
	.cfi_offset	15, offsetof_sigcontext_gr + 15 * 4
	.cfi_offset	16, offsetof_sigcontext_gr + 16 * 4
	.cfi_offset	17, offsetof_sigcontext_gr + 17 * 4
	.cfi_offset	18, offsetof_sigcontext_gr + 18 * 4
	.cfi_offset	19, offsetof_sigcontext_gr + 19 * 4
	.cfi_offset	20, offsetof_sigcontext_gr + 20 * 4
	.cfi_offset	21, offsetof_sigcontext_gr + 21 * 4
	.cfi_offset	22, offsetof_sigcontext_gr + 22 * 4
	.cfi_offset	23, offsetof_sigcontext_gr + 23 * 4
	.cfi_offset	24, offsetof_sigcontext_gr + 24 * 4
	.cfi_offset	25, offsetof_sigcontext_gr + 25 * 4
	.cfi_offset	26, offsetof_sigcontext_gr + 26 * 4
	.cfi_offset	27, offsetof_sigcontext_gr + 27 * 4
	.cfi_offset	28, offsetof_sigcontext_gr + 28 * 4
	.cfi_offset	29, offsetof_sigcontext_gr + 29 * 4
	.cfi_offset	30, offsetof_sigcontext_gr + 30 * 4
	.cfi_offset	31, offsetof_sigcontext_gr + 31 * 4

	/* Record save offset of fp registers, left and right halves. */
	.cfi_offset	32, offsetof_sigcontext_fr + 4 * 8
	.cfi_offset	33, offsetof_sigcontext_fr + 4 * 8 + 4
	.cfi_offset	34, offsetof_sigcontext_fr + 5 * 8
	.cfi_offset	35, offsetof_sigcontext_fr + 5 * 8 + 4
	.cfi_offset	36, offsetof_sigcontext_fr + 6 * 8
	.cfi_offset	37, offsetof_sigcontext_fr + 6 * 8 + 4
	.cfi_offset	38, offsetof_sigcontext_fr + 7 * 8
	.cfi_offset	39, offsetof_sigcontext_fr + 7 * 8 + 4
	.cfi_offset	40, offsetof_sigcontext_fr + 8 * 8
	.cfi_offset	41, offsetof_sigcontext_fr + 8 * 8 + 4
	.cfi_offset	42, offsetof_sigcontext_fr + 9 * 8
	.cfi_offset	43, offsetof_sigcontext_fr + 9 * 8 + 4
	.cfi_offset	44, offsetof_sigcontext_fr + 10 * 8
	.cfi_offset	45, offsetof_sigcontext_fr + 10 * 8 + 4
	.cfi_offset	46, offsetof_sigcontext_fr + 11 * 8
	.cfi_offset	47, offsetof_sigcontext_fr + 11 * 8 + 4
	.cfi_offset	48, offsetof_sigcontext_fr + 12 * 8
	.cfi_offset	49, offsetof_sigcontext_fr + 12 * 8 + 4
	.cfi_offset	50, offsetof_sigcontext_fr + 13 * 8
	.cfi_offset	51, offsetof_sigcontext_fr + 13 * 8 + 4
	.cfi_offset	52, offsetof_sigcontext_fr + 14 * 8
	.cfi_offset	53, offsetof_sigcontext_fr + 14 * 8 + 4
	.cfi_offset	54, offsetof_sigcontext_fr + 15 * 8
	.cfi_offset	55, offsetof_sigcontext_fr + 15 * 8 + 4
	.cfi_offset	56, offsetof_sigcontext_fr + 16 * 8
	.cfi_offset	57, offsetof_sigcontext_fr + 16 * 8 + 4
	.cfi_offset	58, offsetof_sigcontext_fr + 17 * 8
	.cfi_offset	59, offsetof_sigcontext_fr + 17 * 8 + 4
	.cfi_offset	60, offsetof_sigcontext_fr + 18 * 8
	.cfi_offset	61, offsetof_sigcontext_fr + 18 * 8 + 4
	.cfi_offset	62, offsetof_sigcontext_fr + 19 * 8
	.cfi_offset	63, offsetof_sigcontext_fr + 19 * 8 + 4
	.cfi_offset	64, offsetof_sigcontext_fr + 20 * 8
	.cfi_offset	65, offsetof_sigcontext_fr + 20 * 8 + 4
	.cfi_offset	66, offsetof_sigcontext_fr + 21 * 8
	.cfi_offset	67, offsetof_sigcontext_fr + 21 * 8 + 4
	.cfi_offset	68, offsetof_sigcontext_fr + 22 * 8
	.cfi_offset	69, offsetof_sigcontext_fr + 22 * 8 + 4
	.cfi_offset	70, offsetof_sigcontext_fr + 23 * 8
	.cfi_offset	71, offsetof_sigcontext_fr + 23 * 8 + 4
	.cfi_offset	72, offsetof_sigcontext_fr + 24 * 8
	.cfi_offset	73, offsetof_sigcontext_fr + 24 * 8 + 4
	.cfi_offset	74, offsetof_sigcontext_fr + 25 * 8
	.cfi_offset	75, offsetof_sigcontext_fr + 25 * 8 + 4
	.cfi_offset	76, offsetof_sigcontext_fr + 26 * 8
	.cfi_offset	77, offsetof_sigcontext_fr + 26 * 8 + 4
	.cfi_offset	78, offsetof_sigcontext_fr + 27 * 8
	.cfi_offset	79, offsetof_sigcontext_fr + 27 * 8 + 4
	.cfi_offset	80, offsetof_sigcontext_fr + 28 * 8
	.cfi_offset	81, offsetof_sigcontext_fr + 28 * 8 + 4
	.cfi_offset	82, offsetof_sigcontext_fr + 29 * 8
	.cfi_offset	83, offsetof_sigcontext_fr + 29 * 8 + 4
	.cfi_offset	84, offsetof_sigcontext_fr + 30 * 8
	.cfi_offset	85, offsetof_sigcontext_fr + 30 * 8 + 4
	.cfi_offset	86, offsetof_sigcontext_fr + 31 * 8
	.cfi_offset	87, offsetof_sigcontext_fr + 31 * 8 + 4

	/* Record save offset of %sar */
	.cfi_offset	88, offsetof_sigcontext_sar

	/* Record save offset of return address, iaoq[0]. */
	.cfi_return_column 89
	.cfi_offset	89, offsetof_sigcontext_iaoq

	nop

__kernel_rt_sigreturn:

	ldi	0, %r25
	ldi	__NR_rt_sigreturn, %r20
	be,l	0x100(%sr2, %r0), %sr0, %r31
	nop

	.cfi_endproc

	.size	__kernel_rt_sigreturn, . - __kernel_rt_sigreturn
	.type	__kernel_rt_sigreturn, @function
	.globl	__kernel_rt_sigreturn
