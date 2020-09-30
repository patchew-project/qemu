#*****************************************************************************
# beq.S
#-----------------------------------------------------------------------------
#
# Test beq instruction.
#

#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Branch tests
  #-------------------------------------------------------------

  # Each test checks both forward and backward branches

  TEST_BR2_OP_TAKEN( 2, beq,  0,  0 );
  TEST_BR2_OP_TAKEN( 3, beq,  1,  1 );
  TEST_BR2_OP_TAKEN( 4, beq, -1, -1 );

  TEST_BR2_OP_NOTTAKEN( 5, beq,  0,  1 );
  TEST_BR2_OP_NOTTAKEN( 6, beq,  1,  0 );
  TEST_BR2_OP_NOTTAKEN( 7, beq, -1,  1 );
  TEST_BR2_OP_NOTTAKEN( 8, beq,  1, -1 );
ARCTEST_END
