	.include "macros.inc"

	start
	test_name TIMER0_RETRIG
	;; enable TIMER0 interrupts
	sr	0x01,[control0]
	mov	r0, 0x1fffff
	sr	r0,[limit0]
	sr	0,[count0]
	;; Now wait for the counter to reach it's limit
	mov	r0,0
	seti
.loop0:
	brlt	r3, 2, @.loop0
	print	"[PASS] TIMER0: Re-trigger\n"
	end

	.align 4
	.global IRQ_Timer0
	.type IRQ_Timer0, @function
IRQ_Timer0:
	clri
	;; reset interrupts & enable IRQ
	sr	0x01,[control0]
	;; The timer needs to continue counting, and we expect a new
	;; interrupt soon.
	add	r3, r3, 1
	rtie
	end
