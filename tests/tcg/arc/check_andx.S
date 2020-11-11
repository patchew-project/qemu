#define ARCTEST_ARC32

#*****************************************************************************
# and.S
#-----------------------------------------------------------------------------
#
# Test and instruction.
#

#include "test_macros.h"

ARCTEST_BEGIN

  #-------------------------------------------------------------
  # Logical tests
  #-------------------------------------------------------------

  TEST_IMM_OP( 2, and, 0xff00ff00, 0xff00ff00, 0xf0f );
  TEST_IMM_OP( 3, and, 0x000000f0, 0x0ff00ff0, 0x0f0 );
  TEST_IMM_OP( 4, and, 0x0000000f, 0x00ff00ff, 0x70f );
  TEST_IMM_OP( 5, and, 0x00000000, 0xf00ff00f, 0x0f0 );
  TEST_RR_3OP( 6, and, 0x0f000f00, 0xff00ff00, 0x0f0f0f0f );
  TEST_RR_3OP( 7, and, 0x00f000f0, 0x0ff00ff0, 0xf0f0f0f0 );
  TEST_RR_3OP( 8, and, 0x000f000f, 0x00ff00ff, 0x0f0f0f0f );
  TEST_RR_3OP( 9, and, 0xf000f000, 0xf00ff00f, 0xf0f0f0f0 );

  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------

  TEST_IMM_SRC1_EQ_DEST( 10, and, 0x00000000, 0xff00ff00, 0x0f0 );
  TEST_RR_SRC1_EQ_DEST( 11, and, 0x0f000f00, 0xff00ff00, 0x0f0f0f0f );
  TEST_RR_SRC2_EQ_DEST( 12, and, 0x00f000f0, 0x0ff00ff0, 0xf0f0f0f0 );
  TEST_RR_SRC12_EQ_DEST( 13, and, 0xff00ff00, 0xff00ff00 );

ARCTEST_END
