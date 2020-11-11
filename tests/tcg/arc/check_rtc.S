	.include "macros.inc"

;;; Simple RTC test, read RTC value if it exists, spend some time, and
;;; re-read it. Fail if the value is the same..data
test_nr:
  .word 0x0

	start
	test_name RTC
	lr	r0,[timer_build]
	and.f	0,r0,0x400
	beq	@.lfail
	sr	1,[0x103]
	lr	r2,[0x104]
.loop:
	sub.f	r0,r0,1
	bnz	@.loop
	lr	r0,[0x104]
	breq	r0,r2,@.lfail
	print	"[PASS] "
	b @1f

.lfail:
	ld r0, [test_nr]
	;print_number r0
	print "[FAIL] "
1:
	printl r30
	end
