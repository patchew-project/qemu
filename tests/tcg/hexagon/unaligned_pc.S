test:
	allocframe(#0x8)
	r0 = #0xffffffff
	framekey = r0
	dealloc_return

.global _start
_start:
	call test
	jump pass
