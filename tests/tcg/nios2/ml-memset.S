/*
 * Minimal Nios2 system minilib code: memset
 * Copyright Linaro Ltd 2022
 */

	.set	noat

        .text
	.global	memset
	.type	memset, @function

#define dst	r4
#define val	r5
#define len	r6

memset:
	/* Store return value right away, per API */
	mov	r2, dst

	/* Check for small blocks; fall back to bytewise. */
	movi	r3, 8
	bltu	len, r3, .L_test1

	/* Replicate the byte across the word. */
	andi	val, val, 0xff
	slli	at, val, 8
	or	val, val, at
	slli	at, val, 16
	or	val, val, at

	/* Check for destination alignment; realign if needed. */
	andi	at, dst, 3
	bne	at, zero, .L_align

	/* Set blocks of 8. */

.L_loop8:
	stw	val, 0(dst)
	stw	val, 4(dst)
	addi	dst, dst, 8
	subi	len, len, 8
	bgeu	len, r3, .L_loop8

	/* Set final aligned block of 4. */

.L_test4:
	movi	at, 4
	bltu	len, at, .L_test1

	stw	r8, 0(dst)
	addi	dst, dst, 4
	subi	len, len, 4
	stw	r8, -4(dst)

	/* Set single bytes to finish. */

.L_test1:
	beq	len, zero, .L_done

.L_loop1:
	stb	r8, 0(dst)
	addi	dst, dst, 1
	subi	len, len, 1
	bne	len, zero, .L_loop1

.L_done:
	ret

	/* Realign for a large block, len >= 8. */
.L_align:
	andi	at, dst, 1
	beq	at, zero, 2f

	stb	val, 0(dst)
	addi	dst, dst, 1
	subi	len, len, 1

2:	andi	at, dst, 2
	beq	at, zero, 4f

	sth	val, 0(dst)
	addi	dst, dst, 2
	subi	len, len, 2

4:	bgeu	len, r3, .L_loop8
	br	.L_test4

	.size	memset, . - memset
