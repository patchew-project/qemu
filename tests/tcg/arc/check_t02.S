	.include "macros.inc"
	start
	test_name PREDICATE_1
	mov	r2,2
	lsr.f	r2,r2
	mov.nc	r2,1
	mov.cs	r2,-1	# Should not execute
	check_r2	0x01
	end
