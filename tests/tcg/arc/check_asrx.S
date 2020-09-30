#define ARCTEST_ARC32

#*****************************************************************************
# check_asrx.S
#-----------------------------------------------------------------------------
#
# Test or instruction.
#
# .--------------.----------.--------------.
# | instruction  | check CC | update flags |
# |--------------+----------+--------------|
# | asr          | no       | Z, N, C      |
# | asr multiple | yes      | Z, N, C      |
# | asr8         | no       | Z, N         |
# | asr16        | no       | Z, N         |
# `--------------^----------^--------------'

#include "test_macros.h"

ARCTEST_BEGIN

  #-------------------------------------------------------------
  # Logical tests
  #-------------------------------------------------------------
  TEST_RR_3OP( 2, asr  , 0x12345678, 0x12345678,  0);
  TEST_RR_3OP( 3, asr  , 0x01234567, 0x12345678,  4);
  TEST_RR_3OP( 4, asr  , 0xFF234567, 0xF2345678,  4);
  TEST_RR_3OP( 5, asr  , 0xffffffff, 0x8fffffff, 31);
  TEST_RR_3OP( 6, asr  , 0x00000001, 0x7fffffff, 30);

  TEST_RR_2OP( 7, asr  , 0x00000009, 0x00000012);
  TEST_RR_2OP( 8, asr  , 0xc0000000, 0x80000000);
  TEST_RR_2OP( 9, asr  , 0x20000000, 0x40000000);

  TEST_RR_2OP(10, asr8 , 0x00000100, 0x00010000);
  TEST_RR_2OP(11, asr8 , 0xffff0000, 0xff000000);
  TEST_RR_2OP(12, asr8 , 0xff800000, 0x80000000);
  TEST_RR_2OP(13, asr8 , 0x007f0000, 0x7f000000);
  TEST_RR_2OP(14, asr8 , 0x00000000, 0x000000ff);

  TEST_RR_2OP(15, asr16, 0x00000001, 0x00010000);
  TEST_RR_2OP(16, asr16, 0xffffffff, 0xffff0000);
  TEST_RR_2OP(17, asr16, 0xffff8000, 0x80000000);
  TEST_RR_2OP(18, asr16, 0x00007fff, 0x7fff0000);
  TEST_RR_2OP(19, asr16, 0x00000000, 0x0000ff00);

  #-------------------------------------------------------------
  # Source/Destination tests
  #-------------------------------------------------------------
  TEST_RR_SRC1_EQ_DEST    (20, asr  ,  0xfffffbdd, 0xdeedbeef, 0x00000013);
  TEST_RR_2OP_SRC1_EQ_DEST(21, asr  ,  0xc0020132, 0x80040265);
  TEST_RR_2OP_SRC1_EQ_DEST(22, asr8 ,  0xff800402, 0x80040265);
  TEST_RR_2OP_SRC1_EQ_DEST(23, asr16,  0xffff8004, 0x80040265);

  #-------------------------------------------------------------
  # Flag tests
  #-------------------------------------------------------------
  TEST_2OP_CARRY   (24, asr  , 0, 0x00000001, 0x02);
  TEST_2OP_CARRY   (25, asr  , 1, 0x00000001, 0x01);
  TEST_2OP_ZERO    (26, asr  , 0, 0x00000004, 0x02);
  TEST_2OP_ZERO    (27, asr  , 1, 0x12345678, 0xbf);
  TEST_2OP_NEGATIVE(28, asr  , 1, 0xFF345678, 0x04);
  TEST_2OP_NEGATIVE(29, asr  , 0, 0x7F345678, 0x04);

  TEST_1OP_CARRY   (30, asr  , 0, 0x00000002);
  TEST_1OP_CARRY   (31, asr  , 1, 0x00000001);
  TEST_1OP_ZERO    (32, asr  , 0, 0x00000002);
  TEST_1OP_ZERO    (33, asr  , 1, 0x00000001);
  TEST_1OP_NEGATIVE(34, asr  , 1, 0x80000000);
  TEST_1OP_NEGATIVE(35, asr  , 0, 0x7fffffff);

  TEST_1OP_CARRY   (36, asr8 , 0, 0x0000007f);
  TEST_1OP_CARRY   (37, asr8 , 0, 0xffffffff);
  TEST_1OP_ZERO    (38, asr8 , 0, 0x00000100);
  TEST_1OP_ZERO    (39, asr8 , 1, 0x000000ff);
  TEST_1OP_NEGATIVE(40, asr8 , 1, 0x80000000);
  TEST_1OP_NEGATIVE(41, asr8 , 0, 0x7fffffff);

  TEST_1OP_CARRY   (42, asr16, 0, 0x00007fff);
  TEST_1OP_CARRY   (43, asr16, 0, 0xffffffff);
  TEST_1OP_ZERO    (44, asr16, 0, 0x00010000);
  TEST_1OP_ZERO    (45, asr16, 1, 0x0000ffff);
  TEST_1OP_NEGATIVE(46, asr16, 1, 0x80000000);
  TEST_1OP_NEGATIVE(47, asr16, 0, 0x7fffffff);

ARCTEST_END
