#define ARCTEST_ARC32

#*****************************************************************************
# add.S
#-----------------------------------------------------------------------------
#
# Test add instruction.
#

#include "test_macros.h"

ARCTEST_BEGIN

  #-------------------------------------------------------------
  # Arithmetic tests
  #-------------------------------------------------------------

  TEST_IMM_OP( 2,  add, 0x00000000, 0x00000000, 0x000 )
  TEST_IMM_OP( 3,  add, 0x00000002, 0x00000001, 0x001 )
  TEST_IMM_OP( 4,  add, 0x0000000a, 0x00000003, 0x007 )

  TEST_IMM_OP( 5,  add, 0xfffffffffffff800, 0x0000000000000000, 0x800 )
  TEST_IMM_OP( 6,  add, 0xffffffff80000000, 0xffffffff80000000, 0x000 )
  TEST_IMM_OP( 7,  add, 0xffffffff7ffff800, 0xffffffff80000000, 0x800 )

  TEST_IMM_OP( 8,  add, 0x00000000000007ff, 0x00000000, 0x7ff )
  TEST_IMM_OP( 9,  add, 0x000000007fffffff, 0x7fffffff, 0x000 )
  TEST_IMM_OP( 10, add, 0x00000000800007fe, 0x7fffffff, 0x7ff )

  TEST_IMM_OP( 11, add, 0xffffffff800007ff, 0xffffffff80000000, 0x7ff )
  TEST_IMM_OP( 12, add, 0x000000007ffff7ff, 0x000000007fffffff, 0x800 )

  TEST_IMM_OP( 13, add, 0xffffffffffffffff, 0x0000000000000000, 0xfff )
  TEST_IMM_OP( 14, add, 0x0000000000000000, 0xffffffffffffffff, 0x001 )
  TEST_IMM_OP( 15, add, 0xfffffffffffffffe, 0xffffffffffffffff, 0xfff )

  TEST_IMM_OP( 16, add, 0x0000000080000000, 0x7fffffff, 0x001 )

  TEST_RR_3OP( 17,  add, 0x00000000, 0x00000000, 0x00000000 )
  TEST_RR_3OP( 18,  add, 0x00000002, 0x00000001, 0x00000001 )
  TEST_RR_3OP( 19,  add, 0x0000000a, 0x00000003, 0x00000007 )

  TEST_RR_3OP( 20,  add, 0xffffffffffff8000, 0x0000000000000000, 0xffffffffffff8000 )
  TEST_RR_3OP( 21,  add, 0xffffffff80000000, 0xffffffff80000000, 0x00000000 )
  TEST_RR_3OP( 22,  add, 0xffffffff7fff8000, 0xffffffff80000000, 0xffffffffffff8000 )

  TEST_RR_3OP( 23,  add, 0x0000000000007fff, 0x0000000000000000, 0x0000000000007fff )
  TEST_RR_3OP( 24,  add, 0x000000007fffffff, 0x000000007fffffff, 0x0000000000000000 )
  TEST_RR_3OP( 25,  add, 0x0000000080007ffe, 0x000000007fffffff, 0x0000000000007fff )

  TEST_RR_3OP( 26,  add, 0xffffffff80007fff, 0xffffffff80000000, 0x0000000000007fff )
  TEST_RR_3OP( 27,  add, 0x000000007fff7fff, 0x000000007fffffff, 0xffffffffffff8000 )

  TEST_RR_3OP( 28,  add, 0xffffffffffffffff, 0x0000000000000000, 0xffffffffffffffff )
  TEST_RR_3OP( 29,  add, 0x0000000000000000, 0xffffffffffffffff, 0x0000000000000001 )
  TEST_RR_3OP( 30,  add, 0xfffffffffffffffe, 0xffffffffffffffff, 0xffffffffffffffff )

  TEST_RR_3OP( 31,  add, 0x0000000080000000, 0x0000000000000001, 0x000000007fffffff )


  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------

  TEST_IMM_SRC1_EQ_DEST( 32, add, 24, 13, 11 )

  TEST_RR_SRC1_EQ_DEST( 33, add, 24, 13, 11 )
  TEST_RR_SRC2_EQ_DEST( 34, add, 25, 14, 11 )
  TEST_RR_SRC12_EQ_DEST( 35, add, 26, 13 )

ARCTEST_END
