#*****************************************************************************
# breq.S
#-----------------------------------------------------------------------------
#
# Test breq instruction.
#

#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Branch tests
  #-------------------------------------------------------------

  # Each test checks both forward and backward branches

  TEST_BR_OP_TAKEN( 2, breq,  0,  0 );
  TEST_BR_OP_TAKEN( 3, breq,  1,  1 );
  TEST_BR_OP_TAKEN( 4, breq, -1, -1 );

  TEST_BR_OP_NOTTAKEN( 5, breq,  0,  1 );
  TEST_BR_OP_NOTTAKEN( 6, breq,  1,  0 );
  TEST_BR_OP_NOTTAKEN( 7, breq, -1,  1 );
  TEST_BR_OP_NOTTAKEN( 8, breq,  1, -1 );
ARCTEST_END
