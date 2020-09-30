.include "macros.inc"

	start

	test_name STLD_1
	st	-32,[0x10000]
	ld 	r2,[0x10000]
	check_r2	-32

	end
