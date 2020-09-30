.include "macros.inc"

	start
	mov_s r2,0x28cc
	sub r3,0x28d8,r2
	mov  lp_count,0x00fffff0
	lpne bla
	st.ab r3,[r2,4]
	mov 0,0
bla:
	print	"[PASS] LP: simple\n"
	end
