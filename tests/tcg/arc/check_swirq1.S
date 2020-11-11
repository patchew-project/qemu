	.include "macros.inc"

	start
	;; print	"Check normal IRQ functioning.\n"
	;; Set the stack somewhere
	mov	sp, 0x1000
	;; Use IRQ18 for the test, change to a level 1, irq so we can
	;; avoid firq.
	sr	18,[REG_IRQ_SELECT]
	sr	1,[irq_priority]
  set_interrupt_prio_level 1
	sr	16,[aux_irq_ctrl]
	;; Enable the interrupt system, and trigger the IRQ 18.
	seti
	sr	18, [aux_irq_hint]
	print "[PASS] IRQ:End\n"
	end

	/* The delay between writing to the AUX_IRQ_HINT register and
	the interrupt being taken is implementation specific.  Hence,
	we need to save/restore any clobber register by ISR.  */
	.align 4
	.global IRQ_18
	.type IRQ_18, @function
IRQ_18:
	clri
	sr	0, [aux_irq_hint]
	print "[PASS] IRQ:IRQ\n"
	rtie
	print "[FAIL] IRQ\n"
	end
