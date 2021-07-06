/*
 * arm linux replacement vdso.
 *
 * Copyright 2021 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <asm/unistd.h>

	.text
	.eabi_attribute Tag_FP_arch, 0

#ifdef __thumb__
	.thumb
	.arch	armv7-m
	.eabi_attribute Tag_ARM_ISA_use, 0
	.eabi_attribute Tag_CPU_arch, 13 /* TAG_CPU_ARCH_V7_M */

.macro	raw_syscall n
	.ifne \n < 0x100
	mov	r7, #\n
	.else
	ldr	r7, =\n
	.endif
	swi	#0
.endm

.macro	fdpic_thunk ofs
	ldr	r3, [sp, #\ofs]
	ldmia	r2, {r2, r3}
	mov	r9, r3
	bx	r2
.endm

#else
	.arm
	.arch	armv4t
	.eabi_attribute Tag_THUMB_ISA_use, 0

.macro	raw_syscall n
	.ifne \n < 0x100
	mov	r7, #\n
	.else
	mov	r7, #(\n & 0xff)
	orr	r7, r7, #(\n & 0xff00)
	.endif
	svc	#(\n | __NR_OABI_SYSCALL_BASE)
.endm

.macro	fdpic_thunk ofs
	ldr	r3, [sp, #\ofs]
	ldmia	r3, {r3, r9}
	bx	r3
.endm

#endif

.macro	FUNC name
	.globl	\name
	.type	\name, %function
#ifdef __thumb__
	.thumb_func
#endif
\name:
.endm

.macro	ENDF name
	.size	\name, . - \name
.endm

/*
 * We must save/restore r7 for the EABI syscall number.
 * While we're doing that, we might as well save LR to get a free return,
 * and a branch that is interworking back to ARMv5.
 */

.macro syscall n
	.cfi_startproc
	push	{r7, lr}
	.cfi_adjust_cfa_offset 8
	.cfi_offset r7, -8
	.cfi_offset lr, -4
	raw_syscall \n
	pop	{r7, pc}
	.cfi_endproc
.endm

FUNC __vdso_clock_gettime
	syscall	__NR_clock_gettime
ENDF __vdso_clock_gettime

FUNC __vdso_clock_gettime64
	syscall	__NR_clock_gettime64
ENDF __vdso_clock_gettime64

FUNC __vdso_clock_getres
	syscall	__NR_clock_getres
ENDF __vdso_clock_getres

FUNC __vdso_gettimeofday
	syscall	__NR_gettimeofday
ENDF __vdso_gettimeofday


/*
 * We, like the real kernel, use a table of sigreturn trampolines.
 * Unlike the real kernel, we do not attempt to pack this into as
 * few bytes as possible -- simply use 16 bytes per slot.
 *
 * Within each slot, use the exact same code sequence as the kernel,
 * lest we trip up someone doing code inspection.
 */

/* offsetof(struct sigframe, retcode[3]) */
#define SIGFRAME_RC3_OFFSET     756
#define RT_SIGFRAME_RC3_OFFSET  884

.macro	slot n
	.balign	16
	.org	sigreturn_codes + 16 * \n
.endm

/*
 * Start the unwind info at least one instruction before the signal
 * trampoline, because the unwinder will assume we are returning
 * after a call site.
 */
	.cfi_startproc simple
	.cfi_signal_frame
	.cfi_return_column 15

	.cfi_def_cfa	sp, 32 + 64
	.cfi_offset	r0, -16 * 4
	.cfi_offset	r1, -15 * 4
	.cfi_offset	r2, -14 * 4
	.cfi_offset	r3, -13 * 4
	.cfi_offset	r4, -12 * 4
	.cfi_offset	r5, -11 * 4
	.cfi_offset	r6, -10 * 4
	.cfi_offset	r7, -9 * 4
	.cfi_offset	r8, -8 * 4
	.cfi_offset	r9, -7 * 4
	.cfi_offset	r10, -6 * 4
	.cfi_offset	r11, -5 * 4
	.cfi_offset	r12, -4 * 4
	.cfi_offset	r13, -3 * 4
	.cfi_offset	r14, -2 * 4
	.cfi_offset	r15, -1 * 4

	nop

	.balign	16
FUNC sigreturn_codes
	/* [EO]ABI sigreturn */
	slot	0
	raw_syscall __NR_sigreturn

	.cfi_def_cfa_offset 160 + 64

	/* [EO]ABI rt_sigreturn */
	slot	1
	raw_syscall __NR_rt_sigreturn

	.cfi_endproc

	.macro cfi_fdpic_pc ofs
	/*
	 * fd = *(r13 + ofs)
         * pc = *fd
	 *
	 * DW_CFA_expression lr (14), length (5),
	 *   DW_OP_breg13, ofs, DW_OP_deref, DW_OP_deref
         */
	.cfi_escape 0x10, 14, 5, 0x7d, (\ofs & 0x7f) + 0x80, (\ofs >> 7), 0x06, 0x06
	.endm

	.macro cfi_fdpic_r9 ofs
	/*
	 * fd = *(r13 + ofs)
         * r9 = *(fd + 4)
	 *
	 * DW_CFA_expression r9, length (7),
	 *   DW_OP_breg13, ofs, DW_OP_deref,
	 *   DW_OP_plus_uconst, 4, DW_OP_deref
         */
	.cfi_escape 0x10, 9, 7, 0x7d, (\ofs & 0x7f) + 0x80, (\ofs >> 7), 0x06, 0x23, 4, 0x06
	.endm

	/* FDPIC sigreturn */
	.cfi_startproc
	cfi_fdpic_pc SIGFRAME_RC3_OFFSET
	cfi_fdpic_r9 SIGFRAME_RC3_OFFSET

	slot	2
	fdpic_thunk SIGFRAME_RC3_OFFSET
	.cfi_endproc

	/* FDPIC rt_sigreturn */
	.cfi_startproc
	cfi_fdpic_pc RT_SIGFRAME_RC3_OFFSET
	cfi_fdpic_r9 RT_SIGFRAME_RC3_OFFSET

	slot	3
	fdpic_thunk RT_SIGFRAME_RC3_OFFSET
	.cfi_endproc

	.balign	16
ENDF sigreturn_codes
