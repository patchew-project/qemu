#define ARCTEST_ARC32

#*****************************************************************************
# or.S
#-----------------------------------------------------------------------------
#
# Test or instruction.
#

#include "test_macros.h"

ARCTEST_BEGIN

  #-------------------------------------------------------------
  # Logical tests
  #-------------------------------------------------------------
  TEST_IMM_OP( 2, or, 0xffffffffffffff0f, 0xffffffffff00ff00, 0xf0f );
  TEST_IMM_OP( 3, or, 0x000000000ff00ff0, 0x000000000ff00ff0, 0x0f0 );
  TEST_IMM_OP( 4, or, 0x0000000000ff07ff, 0x0000000000ff00ff, 0x70f );
  TEST_IMM_OP( 5, or, 0xfffffffff00ff0ff, 0xfffffffff00ff00f, 0x0f0 );
  TEST_RR_3OP( 6, or, 0xff0fff0f, 0xff00ff00, 0x0f0f0f0f );
  TEST_RR_3OP( 7, or, 0xfff0fff0, 0x0ff00ff0, 0xf0f0f0f0 );
  TEST_RR_3OP( 8, or, 0x0fff0fff, 0x00ff00ff, 0x0f0f0f0f );
  TEST_RR_3OP( 9, or, 0xf0fff0ff, 0xf00ff00f, 0xf0f0f0f0 );

  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------

  TEST_IMM_SRC1_EQ_DEST( 10, or, 0xff00fff0, 0xff00ff00, 0x0f0 );
  TEST_RR_SRC1_EQ_DEST( 11, or, 0xff0fff0f, 0xff00ff00, 0x0f0f0f0f );
  TEST_RR_SRC2_EQ_DEST( 12, or, 0xff0fff0f, 0xff00ff00, 0x0f0f0f0f );
  TEST_RR_SRC12_EQ_DEST( 13, or, 0xff00ff00, 0xff00ff00 );
ARCTEST_END
