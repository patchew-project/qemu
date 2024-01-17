/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Original implementation written by Andy Polyakov, @dot-asm.
 * This is an adaptation of the original code for kernel use.
 *
 * Copyright (C) 2006-2019 CRYPTOGAMS by <appro@openssl.org>. All Rights Reserved.
 *
 * For qemu testing, drop <asm/vx-insn-asm.h> and assume assembler support.
 */

#define SP	%r15
#define FRAME	(16 * 8 + 4 * 8)

	.data
	.balign	32

sigma:
	.long	0x61707865,0x3320646e,0x79622d32,0x6b206574	# endian-neutral
	.long	1,0,0,0
	.long	2,0,0,0
	.long	3,0,0,0
	.long	0x03020100,0x07060504,0x0b0a0908,0x0f0e0d0c	# byte swap

	.long	0,1,2,3
	.long	0x61707865,0x61707865,0x61707865,0x61707865	# smashed sigma
	.long	0x3320646e,0x3320646e,0x3320646e,0x3320646e
	.long	0x79622d32,0x79622d32,0x79622d32,0x79622d32
	.long	0x6b206574,0x6b206574,0x6b206574,0x6b206574

	.type	sigma, @object
	.size	sigma, . - sigma

	.previous

	.text

#############################################################################
# void chacha20_vx_4x(u8 *out, counst u8 *inp, size_t len,
#		      counst u32 *key, const u32 *counter)

#define	OUT		%r2
#define	INP		%r3
#define	LEN		%r4
#define	KEY		%r5
#define	COUNTER		%r6

#define BEPERM		%v31
#define CTR		%v26

#define K0		%v16
#define K1		%v17
#define K2		%v18
#define K3		%v19

#define XA0		%v0
#define XA1		%v1
#define XA2		%v2
#define XA3		%v3

#define XB0		%v4
#define XB1		%v5
#define XB2		%v6
#define XB3		%v7

#define XC0		%v8
#define XC1		%v9
#define XC2		%v10
#define XC3		%v11

#define XD0		%v12
#define XD1		%v13
#define XD2		%v14
#define XD3		%v15

#define XT0		%v27
#define XT1		%v28
#define XT2		%v29
#define XT3		%v30

	.balign	32
chacha20_vx_4x:
	stmg	%r6,%r7,6*8(SP)

	larl	%r7,sigma
	lhi	%r0,10
	lhi	%r1,0

	vl	K0,0(%r7)		# load sigma
	vl	K1,0(KEY)		# load key
	vl	K2,16(KEY)
	vl	K3,0(COUNTER)		# load counter

	vl	BEPERM,0x40(%r7)
	vl	CTR,0x50(%r7)

	vlm	XA0,XA3,0x60(%r7),4	# load [smashed] sigma

	vrepf	XB0,K1,0		# smash the key
	vrepf	XB1,K1,1
	vrepf	XB2,K1,2
	vrepf	XB3,K1,3

	vrepf	XD0,K3,0
	vrepf	XD1,K3,1
	vrepf	XD2,K3,2
	vrepf	XD3,K3,3
	vaf	XD0,XD0,CTR

	vrepf	XC0,K2,0
	vrepf	XC1,K2,1
	vrepf	XC2,K2,2
	vrepf	XC3,K2,3

.Loop_4x:
	vaf	XA0,XA0,XB0
	vx	XD0,XD0,XA0
	verllf	XD0,XD0,16

	vaf	XA1,XA1,XB1
	vx	XD1,XD1,XA1
	verllf	XD1,XD1,16

	vaf	XA2,XA2,XB2
	vx	XD2,XD2,XA2
	verllf	XD2,XD2,16

	vaf	XA3,XA3,XB3
	vx	XD3,XD3,XA3
	verllf	XD3,XD3,16

	vaf	XC0,XC0,XD0
	vx	XB0,XB0,XC0
	verllf	XB0,XB0,12

	vaf	XC1,XC1,XD1
	vx	XB1,XB1,XC1
	verllf	XB1,XB1,12

	vaf	XC2,XC2,XD2
	vx	XB2,XB2,XC2
	verllf	XB2,XB2,12

	vaf	XC3,XC3,XD3
	vx	XB3,XB3,XC3
	verllf	XB3,XB3,12

	vaf	XA0,XA0,XB0
	vx	XD0,XD0,XA0
	verllf	XD0,XD0,8

	vaf	XA1,XA1,XB1
	vx	XD1,XD1,XA1
	verllf	XD1,XD1,8

	vaf	XA2,XA2,XB2
	vx	XD2,XD2,XA2
	verllf	XD2,XD2,8

	vaf	XA3,XA3,XB3
	vx	XD3,XD3,XA3
	verllf	XD3,XD3,8

	vaf	XC0,XC0,XD0
	vx	XB0,XB0,XC0
	verllf	XB0,XB0,7

	vaf	XC1,XC1,XD1
	vx	XB1,XB1,XC1
	verllf	XB1,XB1,7

	vaf	XC2,XC2,XD2
	vx	XB2,XB2,XC2
	verllf	XB2,XB2,7

	vaf	XC3,XC3,XD3
	vx	XB3,XB3,XC3
	verllf	XB3,XB3,7

	vaf	XA0,XA0,XB1
	vx	XD3,XD3,XA0
	verllf	XD3,XD3,16

	vaf	XA1,XA1,XB2
	vx	XD0,XD0,XA1
	verllf	XD0,XD0,16

	vaf	XA2,XA2,XB3
	vx	XD1,XD1,XA2
	verllf	XD1,XD1,16

	vaf	XA3,XA3,XB0
	vx	XD2,XD2,XA3
	verllf	XD2,XD2,16

	vaf	XC2,XC2,XD3
	vx	XB1,XB1,XC2
	verllf	XB1,XB1,12

	vaf	XC3,XC3,XD0
	vx	XB2,XB2,XC3
	verllf	XB2,XB2,12

	vaf	XC0,XC0,XD1
	vx	XB3,XB3,XC0
	verllf	XB3,XB3,12

	vaf	XC1,XC1,XD2
	vx	XB0,XB0,XC1
	verllf	XB0,XB0,12

	vaf	XA0,XA0,XB1
	vx	XD3,XD3,XA0
	verllf	XD3,XD3,8

	vaf	XA1,XA1,XB2
	vx	XD0,XD0,XA1
	verllf	XD0,XD0,8

	vaf	XA2,XA2,XB3
	vx	XD1,XD1,XA2
	verllf	XD1,XD1,8

	vaf	XA3,XA3,XB0
	vx	XD2,XD2,XA3
	verllf	XD2,XD2,8

	vaf	XC2,XC2,XD3
	vx	XB1,XB1,XC2
	verllf	XB1,XB1,7

	vaf	XC3,XC3,XD0
	vx	XB2,XB2,XC3
	verllf	XB2,XB2,7

	vaf	XC0,XC0,XD1
	vx	XB3,XB3,XC0
	verllf	XB3,XB3,7

	vaf	XC1,XC1,XD2
	vx	XB0,XB0,XC1
	verllf	XB0,XB0,7
	brct	%r0,.Loop_4x

	vaf	XD0,XD0,CTR

	vmrhf	XT0,XA0,XA1		# transpose data
	vmrhf	XT1,XA2,XA3
	vmrlf	XT2,XA0,XA1
	vmrlf	XT3,XA2,XA3
	vpdi	XA0,XT0,XT1,0b0000
	vpdi	XA1,XT0,XT1,0b0101
	vpdi	XA2,XT2,XT3,0b0000
	vpdi	XA3,XT2,XT3,0b0101

	vmrhf	XT0,XB0,XB1
	vmrhf	XT1,XB2,XB3
	vmrlf	XT2,XB0,XB1
	vmrlf	XT3,XB2,XB3
	vpdi	XB0,XT0,XT1,0b0000
	vpdi	XB1,XT0,XT1,0b0101
	vpdi	XB2,XT2,XT3,0b0000
	vpdi	XB3,XT2,XT3,0b0101

	vmrhf	XT0,XC0,XC1
	vmrhf	XT1,XC2,XC3
	vmrlf	XT2,XC0,XC1
	vmrlf	XT3,XC2,XC3
	vpdi	XC0,XT0,XT1,0b0000
	vpdi	XC1,XT0,XT1,0b0101
	vpdi	XC2,XT2,XT3,0b0000
	vpdi	XC3,XT2,XT3,0b0101

	vmrhf	XT0,XD0,XD1
	vmrhf	XT1,XD2,XD3
	vmrlf	XT2,XD0,XD1
	vmrlf	XT3,XD2,XD3
	vpdi	XD0,XT0,XT1,0b0000
	vpdi	XD1,XT0,XT1,0b0101
	vpdi	XD2,XT2,XT3,0b0000
	vpdi	XD3,XT2,XT3,0b0101

	vaf	XA0,XA0,K0
	vaf	XB0,XB0,K1
	vaf	XC0,XC0,K2
	vaf	XD0,XD0,K3

	vperm	XA0,XA0,XA0,BEPERM
	vperm	XB0,XB0,XB0,BEPERM
	vperm	XC0,XC0,XC0,BEPERM
	vperm	XD0,XD0,XD0,BEPERM

	vlm	XT0,XT3,0(INP),0

	vx	XT0,XT0,XA0
	vx	XT1,XT1,XB0
	vx	XT2,XT2,XC0
	vx	XT3,XT3,XD0

	vstm	XT0,XT3,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	aghi	LEN,-0x40

	vaf	XA0,XA1,K0
	vaf	XB0,XB1,K1
	vaf	XC0,XC1,K2
	vaf	XD0,XD1,K3

	vperm	XA0,XA0,XA0,BEPERM
	vperm	XB0,XB0,XB0,BEPERM
	vperm	XC0,XC0,XC0,BEPERM
	vperm	XD0,XD0,XD0,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_4x

	vlm	XT0,XT3,0(INP),0

	vx	XT0,XT0,XA0
	vx	XT1,XT1,XB0
	vx	XT2,XT2,XC0
	vx	XT3,XT3,XD0

	vstm	XT0,XT3,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	aghi	LEN,-0x40
	je	.Ldone_4x

	vaf	XA0,XA2,K0
	vaf	XB0,XB2,K1
	vaf	XC0,XC2,K2
	vaf	XD0,XD2,K3

	vperm	XA0,XA0,XA0,BEPERM
	vperm	XB0,XB0,XB0,BEPERM
	vperm	XC0,XC0,XC0,BEPERM
	vperm	XD0,XD0,XD0,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_4x

	vlm	XT0,XT3,0(INP),0

	vx	XT0,XT0,XA0
	vx	XT1,XT1,XB0
	vx	XT2,XT2,XC0
	vx	XT3,XT3,XD0

	vstm	XT0,XT3,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	aghi	LEN,-0x40
	je	.Ldone_4x

	vaf	XA0,XA3,K0
	vaf	XB0,XB3,K1
	vaf	XC0,XC3,K2
	vaf	XD0,XD3,K3

	vperm	XA0,XA0,XA0,BEPERM
	vperm	XB0,XB0,XB0,BEPERM
	vperm	XC0,XC0,XC0,BEPERM
	vperm	XD0,XD0,XD0,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_4x

	vlm	XT0,XT3,0(INP),0

	vx	XT0,XT0,XA0
	vx	XT1,XT1,XB0
	vx	XT2,XT2,XC0
	vx	XT3,XT3,XD0

	vstm	XT0,XT3,0(OUT),0

.Ldone_4x:
	lmg	%r6,%r7,6*8(SP)
	br	%r14

.Ltail_4x: 
	vlr	XT0,XC0
	vlr	XT1,XD0

	vst	XA0,8*8+0x00(SP)
	vst	XB0,8*8+0x10(SP)
	vst	XT0,8*8+0x20(SP)
	vst	XT1,8*8+0x30(SP)

	lghi	%r1,0

.Loop_tail_4x:
	llgc	%r5,0(%r1,INP)
	llgc	%r6,8*8(%r1,SP)
	xr	%r6,%r5
	stc	%r6,0(%r1,OUT)
	la	%r1,1(%r1)
	brct	LEN,.Loop_tail_4x

	lmg	%r6,%r7,6*8(SP)
	br	%r14

	.type	chacha20_vx_4x, @function
	.size	chacha20_vx_4x, . - chacha20_vx_4x

#undef	OUT
#undef	INP
#undef	LEN
#undef	KEY
#undef	COUNTER

#undef BEPERM

#undef K0
#undef K1
#undef K2
#undef K3


#############################################################################
# void chacha20_vx(u8 *out, counst u8 *inp, size_t len,
#		   counst u32 *key, const u32 *counter)

#define	OUT		%r2
#define	INP		%r3
#define	LEN		%r4
#define	KEY		%r5
#define	COUNTER		%r6

#define BEPERM		%v31

#define K0		%v27
#define K1		%v24
#define K2		%v25
#define K3		%v26

#define A0		%v0
#define B0		%v1
#define C0		%v2
#define D0		%v3

#define A1		%v4
#define B1		%v5
#define C1		%v6
#define D1		%v7

#define A2		%v8
#define B2		%v9
#define C2		%v10
#define D2		%v11

#define A3		%v12
#define B3		%v13
#define C3		%v14
#define D3		%v15

#define A4		%v16
#define B4		%v17
#define C4		%v18
#define D4		%v19

#define A5		%v20
#define B5		%v21
#define C5		%v22
#define D5		%v23

#define T0		%v27
#define T1		%v28
#define T2		%v29
#define T3		%v30

	.balign	32
chacha20_vx:
	clgfi	LEN,256
	jle	chacha20_vx_4x
	stmg	%r6,%r7,6*8(SP)

	lghi	%r1,-FRAME
	lgr	%r0,SP
	la	SP,0(%r1,SP)
	stg	%r0,0(SP)		# back-chain

	larl	%r7,sigma
	lhi	%r0,10

	vlm	K1,K2,0(KEY),0		# load key
	vl	K3,0(COUNTER)		# load counter

	vlm	K0,BEPERM,0(%r7),4	# load sigma, increments, ...

.Loop_outer_vx:
	vlr	A0,K0
	vlr	B0,K1
	vlr	A1,K0
	vlr	B1,K1
	vlr	A2,K0
	vlr	B2,K1
	vlr	A3,K0
	vlr	B3,K1
	vlr	A4,K0
	vlr	B4,K1
	vlr	A5,K0
	vlr	B5,K1

	vlr	D0,K3
	vaf	D1,K3,T1		# K[3]+1
	vaf	D2,K3,T2		# K[3]+2
	vaf	D3,K3,T3		# K[3]+3
	vaf	D4,D2,T2		# K[3]+4
	vaf	D5,D2,T3		# K[3]+5

	vlr	C0,K2
	vlr	C1,K2
	vlr	C2,K2
	vlr	C3,K2
	vlr	C4,K2
	vlr	C5,K2

	vlr	T1,D1
	vlr	T2,D2
	vlr	T3,D3

.Loop_vx:
	vaf	A0,A0,B0
	vaf	A1,A1,B1
	vaf	A2,A2,B2
	vaf	A3,A3,B3
	vaf	A4,A4,B4
	vaf	A5,A5,B5
	vx	D0,D0,A0
	vx	D1,D1,A1
	vx	D2,D2,A2
	vx	D3,D3,A3
	vx	D4,D4,A4
	vx	D5,D5,A5
	verllf	D0,D0,16
	verllf	D1,D1,16
	verllf	D2,D2,16
	verllf	D3,D3,16
	verllf	D4,D4,16
	verllf	D5,D5,16

	vaf	C0,C0,D0
	vaf	C1,C1,D1
	vaf	C2,C2,D2
	vaf	C3,C3,D3
	vaf	C4,C4,D4
	vaf	C5,C5,D5
	vx	B0,B0,C0
	vx	B1,B1,C1
	vx	B2,B2,C2
	vx	B3,B3,C3
	vx	B4,B4,C4
	vx	B5,B5,C5
	verllf	B0,B0,12
	verllf	B1,B1,12
	verllf	B2,B2,12
	verllf	B3,B3,12
	verllf	B4,B4,12
	verllf	B5,B5,12

	vaf	A0,A0,B0
	vaf	A1,A1,B1
	vaf	A2,A2,B2
	vaf	A3,A3,B3
	vaf	A4,A4,B4
	vaf	A5,A5,B5
	vx	D0,D0,A0
	vx	D1,D1,A1
	vx	D2,D2,A2
	vx	D3,D3,A3
	vx	D4,D4,A4
	vx	D5,D5,A5
	verllf	D0,D0,8
	verllf	D1,D1,8
	verllf	D2,D2,8
	verllf	D3,D3,8
	verllf	D4,D4,8
	verllf	D5,D5,8

	vaf	C0,C0,D0
	vaf	C1,C1,D1
	vaf	C2,C2,D2
	vaf	C3,C3,D3
	vaf	C4,C4,D4
	vaf	C5,C5,D5
	vx	B0,B0,C0
	vx	B1,B1,C1
	vx	B2,B2,C2
	vx	B3,B3,C3
	vx	B4,B4,C4
	vx	B5,B5,C5
	verllf	B0,B0,7
	verllf	B1,B1,7
	verllf	B2,B2,7
	verllf	B3,B3,7
	verllf	B4,B4,7
	verllf	B5,B5,7

	vsldb	C0,C0,C0,8
	vsldb	C1,C1,C1,8
	vsldb	C2,C2,C2,8
	vsldb	C3,C3,C3,8
	vsldb	C4,C4,C4,8
	vsldb	C5,C5,C5,8
	vsldb	B0,B0,B0,4
	vsldb	B1,B1,B1,4
	vsldb	B2,B2,B2,4
	vsldb	B3,B3,B3,4
	vsldb	B4,B4,B4,4
	vsldb	B5,B5,B5,4
	vsldb	D0,D0,D0,12
	vsldb	D1,D1,D1,12
	vsldb	D2,D2,D2,12
	vsldb	D3,D3,D3,12
	vsldb	D4,D4,D4,12
	vsldb	D5,D5,D5,12

	vaf	A0,A0,B0
	vaf	A1,A1,B1
	vaf	A2,A2,B2
	vaf	A3,A3,B3
	vaf	A4,A4,B4
	vaf	A5,A5,B5
	vx	D0,D0,A0
	vx	D1,D1,A1
	vx	D2,D2,A2
	vx	D3,D3,A3
	vx	D4,D4,A4
	vx	D5,D5,A5
	verllf	D0,D0,16
	verllf	D1,D1,16
	verllf	D2,D2,16
	verllf	D3,D3,16
	verllf	D4,D4,16
	verllf	D5,D5,16

	vaf	C0,C0,D0
	vaf	C1,C1,D1
	vaf	C2,C2,D2
	vaf	C3,C3,D3
	vaf	C4,C4,D4
	vaf	C5,C5,D5
	vx	B0,B0,C0
	vx	B1,B1,C1
	vx	B2,B2,C2
	vx	B3,B3,C3
	vx	B4,B4,C4
	vx	B5,B5,C5
	verllf	B0,B0,12
	verllf	B1,B1,12
	verllf	B2,B2,12
	verllf	B3,B3,12
	verllf	B4,B4,12
	verllf	B5,B5,12

	vaf	A0,A0,B0
	vaf	A1,A1,B1
	vaf	A2,A2,B2
	vaf	A3,A3,B3
	vaf	A4,A4,B4
	vaf	A5,A5,B5
	vx	D0,D0,A0
	vx	D1,D1,A1
	vx	D2,D2,A2
	vx	D3,D3,A3
	vx	D4,D4,A4
	vx	D5,D5,A5
	verllf	D0,D0,8
	verllf	D1,D1,8
	verllf	D2,D2,8
	verllf	D3,D3,8
	verllf	D4,D4,8
	verllf	D5,D5,8

	vaf	C0,C0,D0
	vaf	C1,C1,D1
	vaf	C2,C2,D2
	vaf	C3,C3,D3
	vaf	C4,C4,D4
	vaf	C5,C5,D5
	vx	B0,B0,C0
	vx	B1,B1,C1
	vx	B2,B2,C2
	vx	B3,B3,C3
	vx	B4,B4,C4
	vx	B5,B5,C5
	verllf	B0,B0,7
	verllf	B1,B1,7
	verllf	B2,B2,7
	verllf	B3,B3,7
	verllf	B4,B4,7
	verllf	B5,B5,7

	vsldb	C0,C0,C0,8
	vsldb	C1,C1,C1,8
	vsldb	C2,C2,C2,8
	vsldb	C3,C3,C3,8
	vsldb	C4,C4,C4,8
	vsldb	C5,C5,C5,8
	vsldb	B0,B0,B0,12
	vsldb	B1,B1,B1,12
	vsldb	B2,B2,B2,12
	vsldb	B3,B3,B3,12
	vsldb	B4,B4,B4,12
	vsldb	B5,B5,B5,12
	vsldb	D0,D0,D0,4
	vsldb	D1,D1,D1,4
	vsldb	D2,D2,D2,4
	vsldb	D3,D3,D3,4
	vsldb	D4,D4,D4,4
	vsldb	D5,D5,D5,4
	brct	%r0,.Loop_vx

	vaf	A0,A0,K0
	vaf	B0,B0,K1
	vaf	C0,C0,K2
	vaf	D0,D0,K3
	vaf	A1,A1,K0
	vaf	D1,D1,T1		# +K[3]+1

	vperm	A0,A0,A0,BEPERM
	vperm	B0,B0,B0,BEPERM
	vperm	C0,C0,C0,BEPERM
	vperm	D0,D0,D0,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_vx

	vaf	D2,D2,T2		# +K[3]+2
	vaf	D3,D3,T3		# +K[3]+3
	vlm	T0,T3,0(INP),0

	vx	A0,A0,T0
	vx	B0,B0,T1
	vx	C0,C0,T2
	vx	D0,D0,T3

	vlm	K0,T3,0(%r7),4		# re-load sigma and increments

	vstm	A0,D0,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	aghi	LEN,-0x40
	je	.Ldone_vx

	vaf	B1,B1,K1
	vaf	C1,C1,K2

	vperm	A0,A1,A1,BEPERM
	vperm	B0,B1,B1,BEPERM
	vperm	C0,C1,C1,BEPERM
	vperm	D0,D1,D1,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_vx

	vlm	A1,D1,0(INP),0

	vx	A0,A0,A1
	vx	B0,B0,B1
	vx	C0,C0,C1
	vx	D0,D0,D1

	vstm	A0,D0,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	aghi	LEN,-0x40
	je	.Ldone_vx

	vaf	A2,A2,K0
	vaf	B2,B2,K1
	vaf	C2,C2,K2

	vperm	A0,A2,A2,BEPERM
	vperm	B0,B2,B2,BEPERM
	vperm	C0,C2,C2,BEPERM
	vperm	D0,D2,D2,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_vx

	vlm	A1,D1,0(INP),0

	vx	A0,A0,A1
	vx	B0,B0,B1
	vx	C0,C0,C1
	vx	D0,D0,D1

	vstm	A0,D0,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	aghi	LEN,-0x40
	je	.Ldone_vx

	vaf	A3,A3,K0
	vaf	B3,B3,K1
	vaf	C3,C3,K2
	vaf	D2,K3,T3		# K[3]+3

	vperm	A0,A3,A3,BEPERM
	vperm	B0,B3,B3,BEPERM
	vperm	C0,C3,C3,BEPERM
	vperm	D0,D3,D3,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_vx

	vaf	D3,D2,T1		# K[3]+4
	VLM	A1,D1,0(INP),0

	vx	A0,A0,A1
	vx	B0,B0,B1
	vx	C0,C0,C1
	vx	D0,D0,D1

	vstm	A0,D0,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	aghi	LEN,-0x40
	je	.Ldone_vx

	vaf	A4,A4,K0
	vaf	B4,B4,K1
	vaf	C4,C4,K2
	vaf	D4,D4,D3		# +K[3]+4
	vaf	D3,D3,T1		# K[3]+5
	vaf	K3,D2,T3		# K[3]+=6

	vperm	A0,A4,A4,BEPERM
	vperm	B0,B4,B4,BEPERM
	vperm	C0,C4,C4,BEPERM
	vperm	D0,D4,D4,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_vx

	vlm	A1,D1,0(INP),0

	vx	A0,A0,A1
	vx	B0,B0,B1
	vx	C0,C0,C1
	vx	D0,D0,D1

	vstm	A0,D0,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	aghi	LEN,-0x40
	je	.Ldone_vx

	vaf	A5,A5,K0
	vaf	B5,B5,K1
	vaf	C5,C5,K2
	vaf	D5,D5,D3		# +K[3]+5

	vperm	A0,A5,A5,BEPERM
	vperm	B0,B5,B5,BEPERM
	vperm	C0,C5,C5,BEPERM
	vperm	D0,D5,D5,BEPERM

	clgfi	LEN,0x40
	jl	.Ltail_vx

	vlm	A1,D1,0(INP),0

	vx	A0,A0,A1
	vx	B0,B0,B1
	vx	C0,C0,C1
	vx	D0,D0,D1

	vstm	A0,D0,0(OUT),0

	la	INP,0x40(INP)
	la	OUT,0x40(OUT)
	lhi	%r0,10
	aghi	LEN,-0x40
	jne	.Loop_outer_vx

.Ldone_vx:
	lmg	%r6,%r7,FRAME+6*8(SP)
	la	SP,FRAME(SP)
	br	%r14

.Ltail_vx:
	vstm	A0,D0,8*8(SP),3
	lghi	%r1,0

.Loop_tail_vx:
	llgc	%r5,0(%r1,INP)
	llgc	%r6,8*8(%r1,SP)
	xr	%r6,%r5
	stc	%r6,0(%r1,OUT)
	la	%r1,1(%r1)
	brct	LEN,.Loop_tail_vx

	lmg	%r6,%r7,FRAME+6*8(SP)
	la	SP,FRAME(SP)
	br	%r14

	.type	chacha20_vx, @function
	.size	chacha20_vx, . - chacha20_vx
	.globl	chacha20_vx

.previous
.section .note.GNU-stack,"",%progbits
