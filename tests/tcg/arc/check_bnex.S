#*****************************************************************************
# bne.S
#-----------------------------------------------------------------------------
#
# Test bne instruction.
#

#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Branch tests
  #-------------------------------------------------------------

  # Each test checks both forward and backward branches

  TEST_BR2_OP_TAKEN( 2, bne,  0,  1 );
  TEST_BR2_OP_TAKEN( 3, bne,  1,  0 );
  TEST_BR2_OP_TAKEN( 4, bne, -1,  1 );
  TEST_BR2_OP_TAKEN( 5, bne,  1, -1 );

  TEST_BR2_OP_NOTTAKEN( 6, bne,  0,  0 );
  TEST_BR2_OP_NOTTAKEN( 7, bne,  1,  1 );
  TEST_BR2_OP_NOTTAKEN( 8, bne, -1, -1 );
ARCTEST_END
