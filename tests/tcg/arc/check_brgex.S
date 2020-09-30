#*****************************************************************************
# brge.S
#-----------------------------------------------------------------------------
#
# Test brge instruction.
#

#defirge ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Branch tests
  #-------------------------------------------------------------

  # Each test checks both forward and backward branches

  TEST_BR_OP_NOTTAKEN( 2, brge,  0,  1 );
  TEST_BR_OP_TAKEN( 3, brge,  1,  0 );
  TEST_BR_OP_NOTTAKEN( 4, brge, -1,  1 );
  TEST_BR_OP_TAKEN( 5, brge,  1, -1 );

  TEST_BR_OP_TAKEN( 6, brge,  0,  0 );
  TEST_BR_OP_TAKEN( 7, brge,  1,  1 );
  TEST_BR_OP_TAKEN( 8, brge, -1, -1 );
ARCTEST_END
