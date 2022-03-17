/*
 * Minimal Nios2 system boot code.
 * Copyright Linaro Ltd 2022
 */

#include "semicall.h"

        .text
	.set	noat

_start:
	/* Linker script defines stack at end of ram. */
	movia	sp, __stack

	/* Install trampoline to _fast_tlb_miss at hardcoded vector. */
	movia	r4, 0xc0000100
	movia	r5, _ftm_tramp
	movi	r6, .L__ftm_end - _ftm_tramp
	call	memcpy

	/* Zero the bss to satisfy C. */
	movia	r4, __bss_start
	movia	r6, __bss_end
	sub	r6, r6, r4
	movi	r5, 0
	call	memset

	/* Test! */
	call	main

	/* Exit with main's return value. */
	movi	r4, HOSTED_EXIT
	mov	r5, r2
	semihosting_call

	.globl	_start
	.type	_start, @function
	.size	_start, . - _start

_ftm_tramp:
	movia	et, _fast_tlb_miss
	jmp	et
.L__ftm_end:

	.type	_ftm_tramp, @function
	.size	_ftm_tramp, . - _ftm_tramp
