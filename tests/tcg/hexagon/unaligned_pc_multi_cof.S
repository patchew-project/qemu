.org 0x3
test:
	nop
	jumpr r31

.global _start
_start:
	p0 = cmp.eq(r0, r0)
	{
		if (p0) jump test
		jump pass
	}
	jump pass
