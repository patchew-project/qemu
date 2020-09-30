#*****************************************************************************
# brhs.S
#-----------------------------------------------------------------------------
#
# Test brhs instruction.
#

#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Branch tests
  #-------------------------------------------------------------

  # Each test checks both forward and backward branches

  TEST_BR_OP_TAKEN( 2, brhs,  0,  0 );
  TEST_BR_OP_TAKEN( 3, brhs,  1,  1 );
  TEST_BR_OP_TAKEN( 4, brhs, -1, -1 );
  TEST_BR_OP_TAKEN( 5, brhs, -1,  1 );

  TEST_BR_OP_NOTTAKEN( 6, brhs,  0,  1 );
  TEST_BR_OP_NOTTAKEN( 7, brhs,  1, -1 );
  TEST_BR_OP_NOTTAKEN( 8, brhs,  33, 0x2aaaaaab );
  TEST_BR_OP_NOTTAKEN( 9, brhs,  123, 124 );
ARCTEST_END
