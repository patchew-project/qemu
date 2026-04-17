/*
 * Minimal AArch64 system boot code.
 *
 * Copyright Linaro Ltd 2019
 *
 * Loosely based on the newlib/libgloss setup stubs. Using semihosting
 * for serial output and exit functions.
 */

/*
 * Semihosting interface on ARM AArch64
 * See "Semihosting for AArch32 and AArch64 Release 2.0" by ARM
 * w0 - semihosting call number
 * x1 - semihosting parameter
 */
#define semihosting_call hlt 0xf000
#define SYS_WRITEC	0x03	/* character to debug channel */
#define SYS_WRITE0	0x04	/* string to debug channel */
#define SYS_GET_CMDLINE 0x15	/* get command line */
#define SYS_EXIT	0x18

	.align	12

	.macro	ventry	label
	.align	7
	b	\label
	.endm

vector_table:
	/* Current EL with SP0.	 */
	ventry	curr_sp0_sync		/* Synchronous	*/
	ventry	curr_sp0_irq		/* Irq/vIRQ  */
	ventry	curr_sp0_fiq		/* Fiq/vFIQ  */
	ventry	curr_sp0_serror		/* SError/VSError  */

	/* Current EL with SPx.	 */
	ventry	curr_spx_sync		/* Synchronous	*/
	ventry	curr_spx_irq		/* Basic handler for IRQ/vIRQ */
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

	/* Apart from curr_spx_irq we error out with an unexpected exception */
curr_sp0_sync:
curr_sp0_irq:
curr_sp0_fiq:
curr_sp0_serror:
curr_spx_sync:
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
	adr	x1, .unexp_excp
exit_msg:
	mov	x0, SYS_WRITE0
	semihosting_call
	mov	x0, 1 /* EXIT_FAILURE */
	bl 	_exit
	/* never returns */

	.section .rodata
.unexp_excp:
	.string "Unexpected exception.\n"
.high_el_msg:
	.string "Started in lower EL than requested.\n"
.unexp_el0:
	.string "Started in invalid EL.\n"

	.align 8
.get_cmd:
	.quad	cmdline
	.quad	128

	.text
	.align 4
	.global __start
__start:
	/*
         * Initialise the stack for whatever EL we are in before
	 * anything else, we need it to be able to _exit cleanly.
	 * It's smaller than the stack we pass to the C code but we
	 * don't need much.
	 */
	adrp	x0, system_stack_end
	add	x0, x0, :lo12:system_stack_end
	mov	sp, x0

	/*
	 * The test can set the semihosting command line to the target
	 * EL needed for the test. However if no semihosting args are set we will
	 * end up with -kernel/-append data (see semihosting_arg_fallback).
	 * Keep the normalised target in w11.
	 */
	mov	x0, SYS_GET_CMDLINE
	adr	x1, .get_cmd
	semihosting_call
	adrp	x10, cmdline
	add	x10, x10, :lo12:cmdline
	ldrb	w11, [x10]

	/* sanity check, normalise char to EL, clamp to 1 if outside range */
	subs w11, w11, #'0'
	b.lt el_default
	cmp  w11, #3
	b.gt el_default
	b 1f

el_high:
	adr	x1, .high_el_msg
	b	exit_msg

el_default:
	mov	w11, #1

1:
	/* Determine current Exception Level */
	mrs	x0, CurrentEL
	lsr	x0, x0, #2	  /* CurrentEL[3:2] contains the current EL */

	/* Are we already in a lower EL than we want? */
	cmp	w11, w0
	bgt	el_high

	/* Branch based on current EL */
	cmp	x0, #3
	b.eq	setup_el3
	cmp	x0, #2
	b.eq	setup_el2
	cmp	x0, #1
	b.eq	at_testel	     /* Already at EL1, skip transition */

	/* Should not be at EL0 - error out */
	adr 	x1, .unexp_el0
	b	exit_msg

setup_el3:
	/* Ensure we trap if we get anything wrong */
	adr	x0, vector_table
	msr	vbar_el3, x0

	/* Does the test want to be at EL3? */
	cmp	w11, #3
	beq	at_testel

	/* Configure EL3 to for lower states (EL2 or EL1) */
	mrs	x0, scr_el3
	orr	x0, x0, #(1 << 10)    /* RW = 1: EL2/EL1 execution state is AArch64 */
	orr	x0, x0, #(1 << 0)     /* NS = 1: Non-secure state */
	msr	scr_el3, x0

	/*
	 * We need to check if EL2 is actually enabled via ID_AA64PFR0_EL1,
	 * otherwise we should just jump straight to EL1.
	 */
	mrs	x0, id_aa64pfr0_el1
	ubfx	x0, x0, #8, #4	      /* Extract EL2 field (bits 11:8) */
	cbz	x0, el2_not_present   /* If field is 0 no EL2 */


	/* Prepare SPSR for exception return to EL2 */
	mov	x0, #0x3c9	      /* DAIF bits and EL2h mode (9) */
	msr	spsr_el3, x0

	/* Set EL2 entry point */
	adr	x0, setup_el2
	msr	elr_el3, x0

	/* Return to EL2 */
	eret

el2_not_present:
	/* Initialize SCTLR_EL1 with reset value */
	msr	sctlr_el1, xzr

	/* Set EL1 entry point */
	adr	x0, at_testel
	msr	elr_el3, x0

	/* Prepare SPSR for exception return to EL1h with interrupts masked */
	mov	x0, #0x3c5	      /* DAIF bits and EL1h mode (5) */
	msr	spsr_el3, x0

	isb			      /* Synchronization barrier */
	eret			      /* Jump to EL1 */

setup_el2:
	/* Ensure we trap if we get anything wrong */
	adr	x0, vector_table
	msr	vbar_el2, x0

	/* Does the test want to be at EL2? */
	cmp	w11, #2
	beq	at_testel

	/* Configure EL2 to allow transition to EL1 */
	mrs	x0, hcr_el2
	orr	x0, x0, #(1 << 31)    /* RW = 1: EL1 execution state is AArch64 */
	msr	hcr_el2, x0

	/* Initialize SCTLR_EL1 with reset value */
	msr	sctlr_el1, xzr

	/* Set EL1 entry point */
	adr	x0, at_testel
	msr	elr_el2, x0

	/* Prepare SPSR for exception return to EL1 */
	mov	x0, #(0x5 << 0)	      /* EL1h (SPx), with interrupts disabled */
	msr	spsr_el2, x0

	/* Return to EL1 */
	eret

	/*
	 * At the target EL for the test, usually EL1. Note we still
	 * set everything up as if we were at EL1.
	 */
at_testel:
	/* Installs a table of exception vectors to catch and handle all
	   exceptions by terminating the process with a diagnostic.  */
	adr	x0, vector_table
	msr	vbar_el1, x0

	/* Page table setup (identity mapping). */
	bl	setup_pgtables

	/*
	 * Enable FP/SVE registers. The standard C pre-amble will be
	 * saving these and A-profile compilers will use AdvSIMD
	 * registers unless we tell it not to.
	*/
	mrs	x0, cpacr_el1
	orr	x0, x0, #(3 << 20)
	orr	x0, x0, #(3 << 16)
	msr	cpacr_el1, x0

	/*
	 * Setup some stack space before we enter the test code.
	 * Assume everything except the return value is garbage when we
	 * return, we won't need it.
	 */
	adrp	x0, stack_end
	add	x0, x0, :lo12:stack_end
	mov	sp, x0
	bl	main

	/* pass return value to sys exit */
_exit:
	mov    x1, x0
	ldr    x0, =0x20026 /* ADP_Stopped_ApplicationExit */
	stp    x0, x1, [sp, #-16]!
	mov    x1, sp
	mov    x0, SYS_EXIT
	semihosting_call
	/* never returns */

	/*
	 * IRQ handler
	 */
	.global curr_spx_irq
curr_spx_irq:
	/* Minimal IRQ handler: just mask the timer and return */
	mrs	x0, cntv_ctl_el0
	orr	x0, x0, #2		/* IMASK=1 */
	msr	cntv_ctl_el0, x0
	eret

	/*
	 * Helper Functions
	*/

	/* Output a single character to serial port */
	.global __sys_outc
__sys_outc:
	stp x0, x1, [sp, #-16]!
	/* pass address of c on stack */
	mov x1, sp
	mov x0, SYS_WRITEC
	semihosting_call
	ldp x0, x1, [sp], #16
	ret

	.data

	.align 8
cmdline:
	.space 128, 0

	.align	12
system_stack:
	.space 4096, 0
system_stack_end:

stack:
	.space 65536, 0
stack_end:
