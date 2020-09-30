#*****************************************************************************
# xor.S
#-----------------------------------------------------------------------------
#
# Test xor instruction.
#
#define ARCTEST_ARC32
#include "test_macros.h"

ARCTEST_BEGIN
  #-------------------------------------------------------------
  # Logical tests
  #-------------------------------------------------------------

  TEST_IMM_OP( 2, xor, 0xffffffffff00f00f, 0x0000000000ff0f00, 0xf0f );
  TEST_IMM_OP( 3, xor, 0x000000000ff00f00, 0x000000000ff00ff0, 0x0f0 );
  TEST_IMM_OP( 4, xor, 0x0000000000ff0ff0, 0x0000000000ff08ff, 0x70f );
  TEST_IMM_OP( 5, xor, 0xfffffffff00ff0ff, 0xfffffffff00ff00f, 0x0f0 );
  TEST_RR_3OP( 6, xor, 0xf00ff00f, 0xff00ff00, 0x0f0f0f0f );
  TEST_RR_3OP( 7, xor, 0xff00ff00, 0x0ff00ff0, 0xf0f0f0f0 );
  TEST_RR_3OP( 8, xor, 0x0ff00ff0, 0x00ff00ff, 0x0f0f0f0f );
  TEST_RR_3OP( 9, xor, 0x00ff00ff, 0xf00ff00f, 0xf0f0f0f0 );

  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------

  TEST_IMM_SRC1_EQ_DEST( 10, xor, 0xffffffffff00f00f, 0xffffffffff00f700, 0x70f );
  TEST_RR_SRC1_EQ_DEST( 11, xor, 0xf00ff00f, 0xff00ff00, 0x0f0f0f0f );
  TEST_RR_SRC2_EQ_DEST( 12, xor, 0xf00ff00f, 0xff00ff00, 0x0f0f0f0f );
  TEST_RR_SRC12_EQ_DEST( 13, xor, 0x00000000, 0xff00ff00 );
ARCTEST_END
