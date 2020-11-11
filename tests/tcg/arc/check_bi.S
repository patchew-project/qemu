	.include "macros.inc"

	start
	test_name BI
	mov_s	r0,0
.Lloop:
	bi	[r0]
	b	@.L1
	b	@.L2
	b	@.L3
	b	@.L4
	b	@.Lfail
	b	@.Lfail
	b	@.Lfail
.L1:
	add	r0,r0,1
	print	"[PASS] BI:jmp0\n"
	b	@.Lloop
.L2:
	add	r0,r0,1
	print	"[PASS] BI:jmp1\n"
	b	@.Lloop
.L3:
	add	r0,r0,1
	print	"[PASS] BI:jmp2\n"
	b	@.Lloop
.L4:
	print	"[PASS] BI\n"
	end
.Lfail:
	print	"[FAIL] BI\n"
	end
