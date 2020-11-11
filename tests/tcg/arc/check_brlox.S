#*****************************************************************************
# brlo.S
#-----------------------------------------------------------------------------
#
# Test brlo instruction.
#

#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Branch tests
  #-------------------------------------------------------------

  # Each test checks both forward and backward branches

  TEST_BR_OP_NOTTAKEN( 2, brlo,  0,  0 );
  TEST_BR_OP_NOTTAKEN( 3, brlo,  1,  1 );
  TEST_BR_OP_NOTTAKEN( 4, brlo, -1, -1 );
  TEST_BR_OP_NOTTAKEN( 5, brlo, -1,  1 );

  TEST_BR_OP_TAKEN( 6, brlo,  0,  1 );
  TEST_BR_OP_TAKEN( 7, brlo,  1, -1 );
  TEST_BR_OP_TAKEN( 8, brlo,  33, 0x2aaaaaab );
ARCTEST_END
