	.include "macros.inc"

	start
	test_name TIMER0vsTIMER1
	;; enable TIMER0 interrupts
	sr	0x01,[control0]
	mov	r0, 0x1ffff
	sr	r0,[limit0]
	sr	0,[count0]

	;; enable TIMER1 interrupts
	sr	0x01,[control1]
	mov	r0, 0x3fffe	;Twice slower
	sr	r0,[limit1]
	sr	0,[count1]
	mov	r4,0
	mov	r5,0
	mov	sp,0x1000
	seti
	mov	r3, 0
.loop:
	sleep
	add	r3,r3,1
	brne	r3,10,@.loop
	clri
	stb.ab	0,[sp,1]
	mov	r0,r4
.L02:
	rem	r2,r0,10
	add	r2,r2,0x30
	stb.ab	r2,[sp,1]
	div.f	r0,r0,10
	bne	@.L02
.L03:
	ld.aw	r2,[sp,-1]
	breq	r2,0,@.L04
	;; 	stb	r2,[OUTPUT_DEVICE]
	brne	r2,0,@.L03
.L04:

	;; print	">>>"
	stb.ab	0,[sp,1]
	mov	r0,r5
.L12:
	rem	r2,r0,10
	add	r2,r2,0x30
	stb.ab	r2,[sp,1]
	div.f	r0,r0,10
	bne	@.L12
.L13:
	ld.aw	r2,[sp,-1]
	breq	r2,0,@.L14
	;; 	stb	r2,[OUTPUT_DEVICE]
	brne	r2,0,@.L13
.L14:
	breq	r5, 0, @.failMe
	brgt	r4,r5, @.passMe
.failMe:
	print	"[FAIL] "
	b	1f
.passMe:
	print	"[PASS] "
1:
	printl	r30
	end

	.align 4
	.global IRQ_Timer0
	.type IRQ_Timer0, @function
IRQ_Timer0:
	clri
	;; reset interrupts
	sr	0x01,[control0]
	sr	0,[count0]
	add	r4,r4,1
	rtie

	.global IRQ_Timer1
	.type IRQ_Timer1, @function
IRQ_Timer1:
	clri
	;; reset interrupts
	sr	0x01,[control1]
	sr	0,[count1]
	add	r5,r5,1
	rtie
	end
