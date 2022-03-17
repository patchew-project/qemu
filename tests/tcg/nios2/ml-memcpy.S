/*
 * Minimal Nios2 system minilib code: memcpy
 * Copyright Linaro Ltd 2022
 */

	.set	noat

        .text
	.global	memcpy
	.type	memcpy, @function

#define dst	r4
#define src	r5
#define len	r6

memcpy:
	/* Store return value right away, per API */
	mov	r2, dst

	/* Check for both dst and src aligned. */
	or	at, dst, src
	andi	at, at, 3
	bne	at, zero, .L_test1

	/* Copy blocks of 8. */

	movi	at, 8
	bltu	len, at, .L_test4

.L_loop8:
	ldw	r8, 0(src)
	ldw	r9, 4(src)
	addi	src, src, 8
	addi	dst, dst, 8
	subi	len, len, 8
	stw	r8, -8(dst)
	stw	r9, -4(dst)
	bgeu	len, at, .L_loop8

	/* Copy final aligned block of 4. */

.L_test4:
	movi	at, 4
	bltu	len, at, .L_test1

	ldw	r8, 0(src)
	addi	src, src, 4
	addi	dst, dst, 4
	subi	len, len, 4
	stw	r8, -4(dst)

	/* Copy single bytes to finish. */

.L_test1:
	beq	len, zero, .L_done

.L_loop1:
	ldb	r8, 0(src)
	addi	src, src, 1
	addi	dst, dst, 1
	subi	len, len, 1
	stb	r8, -1(dst)
	bne	len, zero, .L_loop1

.L_done:
	ret

	.size	memcpy, . - memcpy
