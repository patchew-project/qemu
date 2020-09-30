
	.include "macros.inc"

.equ LIMIT,  0x1ff

	start
	test_name TIMER0
	;; enable TIMER0 interrupts
	sr	0x01,[control0]
	mov	r0, LIMIT
	sr	r0,[limit0]
	sr	0,[count0]
	;; Now wait for the counter to reach it's limit
	mov	r0,0
.loop1:
	lr	r0,[count0]
	brgt	r0,LIMIT,@.loop0
	;; Now enable PIC interrupts, we expect the pending interrupt
	;; to kick in.
	mov	r3, 0
	seti
.loop0:
	lr	r4,[count0]
	breq	r3, 1, @.pass
	brgt	r4,LIMIT,@.fail1
	j 	@.loop0
.pass:
	print	"[PASS]"
	j	@.end
.fail1:
	print   "[FAIL]"
	;; 	print_number r4
.end:
	print	" TIMER0: Overflow\n"
	end

	.align 4
	.global IRQ_Timer0
	.type IRQ_Timer0, @function
IRQ_Timer0:
	clri
	;; reset interrupts
	sr	0x00,[control0]
	mov	r3, 1
	rtie
	end
