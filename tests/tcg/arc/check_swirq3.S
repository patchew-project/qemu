	.include "macros.inc"

	start
;;; 	print "Check if an IRQ gets re-trigger while in ISR:"
	;; Set the stack somewhere
	mov	sp, 0x1000
	seti
	mov	r0,0
	;; Use IRQ18 for the test.
	sr	18, [AUX_IRQ_HINT]
	;; wait (sleep doesn't work as expected because all the irq
	;; are triggered BEFORE sleep is even fetch/executed.
.llocal00:
	breq	r0, 0, @.llocal00
	brlt	r0, 2, @.failMe
	print "[PASS] SW-IRQ3\n"
	end
.failMe:
	print "[PASS] SW-IRQ3\n"
	end

	/* The delay between writing to the AUX_IRQ_HINT register and
	the interrupt being taken is implementation specific.  Hence,
	we need to save/restore any clobber register by ISR.  */
	.align 4
	.global IRQ_18
	.type IRQ_18, @function
IRQ_18:
#define AUX_IRQ_SELECT  0x40b
#define AUX_IRQ_ENABLE  0x40c
	clri
	add	r0,r0,1
        mov     r1, AUX_IRQ_SELECT
        mov     r2, AUX_IRQ_ENABLE
	;; clean the IRQ
        sr      18, [r1]
        sr      0, [r2]
	sr	0, [AUX_IRQ_HINT]
	brgt	r0,1,@.extisr
	;; retrigger the irq
	sr	18, [AUX_IRQ_HINT]
        sr      18, [r1]
        sr      1, [r2]
	;; 	print " SW-IRQ 0,"
	rtie
.extisr:
	;; 	print " SW-IRQ 1,"
	rtie
	end
