/*
 * Minimal Nios2 system minilib code: __sys_outc
 * Copyright Linaro Ltd 2022
 */

#include "semicall.h"

        .text
	.global	__sys_outc
	.type	__sys_outc, @function
	.set	noat

/*
 * void __sys_outc(char c);
 */
__sys_outc:
	subi	sp, sp, 16
	stb	r4, 0(sp)	/* buffer[0] = c */
	movi	at, 1
	stw	at, 4(sp)	/* STDOUT_FILENO */
	stw	sp, 8(sp)	/* buffer */
	stw	at, 12(sp)	/* len */

	movi	r4, HOSTED_WRITE
	addi	r5, sp, 4
	semihosting_call

	addi	sp, sp, 16
	ret

	.size	__sys_outc, . - __sys_outc
