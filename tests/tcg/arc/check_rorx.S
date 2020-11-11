#define ARCTEST_ARC32

#*****************************************************************************
# check_rorx.S
#-----------------------------------------------------------------------------
#
# Test or instruction.
#
# .--------------.----------.--------------.
# | instruction  | check CC | update flags |
# |--------------+----------+--------------|
# | ror          | no       | Z, N, C      |
# | ror multiple | yes      | Z, N, C      |
# | ror8         | no       | Z, N         |
# `--------------^----------^--------------'

#include "test_macros.h"

ARCTEST_BEGIN

  #-------------------------------------------------------------
  # Logical tests
  #-------------------------------------------------------------
  TEST_RR_3OP( 2, ror , 0xdeadbeef, 0xdeadbeef, 0x00000000);
  TEST_RR_3OP( 3, ror , 0x00000001, 0x00000001, 0x00000000);
  TEST_RR_3OP( 4, ror , 0x80000000, 0x80000000, 0x00000000);
  TEST_RR_3OP( 5, ror , 0xbd5b7ddf, 0xdeadbeef, 0x0000001f);
  TEST_RR_3OP( 6, ror , 0x00000002, 0x00000001, 0x0000001f);
  TEST_RR_3OP( 7, ror , 0x00000001, 0x80000000, 0x0000001f);
  TEST_RR_2OP( 8, ror , 0x80000000, 0x00000001);
  TEST_RR_2OP( 9, ror , 0xdeadbeef, 0xbd5b7ddf);
  TEST_RR_2OP(10, ror8, 0x01000000, 0x00000001);

  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------
  TEST_RR_SRC1_EQ_DEST    (11, ror,  0xca000804, 0x000804ca, 0xfff80008);
  TEST_RR_2OP_SRC1_EQ_DEST(12, ror,  0x80040265, 0x000804cb);

  #-------------------------------------------------------------
  # Flag tests
  #-------------------------------------------------------------
  TEST_2OP_CARRY   (13, ror , 0, 0x00000001, 0x02);
  TEST_2OP_CARRY   (14, ror , 1, 0x00000001, 0x01);
  TEST_2OP_ZERO    (15, ror , 0, 0x00000100, 0xbf);
  TEST_2OP_ZERO    (16, ror , 1, 0x00000000, 0xbf);
  TEST_2OP_NEGATIVE(17, ror , 0, 0x00000001, 0x02);
  TEST_2OP_NEGATIVE(18, ror , 1, 0x80000000, 0x00);
  TEST_2OP_CARRY   (19, ror , 1, 0x000000ff, 0x08);
  TEST_1OP_CARRY   (20, ror , 0, 0x00000002);
  TEST_1OP_CARRY   (21, ror , 1, 0x00000001);
  TEST_1OP_ZERO    (22, ror , 0, 0x00000100);
  TEST_1OP_ZERO    (23, ror , 1, 0x00000000);
  TEST_1OP_NEGATIVE(24, ror , 0, 0x80000000);
  TEST_1OP_NEGATIVE(25, ror , 1, 0x00000001);
  TEST_1OP_CARRY   (26, ror , 1, 0x80000001);
  #ror8 does not update carry
  TEST_1OP_CARRY   (27, ror8, 0, 0x000000ff);
  TEST_1OP_ZERO    (28, ror8, 0, 0x00001000);
  TEST_1OP_ZERO    (29, ror8, 1, 0x00000000);
  TEST_1OP_NEGATIVE(30, ror8, 0, 0x00000040);
  TEST_1OP_NEGATIVE(31, ror8, 1, 0x00000080);

ARCTEST_END
