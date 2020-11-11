	.include "macros.inc"

	start
	test_name BIH
	mov_s	r0,0
.Lloop:
	bih	[r0]
	b_s	@.L1
	b_s	@.L2
	b_s	@.L3
	b_s	@.L4
	b_s	@.Lfail
	b_s	@.Lfail
	b_s	@.Lfail
.L1:
	add	r0,r0,1
	b	@.Lloop
.L2:
	add	r0,r0,1
	b	@.Lloop
.L3:
	add	r0,r0,1
	b	@.Lloop
.L4:
	print	"[PASS] BIH\n"
	end
.Lfail:
	print	"[FAIL] BIH\n"
	end
