#define ARCTEST_ARC32

#*****************************************************************************
# flags.S
#-----------------------------------------------------------------------------
#
# Test or instruction.
#

#include "test_macros.h"

ARCTEST_BEGIN

	TEST_2OP_CARRY (2, sub, 0, 0x00000000, 0x0000000) ;
	TEST_2OP_CARRY (3, sub, 1, 0x00000000, 0x0000001) ;
	TEST_2OP_ZERO (4, sub, 0, 0x00000001, 0x0000000) ;
	TEST_2OP_ZERO (5, sub, 1, 0x00000001, 0x0000001) ;
	TEST_2OP_NEGATIVE (6, sub, 0, 0x00000000, 0x00000000) ;
	TEST_2OP_NEGATIVE (7, sub, 1, 0x00000000, 0x00000001) ;
	TEST_2OP_OVERFLOW (8, sub, 0, 0x00000000, 0x00000000) ;
	TEST_2OP_OVERFLOW (9, sub, 1, 0x00000000, 0x80000000) ;

ARCTEST_END
