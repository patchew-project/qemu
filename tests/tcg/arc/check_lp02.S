.include "macros.inc"

	start
	mov	r3,0
	mov	r2, 0x2e10
	mov.f	lp_count,0x10
	lpne	2f
	st.ab	r3,[r2,4]
2:
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop
	mov	r2,0x1000
	mov_s	r3,0xa
        and.f	lp_count,r3, 0x1f
        lpnz	2f
        add 	r2,r2,r2
2:      # end single insn loop

	print	"[PASS] LP01\n"
	end
