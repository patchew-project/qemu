#*****************************************************************************
# ldst.S
#-----------------------------------------------------------------------------
#
# This test verifies that ld, ldb, ldw work as expected.
#

#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN

	TEST_CASE(2, r0, 0x40000000, "ld:2", ld r1,[@tdat]` ld r0,[r1,@tdat])
	TEST_CASE(3, r0, 0xbeef,     "ld:3", mov r1, 16` ldw r0,[r1, @tdat])
	TEST_CASE(4, r0, 0xbe,       "ld:4", mov r1, 20` ldb r0,[r1, @tdat])
	TEST_CASE(5, r0, 0xffffbeef, "ld:5", mov r1, 16` ldw.x r0,[r1, @tdat])
	TEST_CASE(6, r0, 0xffffffbe, "ld:6", mov r1, 20` ldb.x r0,[r1, @tdat])

	TEST_CASE(7, r0, 0xbeef,      "ld:7", mov r1, @tdat` ldw.as r0,[r1,8])
	TEST_CASE(8, r0, 0xcafebabe,  "ld:8", mov r1, @tdat` ld.as r0,[r1, 5])
	TEST_CASE(9, r0, 0xcafebabe,  "ld:9", mov r2, 5` mov r1, @tdat` ld_s.as r0,[r1, r2])
	TEST_CASE(10, r0, 0x40400000, "ld:10", ldd.as r0,[@tdat,2])
	TEST_CASE(11, r1, 0xc0800000, "ld:11", ldd.as r0,[@tdat,2])


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
