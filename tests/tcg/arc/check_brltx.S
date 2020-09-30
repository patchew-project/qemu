#*****************************************************************************
# brlt.S
#-----------------------------------------------------------------------------
#
# Test brlt instruction.
#

#defirlt ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Branch tests
  #-------------------------------------------------------------

  # Each test checks both forward and backward branches

  TEST_BR_OP_TAKEN( 2, brlt,  0,  1 );
  TEST_BR_OP_NOTTAKEN( 3, brlt,  1,  0 );
  TEST_BR_OP_TAKEN( 4, brlt, -1,  1 );
  TEST_BR_OP_NOTTAKEN( 5, brlt,  1, -1 );

  TEST_BR_OP_NOTTAKEN( 6, brlt,  0,  0 );
  TEST_BR_OP_NOTTAKEN( 7, brlt,  1,  1 );
  TEST_BR_OP_NOTTAKEN( 8, brlt, -1, -1 );
ARCTEST_END
