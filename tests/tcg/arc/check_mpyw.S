.include "macros.inc"


.macro mul_test val1, val2, res, test_num
  mov    r0, \val1
  mov    r1, \val2
  mpyw   r2, r0, r1
  assert_eq \res, r2, \test_num
.endm


.macro mul_flags_test val1, val2, res, z=0, n=0, v=0, test_num
  mov    r0, \val1
  mov    r1, \val2
  mpyw.f r2, r0, r1
  assert_eq   \res, r2, \test_num
  assert_flag REG_STAT_Z, \z, \test_num
  assert_flag REG_STAT_N, \n, \test_num
  assert_flag REG_STAT_C,  0, \test_num
  assert_flag REG_STAT_V, \v, \test_num
.endm

start

; 21 * 2 = 42
mul_test  21, 2, 42, test_num=1

; make sure only the lower 16 bits are taken into account
; 0x11220005 * 0x00120020 --> 0x0005 * 0x0020 = 160
mul_test  0x11220005, 0x00120020, 160, test_num=2

; testing sign extension and the signed result
; 0xFFFFFFFF * 0x00000007 --> 0xFFFF (-1) * 0x0007 = 0xFFFFFFF9 (-7)
mul_test  0xFFFFFFFF, 0x00000007, 0xFFFFFFF9, test_num=3

; testing flags
mul_flags_test 1337  , 0     , res=0         , z=1, test_num=4
mul_flags_test 0x7FFF, 0x7FFF, res=0x3FFF0001, v=0, test_num=5
mul_flags_test 0xFFFF, 0x0C  , res=0xFFFFFFF4, n=1, test_num=6

end
