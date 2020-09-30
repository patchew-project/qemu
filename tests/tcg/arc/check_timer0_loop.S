	.include "macros.inc"

	start
	test_name TIMER0
	;; enable TIMER0 interrupts
	sr	0x01,[control0]
	mov	r0, 0x1fffff
	sr	r0,[limit0]
	sr	0,[count0]
	;; Now wait for the counter to reach it's limit
	mov	r0,0
.loop1:
	lr	r0,[control0]
	bbit0	r0,3,@.loop1
	;; Now enable PIC interrupts, we expect the pending interrupt
	;; to kick in.
	mov	r3, 0
	seti
.loop0:
	breq	r3, 0, @.loop0
	print	"The end\n"
	end

	.align 4
	.global IRQ_Timer0
	.type IRQ_Timer0, @function
IRQ_Timer0:
	clri
	;; reset interrupts
	sr	0x00,[control0]
	print "Pass IRQ\n"
	mov	r3, 1
	rtie
	end
