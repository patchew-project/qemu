	.include "macros.inc"

	start
	test_name LOOP_1
	mov	r2, 4
.L1:
	sub_s	r2,r2,1
	tst_s	r2,r2
	bne	@.L1
	check_r2	0x0

	end
