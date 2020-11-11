.include "macros.inc"

.equ NOTSET, 47806

; conditionally sets the ACC data
.macro setup_acc acch, accl
  .if \accl <> NOTSET
    mov r58, \accl
  .endif
  .if \acch <> NOTSET
    mov r59, \acch
  .endif
.endm

; conditionally checks if ACC holds the given value
.macro verify_acc racch, raccl, test_num
  .if \raccl <> NOTSET
    assert_eq r58, \raccl, \test_num
  .endif
  .if \racch <> NOTSET
    assert_eq r59, \racch, \test_num
  .endif
.endm

; all Z, N, C, V flags are cleared and ACC will become 0
.macro clear_flags_and_accu
  ; clearing the Z N C V flags
  mov r0, 1
  add.f r0, r0, r0
  ; clearing the acc
  mov r58, 0
  mov r59, 0
.endm

; checks if Z, N, C, and V flags are set correctly
.macro verify_flags z=0, n=0, c=0, v=0, test_num
  assert_flag REG_STAT_Z, \z, \test_num
  assert_flag REG_STAT_N, \n, \test_num
  assert_flag REG_STAT_C, \c, \test_num
  assert_flag REG_STAT_V, \v, \test_num
.endm

; macro for testing "MAC" instruction
.macro mac_test acch=NOTSET, accl=NOTSET, val1, val2, res, racch=NOTSET, raccl=NOTSET, n=0, v=0, test_num
  ; initializing data
  setup_acc \acch, \accl
  mov   r0, \val1
  mov   r1, \val2
  ; operation under test
  mac.f r0, r0, r1
  ; checking the results
  verify_flags n=\n, v=\v, test_num=\test_num
  assert_eq \res, r0, \test_num
  verify_acc \racch, \raccl, \test_num
.endm

; macro for testing "MACU" instruction
.macro macu_test acch=NOTSET, accl=NOTSET, val1, val2, res, racch=NOTSET, raccl=NOTSET, v=0, test_num
  ; initializing data
  setup_acc \acch, \accl
  mov    r0, \val1
  mov    r1, \val2
  ; operation under test
  macu.f r0, r0, r1
  ; checking the results
  verify_flags v=\v, test_num=\test_num
  assert_eq \res, r0, \test_num
  verify_acc \racch, \raccl, \test_num
.endm


; macro for testing "MACD" instruction
.macro macd_test acch=NOTSET, accl=NOTSET, val1, val2, resh, resl, racch=NOTSET, raccl=NOTSET, n=0, v=0, test_num
  ; initializing data
  setup_acc \acch, \accl
  mov    r0, \val1
  mov    r1, \val2
  ; operation under test
  macd.f r0, r0, r1
  ; checking the results
  verify_flags n=\n, v=\v, test_num=\test_num
  assert_eq \resl, r0, \test_num
  assert_eq \resh, r1, \test_num
  verify_acc \racch, \raccl, \test_num
.endm

; macro for testing "MACU" instruction
.macro macdu_test acch=NOTSET, accl=NOTSET, val1, val2, resh, resl, racch=NOTSET, raccl=NOTSET, v=0, test_num
  ; initializing data
  setup_acc \acch, \accl
  mov     r0, \val1
  mov     r1, \val2
  ; operation under test
  macdu.f r0, r0, r1
  ; checking the results
  verify_flags v=\v, test_num=\test_num
  assert_eq \resl, r0, \test_num
  assert_eq \resh, r1, \test_num
  verify_acc \racch, \raccl, \test_num
.endm


start

;;;;;;;;;;;;;;;;;;;;;; MAC ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; acc =  0 +  4*5 = 20
mac_test val1=4, val2=5, res=20, test_num=1
; acc = 20 +  5*1 = 25
mac_test val1=1, val2=5, res=25, test_num=2
; acc = 25 + -1*5 = 20
mac_test val1=0xFFFFFFFF, val2=0x5, res=20, racch=0x0, raccl=20, n=0, test_num=3
; acc = 20 + -3*9 = -7
mac_test val1=0xFFFFFFFD, val2=0x09, res=0xFFFFFFF9, racch=0xFFFFFFFF, raccl=0xFFFFFFF9, n=1, test_num=4
; producing a result that sets both acch and accl
mac_test acch=0, accl=0, val1=0x7FFFFFFF, val2=0x7FFFFFFF, res=1, racch=0x3FFFFFFF, raccl=0x01, n=0, v=0, test_num=5
; acc is 0x3FFFFFFF00000001
mac_test val1=0x80000000, val2=0x80000000, res=1, racch=0x7FFFFFFF, raccl=0x01, n=0, v=0, test_num=6
; acc is 0x7FFFFFFF00000001; going for the kill: N and V will be set
mac_test val1=0x12344321, val2=0x56788654, res=0xE1C14CD5, racch=0x86262098, raccl=0xE1C14CD5, n=1, v=1, test_num=7
; "mac" is not supposed to clear the overflow bit
mac_test acch=0, accl=0, val1=0, val2=0, res=0, racch=0, raccl=0, n=0, v=1, test_num=8
clear_flags_and_accu


;;;;;;;;;;;;;;;;;;;;;; MACU ;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; acc =  0 +  4*5 = 20
macu_test val1=4, val2=5, res=20, test_num=9
; acc = 20 +  5*1 = 25
macu_test val1=1, val2=5, res=25, test_num=10
; acc = 25 + 21,474,836,475 = 21,474,836,500 (0x00000005,0x00000014)
macu_test val1=0xFFFFFFFF, val2=0x5, res=20, racch=5, raccl=20, test_num=11
; acc = 21,474,836,500 + 38,654,705,637 = 60,129,542,137 (0x0000000D,0xFFFFFFF9)
macu_test val1=0xFFFFFFFD, val2=0x09, res=0xFFFFFFF9, racch=0x0D, raccl=0xFFFFFFF9, test_num=12
; producing a result that sets both acch and accl
macu_test acch=0, accl=0, val1=0x7FFFFFFF, val2=0x7FFFFFFF, res=1, racch=0x3FFFFFFF, raccl=0x01, v=0, test_num=13
; acc is 0x3FFFFFFF00000001
macu_test val1=0x80000000, val2=0x80000000, res=1, racch=0x7FFFFFFF, raccl=0x01, v=0, test_num=14
; acc is 0x7FFFFFFF00000001; line below still will not trigger an overflow for MACU
macu_test val1=0x12344321, val2=0x56788654, res=0xE1C14CD5, racch=0x86262098, raccl=0xE1C14CD5, v=0, test_num=15
; cause an overflow
macu_test acch=0xFFFFFFFF, accl=0xFFFFFFFF, val1=1, val2=1, res=0, racch=0, raccl=0, v=1, test_num=16
; "macu" is not supposed to clear the overflow bit
macu_test acch=0, accl=0, val1=0, val2=0, res=0, racch=0, raccl=0, v=1, test_num=17
clear_flags_and_accu


;;;;;;;;;;;;;;;;;;;;; MACD ;;;;;;;;;;;;;;;;;;;;;;;;;;;;
macd_test val1=4, val2=5, resh=0, resl=20, test_num=18
; acc is now 20
macd_test val1=1, val2=5, resh=0, resl=25, test_num=19
; acc = 25 + -1*5 = 20
macd_test val1=0xFFFFFFFF, val2=0x5, resh=0, resl=20, racch=0x0, raccl=20, n=0, test_num=20
; acc = 20 + -3*9 = -7
macd_test val1=0xFFFFFFFD, val2=0x09, resh = 0xFFFFFFFF, resl=0xFFFFFFF9, racch=0xFFFFFFFF, raccl=0xFFFFFFF9, n=1, test_num=21
; producing a result that sets both acch and accl
macd_test acch=0, accl=0, val1=0x7FFFFFFF, val2=0x7FFFFFFF, resh=0x3FFFFFFF, resl=0x01, racch=0x3FFFFFFF, raccl=0x01, v=0, test_num=22
; acc is 0x3FFFFFFF00000001
macd_test val1=0x80000000, val2=0x80000000, resh=0x7FFFFFFF, resl=0x01, racch=0x7FFFFFFF, raccl=0x01, v=0, test_num=23
; acc is 0x7FFFFFFF00000001; going for the kill: N and V will be set
macd_test val1=0x12344321, val2=0x56788654, resh=0x86262098, resl=0xE1C14CD5, racch=0x86262098, raccl=0xE1C14CD5, n=1, v=1, test_num=24
; "macd" is not supposed to clear the overflow bit
macd_test acch=0, accl=0, val1=0, val2=0, resh=0, resl=0, racch=0, raccl=0, n=0, v=1, test_num=25
clear_flags_and_accu


;;;;;;;;;;;;;;;;;;;; MACDU ;;;;;;;;;;;;;;;;;;;;;;;;;;;;
macdu_test val1=4, val2=5, resh=0, resl=20, test_num=26
; acc is now 20
macdu_test val1=1, val2=5, resh=0, resl=25, test_num=27
; acc = 25 + 21,474,836,475 = 21,474,836,500 (0x00000005,0x00000014)
macdu_test val1=0xFFFFFFFF, val2=0x5, resh=5, resl=20, racch=5, raccl=20, test_num=28
; acc = 21,474,836,500 + 38,654,705,637 = 60,129,542,137 (0x0000000D,0xFFFFFFF9)
macdu_test val1=0xFFFFFFFD, val2=0x09, resh=0x0D, resl=0xFFFFFFF9, racch=0x0D, raccl=0xFFFFFFF9, test_num=29
; producing a result that sets both acch and accl
macdu_test acch=0, accl=0, val1=0x7FFFFFFF, val2=0x7FFFFFFF, resh=0x3FFFFFFF, resl=0x01, racch=0x3FFFFFFF, raccl=0x01, v=0, test_num=30
; acc is 0x3FFFFFFF00000001
macdu_test val1=0x80000000, val2=0x80000000, resh=0x7FFFFFFF, resl=0x01, racch=0x7FFFFFFF, raccl=0x01, v=0, test_num=31
; acc is 0x7FFFFFFF00000001; line below still will not trigger an overflow for MACU
macdu_test val1=0x12344321, val2=0x56788654, resh=0x86262098, resl=0xE1C14CD5, racch=0x86262098, raccl=0xE1C14CD5, v=0, test_num=32
; cause an overflow
macdu_test acch=0xFFFFFFFF, accl=0xFFFFFFFF, val1=1, val2=1, resh=0, resl=0, racch=0, raccl=0, v=1, test_num=33
; "macdu" is not supposed to clear the overflow bit
macdu_test acch=0, accl=0, val1=0, val2=0, resh=0, resl=0, racch=0, raccl=0, v=1, test_num=34
clear_flags_and_accu


;;;;;;;;;;;;;;;;;;; CC anf FF ;;;;;;;;;;;;;;;;;;;;;;;;;
mov r0, 0xFFFFFFFF
mov r1, 0x11111111
mac.f r2, r0, r1
assert_flag REG_STAT_N, 1, test_num=35
clear_flags_and_accu

mov r0, 0xFFFFFFFF
mov r1, 0x11111111
mac r2, r0, r1
assert_flag REG_STAT_N, 0, test_num=36
clear_flags_and_accu

setup_acc acch=0xFFFFFFFF, accl=0xFFFFFFFF
mov r0, 0x01
mov r1, 0x01
; earlier, this caused an overflow; see test case 25
macdu r2, r0, r1
assert_flag REG_STAT_V, 0, test_num=37
clear_flags_and_accu

; FIXME: uncomment code below when assmbler starts supporting conditon codes
; cause an overflow and then execute based on CC
;mov r0, 42
;mov r1, 1
;mov r2, 0x1337
;;macu.v r2, r1, r0        ; assembler does not support this line
;assert_eq 0x1337, r2, test_num=38
;
;mov r0, 42
;mov r1, 1
;; causing the N bit to be set
;mov r4, 0xFFFFFFFF
;add.f r4, r4, r4
;; conditional execution and update flags
;macd.N.f r2, r1, r0      ; assembler does not support this line
;assert_flag REG_STAT_N, 0, test_num=39
;assert_eq 42, r2, test_num=39


;;;;;;;;;;;;;;;;;;; Finished ;;;;;;;;;;;;;;;;;;;;;;;;;;
end
