.global _start
_start:
	loop0(loop, #2)
loop:
	r0 = #0x3
	sa0 = r0
	{ nop }:endloop0
	jump pass
