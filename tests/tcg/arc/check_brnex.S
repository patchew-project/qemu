#*****************************************************************************
# brne.S
#-----------------------------------------------------------------------------
#
# Test brne instruction.
#

#defirne ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Branch tests
  #-------------------------------------------------------------

  # Each test checks both forward and backward branches

  TEST_BR_OP_TAKEN( 2, brne,  0,  1 );
  TEST_BR_OP_TAKEN( 3, brne,  1,  0 );
  TEST_BR_OP_TAKEN( 4, brne, -1,  1 );
  TEST_BR_OP_TAKEN( 5, brne,  1, -1 );

  TEST_BR_OP_NOTTAKEN( 6, brne,  0,  0 );
  TEST_BR_OP_NOTTAKEN( 7, brne,  1,  1 );
  TEST_BR_OP_NOTTAKEN( 8, brne, -1, -1 );
ARCTEST_END
