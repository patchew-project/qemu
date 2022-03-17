/*
 * Minimal Nios2 system boot code.
 *
 * Copyright Linaro Ltd 2022
 *
 * Using semihosting for serial output and exit functions.
 */

#include "semicall.h"

        .text
	.global	_fast_tlb_miss
	.type	_fast_tlb_miss, @function

_fast_tlb_miss:
	movi	r5, 32
	movi	r4, HOSTED_EXIT
	semihosting_call

	.size	_fast_tlb_miss, . - _fast_tlb_miss
