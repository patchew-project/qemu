#*****************************************************************************
# sub.S
#-----------------------------------------------------------------------------
#
# Test sub instruction.
#

#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN

  #-------------------------------------------------------------
  # Arithmetic tests
  #-------------------------------------------------------------

  TEST_RR_3OP( 2,  sub, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000 );
  TEST_RR_3OP( 3,  sub, 0x0000000000000000, 0x0000000000000001, 0x0000000000000001 );
  TEST_RR_3OP( 4,  sub, 0xfffffffffffffffc, 0x0000000000000003, 0x0000000000000007 );

  TEST_RR_3OP( 5,  sub, 0x0000000000008000, 0x0000000000000000, 0xffffffffffff8000 );
  TEST_RR_3OP( 6,  sub, 0xffffffff80000000, 0xffffffff80000000, 0x0000000000000000 );
  TEST_RR_3OP( 7,  sub, 0xffffffff80008000, 0xffffffff80000000, 0xffffffffffff8000 );

  TEST_RR_3OP( 8,  sub, 0xffffffffffff8001, 0x0000000000000000, 0x0000000000007fff );
  TEST_RR_3OP( 9,  sub, 0x000000007fffffff, 0x000000007fffffff, 0x0000000000000000 );
  TEST_RR_3OP( 10, sub, 0x000000007fff8000, 0x000000007fffffff, 0x0000000000007fff );

  TEST_RR_3OP( 11, sub, 0xffffffff7fff8001, 0xffffffff80000000, 0x0000000000007fff );
  TEST_RR_3OP( 12, sub, 0x0000000080007fff, 0x000000007fffffff, 0xffffffffffff8000 );

  TEST_RR_3OP( 13, sub, 0x0000000000000001, 0x0000000000000000, 0xffffffffffffffff );
  TEST_RR_3OP( 14, sub, 0xfffffffffffffffe, 0xffffffffffffffff, 0x0000000000000001 );
  TEST_RR_3OP( 15, sub, 0x0000000000000000, 0xffffffffffffffff, 0xffffffffffffffff );

  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------

  TEST_RR_SRC1_EQ_DEST( 16, sub, 2, 13, 11 );
  TEST_RR_SRC2_EQ_DEST( 17, sub, 3, 14, 11 );
  TEST_RR_SRC12_EQ_DEST( 18, sub, 0, 13 );
ARCTEST_END
