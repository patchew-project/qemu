.include "macros.inc"

.macro validate res, actual, z, n, c, v, test_num
  assert_eq   \res, \actual, \test_num
  assert_flag REG_STAT_Z, \z, \test_num
  assert_flag REG_STAT_N, \n, \test_num
  assert_flag REG_STAT_C, \c, \test_num
  assert_flag REG_STAT_V, \v, \test_num
.endm

.macro sub0_flags_test val1, val2, res, z=0, n=0, c=0, v=0, test_num=1
  mov    r0, \val1
  mov    r1, \val2
  sub.f  r2, r0, r1
  validate  \res, r2, \z, \n, \c, \v, \test_num
.endm

.macro sub1_flags_test val1, val2, res, z=0, n=0, c=0, v=0, test_num=1
  mov    r0, \val1
  mov    r1, \val2
  sub1.f r2, r0, r1
  validate  \res, r2, \z, \n, \c, \v, \test_num
.endm

.macro sub2_flags_test val1, val2, res, z=0, n=0, c=0, v=0, test_num=1
  mov    r0, \val1
  mov    r1, \val2
  sub2.f r2, r0, r1
  validate  \res, r2, \z, \n, \c, \v, \test_num
.endm

.macro sub3_flags_test val1, val2, res, z=0, n=0, c=0, v=0, test_num=1
  mov    r0, \val1
  mov    r1, \val2
  sub3.f r2, r0, r1
  validate  \res, r2, \z, \n, \c, \v, \test_num
.endm


start

sub0_flags_test 0xA0000000, 0xB0000000, 0xF0000000, z=0, n=1, c=1, v=0, test_num=0x01
sub1_flags_test 0xA0000000, 0x58000000, 0xF0000000, z=0, n=1, c=1, v=0, test_num=0x02
sub2_flags_test 0xA0000000, 0x2C000000, 0xF0000000, z=0, n=1, c=1, v=0, test_num=0x03
sub3_flags_test 0xA0000000, 0x16000000, 0xF0000000, z=0, n=1, c=1, v=0, test_num=0x04

sub0_flags_test 0xFFFFFF80, 0xF0000000, 0x0FFFFF80, z=0, n=0, c=0, v=0, test_num=0x05
sub1_flags_test 0xFFFFFF80, 0x78000000, 0x0FFFFF80, z=0, n=0, c=0, v=0, test_num=0x06
sub2_flags_test 0xFFFFFF80, 0x3C000000, 0x0FFFFF80, z=0, n=0, c=0, v=0, test_num=0x07
sub3_flags_test 0xFFFFFF80, 0x1E000000, 0x0FFFFF80, z=0, n=0, c=0, v=0, test_num=0x08

sub0_flags_test 0x80000000, 0x80000000, 0x00000000, z=1, n=0, c=0, v=0, test_num=0x09
sub1_flags_test 0x80000000, 0x40000000, 0x00000000, z=1, n=0, c=0, v=0, test_num=0x10
sub2_flags_test 0x80000000, 0x20000000, 0x00000000, z=1, n=0, c=0, v=0, test_num=0x11
sub3_flags_test 0x80000000, 0x10000000, 0x00000000, z=1, n=0, c=0, v=0, test_num=0x12

sub0_flags_test 0x80000000, 0xC0000000, 0xC0000000, z=0, n=1, c=1, v=0, test_num=0x13
sub1_flags_test 0x80000000, 0x60000000, 0xC0000000, z=0, n=1, c=1, v=0, test_num=0x14
sub2_flags_test 0x80000000, 0x30000000, 0xC0000000, z=0, n=1, c=1, v=0, test_num=0x15
sub3_flags_test 0x80000000, 0x18000000, 0xC0000000, z=0, n=1, c=1, v=0, test_num=0x16

sub0_flags_test 0x80000000, 0x00000008, 0x7FFFFFF8, z=0, n=0, c=0, v=1, test_num=0x17
sub1_flags_test 0x80000000, 0x00000004, 0x7FFFFFF8, z=0, n=0, c=0, v=1, test_num=0x18
sub2_flags_test 0x80000000, 0x00000002, 0x7FFFFFF8, z=0, n=0, c=0, v=1, test_num=0x19
sub3_flags_test 0x80000000, 0x00000001, 0x7FFFFFF8, z=0, n=0, c=0, v=1, test_num=0x20

end
