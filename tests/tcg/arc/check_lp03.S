	.include "macros.inc"

	start
	test_name ZOLvsIRQ
	;; Program the Timer such that we get fast interrupts
	sr	0x01,[control0]
	sr	0x1ff,[limit0]
	sr	0,[count0]
	mov	r0,0
	mov	sp,0x1000
	;; enable global interrupts
	seti
	;; Make a short ZOL
	mov	lp_count,0x1ffff
	lp	1f
	nop
1:
	clri
	stb.ab	0,[sp,1]
2:
	rem	r2,r0,10
	add	r2,r2,0x30
	stb.ab	r2,[sp,1]
	div.f	r0,r0,10
	bne	2b
3:
	ld.aw	r2,[sp,-1]
	breq	r2,0,4f
	;; 	stb	r2,[OUTPUT_DEVICE]
	brne	r2,0,3b
4:
	print	"[PASS] "
	printl	r30
	end

	;; Timer ISR
	.align	4
	.global	IRQ_Timer0
	.type	IRQ_Timer0, @function
IRQ_Timer0:
	clri
	;; reset the pending interrupt and reneable it.
	sr	0x01,[control0]
	sr	0,[count0]
	add	r0,r0,1
	rtie
	print   "[FAIL] "
	printl  r30
	end
