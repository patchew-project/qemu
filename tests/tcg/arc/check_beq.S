.include "macros.inc"

	start
	test_name BEQ_1
	mov.f	r2,0
	beq	1f
	check_r2 0x01
1:
	beq.d	2f
	mov	r2,0x01
	check_r2 0x00
2:
	check_r2 0x01
	end
