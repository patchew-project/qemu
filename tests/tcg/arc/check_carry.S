#define ARCTEST_ARC32
#include "test_macros.h"

	ARCTEST_BEGIN

test_2:
	mov	r0, 10
	mov	r1, 12
	cmp	r0,r1		;Carry is set here
	mov.lo.f	0, 0x0
	mov.hs.f	0, 0x1

	bne	@fail

	ARCTEST_END
