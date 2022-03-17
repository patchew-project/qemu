/*
 * Minimal Nios2 system boot code.
 *
 * Copyright Linaro Ltd 2022
 *
 * Using semihosting for serial output and exit functions.
 */

#include "semicall.h"

        .section .text.intr, "ax"
	.global	_interrupt
	.type	_interrupt, @function

_interrupt:
	rdctl	r5, exception		/* extract exception.CAUSE */
	srli	r5, r5, 2
	movi	r4, HOSTED_EXIT
	semihosting_call

	.size	_interrupt, . - _interrupt
