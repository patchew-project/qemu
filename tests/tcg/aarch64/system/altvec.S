/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Alternative AArch64 vector table. Can replace the vector table
 * in boot.S and record & ignore some exceptions in exception_log.
 * Maintains the semihosting error print and exit for most
 * exceptions.
 *
 * Copyright Linaro Ltd 2026
 *
 */

#define semihosting_call hlt 0xf000
#define SYS_WRITE0	0x04	/* string to debug channel */
#define SYS_EXIT	0x18
	.align	12

	.macro	ventry	label
	.align	7
	b	\label
	.endm

	.global alt_vector_table
alt_vector_table:
	/* Current EL with SP0.	 */
	ventry	curr_sp0_sync		/* Synchronous	*/
	ventry	curr_sp0_irq		/* Irq/vIRQ  */
	ventry	curr_sp0_fiq		/* Fiq/vFIQ  */
	ventry	curr_sp0_serror		/* SError/VSError  */

	/* Current EL with SPx.	 */
	ventry	curr_spx_sync		/* Synchronous	*/
	ventry	curr_spx_irq		/* IRQ/vIRQ  */
	ventry	curr_spx_fiq		/* FIQ/vFIQ  */
	ventry	curr_spx_serror		/* SError/VSError  */

	/* Lower EL using AArch64.  */
	ventry	lower_a64_sync		/* Synchronous	*/
	ventry	lower_a64_irq		/* IRQ/vIRQ  */
	ventry	lower_a64_fiq		/* FIQ/vFIQ  */
	ventry	lower_a64_serror	/* SError/VSError  */

	/* Lower EL using AArch32.  */
	ventry	lower_a32_sync		/* Synchronous	*/
	ventry	lower_a32_irq		/* IRQ/vIRQ  */
	ventry	lower_a32_fiq		/* FIQ/vFIQ  */
	ventry	lower_a32_serror	/* SError/VSError  */

	.text
	.align 4

curr_spx_sync:
	sub     sp, sp, #16
	stp     x0, x1, [sp, #0]
	mrs     x0, ESR_EL3
	lsr 	x0, x0, #26
	and 	x0, x0, #0x3f
	cmp 	x0, #37
	beq 	data_fault
	cmp 	x0, #30
	beq 	gpc_fault
	b       generic_exception

data_fault:
	mrs     x0, FAR_EL3
	adrp    x1, exception_log
	str     x0, [x1]
	ldr     x0, =0x1001
	str     x0, [x1, #8]
	b       skip_return
gpc_fault:
	mrs     x0, FAR_EL3
	adrp    x1, exception_log
	str     x0, [x1]
	ldr     x0, =0x1002
	str     x0, [x1, #8]
	/* Fall through */
skip_return:
	mrs     x0, ELR_EL3
	add     x0, x0, #4 /* Skip faulting instruction */
	msr     ELR_EL3, x0
	ldp     x0, x1, [sp, #0]
	add     sp, sp, #16
	eret
curr_sp0_sync:
curr_sp0_irq:
curr_sp0_fiq:
curr_sp0_serror:
curr_spx_irq:
curr_spx_fiq:
curr_spx_serror:
lower_a64_sync:
lower_a64_irq:
lower_a64_fiq:
lower_a64_serror:
lower_a32_sync:
lower_a32_irq:
lower_a32_fiq:
lower_a32_serror:
generic_exception:
	adr	x1, .unexp_excp
exit_msg:
	mov	x0, SYS_WRITE0
	semihosting_call
	mov	x0, 1 /* EXIT_FAILURE */
	bl 	_exit
	/* never returns */
print_debug_msg:
	mov	x0, SYS_WRITE0
	semihosting_call
	ret
_exit:
	mov    x1, x0
	ldr    x0, =0x20026 /* ADP_Stopped_ApplicationExit */
	stp    x0, x1, [sp, #-16]!
	mov    x1, sp
	mov    x0, SYS_EXIT
	semihosting_call


	.section .rodata
.unexp_excp:
	.string "Unexpected exception (via alternative handler).\n"

	.data
	.align 8
	.global exception_log
exception_log:
	.space 128, 0

