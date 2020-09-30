#define ARCTEST_ARC32

#*****************************************************************************
# check_aslx.S
#-----------------------------------------------------------------------------
#
# Test or instruction.
#
# .--------------.----------.--------------.
# | instruction  | check CC | update flags |
# |--------------+----------+--------------|
# | asl          | no       | Z, N, C, V   |
# | asl multiple | yes      | Z, N, C      |
# `--------------^----------^--------------'

#include "test_macros.h"

ARCTEST_BEGIN

  #-------------------------------------------------------------
  # Logical tests
  #-------------------------------------------------------------
  TEST_RR_3OP(2, asl, 0x12345678, 0x12345678,  0);
  TEST_RR_3OP(3, asl, 0x23456780, 0x12345678,  4);
  TEST_RR_3OP(4, asl, 0x80000000, 0x12345671, 31);

  TEST_RR_2OP(5, asl, 0x00000002, 0x00000001);
  TEST_RR_2OP(6, asl, 0x00000000, 0x80000000);

  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------
  TEST_RR_SRC1_EQ_DEST    (7, asl,  0xf7780000, 0xdeedbeef, 0x00000013);
  TEST_RR_2OP_SRC1_EQ_DEST(8, asl,  0x000804ca, 0x80040265);

  #-------------------------------------------------------------
  # Flag tests
  #-------------------------------------------------------------
  TEST_2OP_CARRY   ( 9, asl, 0, 0x10000000, 0x02);
  TEST_2OP_CARRY   (10, asl, 1, 0x80000000, 0x01);
  TEST_2OP_CARRY   (11, asl, 0, 0xffffffff, 0x00);
  TEST_2OP_ZERO    (12, asl, 1, 0x12345670, 0xbf);
  TEST_2OP_NEGATIVE(13, asl, 1, 0x1F345678, 0x04);
  # no overflow flag update in "asl multiple"
  TEST_2OP_OVERFLOW(14, asl, 0, 0x80000000, 0x01);

  TEST_1OP_CARRY   (15, asl, 0, 0x40000000);
  TEST_1OP_CARRY   (16, asl, 1, 0x80000000);
  TEST_1OP_ZERO    (17, asl, 0, 0x00001000);
  TEST_1OP_ZERO    (18, asl, 1, 0x80000000);
  TEST_1OP_NEGATIVE(19, asl, 0, 0x20000000);
  TEST_1OP_NEGATIVE(20, asl, 1, 0x40000000);
  TEST_1OP_OVERFLOW(21, asl, 1, 0x80000000);
  TEST_1OP_OVERFLOW(22, asl, 0, 0xffffffff);
  TEST_1OP_OVERFLOW(23, asl, 1, 0x40000000);

ARCTEST_END
