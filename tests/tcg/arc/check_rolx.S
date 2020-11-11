#define ARCTEST_ARC32

#*****************************************************************************
# check_rolx.S
#-----------------------------------------------------------------------------
#
# Test or instruction.
#
# .-------------.----------.--------------.
# | instruction | check CC | update flags |
# |-------------+----------+--------------|
# | rol         | no       | Z, N, C      |
# | rol8        | no       | Z, N         |
# `-------------^----------^--------------'

#include "test_macros.h"

ARCTEST_BEGIN

  #-------------------------------------------------------------
  # Logical tests
  #-------------------------------------------------------------
  TEST_RR_2OP(2, rol , 0xbd5b7ddf, 0xdeadbeef);
  TEST_RR_2OP(3, rol8, 0x00000001, 0x01000000);

  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------
  TEST_RR_2OP_SRC1_EQ_DEST(4, rol,  0x94001009, 0xca000804);

  #-------------------------------------------------------------
  # Flag tests
  #-------------------------------------------------------------
  TEST_1OP_CARRY   ( 5, rol , 0, 0x40000000);
  TEST_1OP_CARRY   ( 6, rol , 1, 0x80000000);
  TEST_1OP_ZERO    ( 8, rol , 0, 0x00001000);
  TEST_1OP_ZERO    ( 9, rol , 1, 0x00000000);
  TEST_1OP_NEGATIVE(10, rol , 0, 0x80000000);
  TEST_1OP_NEGATIVE(11, rol , 1, 0x40000000);
  #rol8 does not update carry
  TEST_1OP_CARRY   (12, rol8, 0, 0x000000ff);
  TEST_1OP_ZERO    (13, rol8, 0, 0x00001000);
  TEST_1OP_ZERO    (14, rol8, 1, 0x00000000);
  TEST_1OP_NEGATIVE(15, rol8, 0, 0x00000040);
  TEST_1OP_NEGATIVE(16, rol8, 1, 0x00800000);

ARCTEST_END
