.include "macros.inc"

	start

	test_name ADD_1
	mov	r2,0x10ff01ff
	mov	r3,0x10010001
	add	r2,r2,r3
	check_r2	0x21000200

	end
