#*****************************************************************************
# prefetch
#-----------------------------------------------------------------------------
#
# This test verifies that prefetch works as expected
#

#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN

  # testing the decoding
  TEST_CASE( 2, r0, 0x00000000, "prefetch:2", prefetch    [0x12]`      mov r0, 0x0)
  TEST_CASE( 3, r0, 0x00000000, "prefetch:3", prefetchw   [0x12]`      mov r0, 0x0)
  TEST_CASE( 4, r0, 0x00000000, "prefetch:4", prefetchw   [r1, r2]`    mov r0, 0x0)
  TEST_CASE( 5, r0, 0x00000000, "prefetch:5", prefetchw   [0x12, 0x1]` mov r0, 0x0)
  TEST_CASE( 6, r0, 0x00000000, "prefetch:6", prefetch    [r1, r2]`    mov r0, 0x0)
  TEST_CASE( 7, r0, 0x00000000, "prefetch:7", prefetch    [0x12, 0x1]` mov r0, 0x0)

	mov	r13, @tdat
	TEST_CASE( 8, r0, 0x00000004, "prefetch:8", prefetch    [r13]`       ld r0,[r13])
	TEST_CASE( 9, r0, 0x40000000, "prefetch:9", prefetch.aw [r13,4]`     ld r0,[r13])
	TEST_CASE(10, r0, 0x40400000, "prefetch:10", prefetch.ab [r13,4]`     ld r0,[r13])

ARCTEST_END
#  TEST_DATA

tdat:
.word 0x00000004
.word 0x40000000
.word 0x40400000
.word 0xc0800000
.word 0xdeadbeef
.word 0xcafebabe
.word 0xabad1dea
.word 0x1337d00d
