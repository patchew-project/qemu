; check_mpyd.S
;
; Tests for mpyd: mpyd mpydu
; If the test fails, check the end of this file for how to troubleshoot.

  .include "macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;; Test checking routines ;;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case counter
.data
test_nr:
  .word 0x0

; Increment the test counter and set (Z,N,C,V) to (0,0,0,0).
.macro prep_test_case
  ld    r13, [test_nr]
  add_s r13, r13, 1       ; increase test case counter
  st    r13, [test_nr]
  add.f 0, 0, 1           ; (Z, N, C, V) = (0, 0, 0, 0)
.endm

; These flag checking macros do not directly load the
; status32 register. Instead, they rely on the value
; provided by the caller. The rationale is with all these
; "cmp"s status32 will change. One must use a recorded
; version of status32 at the right time and then try the
; macros.
.macro check_Z_is_clear status
  mov   r11, \status
  mov   r12, REG_STAT_Z
  and   r11, r11, r12
  cmp   r11, 0
  bne   @fail
.endm
.macro check_N_is_set status
  mov   r11, \status
  mov   r12, REG_STAT_N
  and   r11, r11, r12
  cmp   r11, REG_STAT_N
  bne   @fail
.endm
.macro check_N_is_clear status
  mov   r11, \status
  mov   r12, REG_STAT_N
  and   r11, r11, r12
  cmp   r11, 0
  bne   @fail
.endm
.macro check_V_is_set status
  mov   r11, \status
  mov   r12, REG_STAT_V
  and   r11, r11, r12
  cmp   r11, REG_STAT_V
  bne   @fail
.endm
.macro check_V_is_clear status
  mov   r11, \status
  mov   r12, REG_STAT_V
  and   r11, r11, r12
  cmp   r11, 0
  bne   @fail
.endm

; pair(HI, LOW) == pair(REG_HI, REG_LO) == pair(R59, R58)
.macro  check_64bit_result      hi, low, reg_hi, reg_lo
  mov   r11, \hi
  mov   r10, \low
  cmp   r11, \reg_hi
  bne   @fail
  cmp   r11, r59
  bne   @fail
  cmp   r10, \reg_lo
  bne   @fail
  cmp   r10, r58
  bne   @fail
.endm

; (Z, N, C, V) = (0, 0, 0, 1)
.macro  clear_N_set_V
  mov   r11, 0x80000000   ; very small negative number
  add.f 0, r11, r11       ; cause an overflow (with carry)
  rol.f 0, 0x01           ; keep the V flag, set the rests to 0
.endm

; (Z, N, C, V) = (0, 1, 0, 1)
.macro  set_N_set_V
  add.f 0, 0x7fffffff, 1  ; negative result with an overflow
.endm

;;;;;;;;;;;;;;;;;;;;;;;;; Exception related code ;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; parameters that an IllegalInstruction exception may set.
  .align 4
ecr_ref  : .word ILLEGAL_INSTRUCTION
addr_ref : .word 0x0                  ; for both eret and efa
cont_addr: .word 0x0

; exception: IllegalInstruction
; regs used: r11, r12
;
; A parameterized IllegalInstruction exception that checks the followings:
; ecr  == Illegal instruction
; efa  == efa_ref
; eret == eret_ref
; If everything passes, it will jump to 'cont_addr' parameter. The parameters
; must be set beforehand using 'set_except_params' macro.  This requires
; ivt.S file to be compiled and linked.
  .align 4
  .global instruction_error
  .type instruction_error, @function
instruction_error:
  ld   r11, [ecr_ref]
  lr   r12, [ecr]
  cmp  r12, r11
  bne  @fail
  ld   r11, [addr_ref]
  lr   r12, [eret]
  cmp  r12, r11
  bne  @fail
  lr   r12, [efa]
  cmp  r12, r11
  bne  @fail
  ; Success: continuing
  ld   r11, [cont_addr]
  sr   r11, [eret]
  rtie

; macro:      set_except_params
; regs used:  r11
;
; This macro writes the provided parameters to a temporary place holder
; that later will be used by exception above to verify as reference.
.macro set_except_params addr, continue
  mov  r11, \addr
  st   r11, [addr_ref]
  mov  r11, \continue
  st   r11, [cont_addr]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; MPYD ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Let the tests begin
  start

; Test case 1
; reg4 <- reg4, reg4
; 1 = (-1)*(-1)
  prep_test_case
  mov     r4, -1
  mpyd    r4, r4, r4
  check_64bit_result  0x0, 0x1, r5, r4

; Test case 2
; reg0 <- reg1, reg0
; 0 = 0 * 0x22334455
  prep_test_case
  mov     r0, 0x22334455    ; bogus data
  mov     r1, 0
  set_N_set_V               ; (Z,N,C,V)=(0,1,0,1)
  mpyd.f  r0, r1, r0
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_Z_is_clear r5
  check_N_is_clear r5
  check_V_is_clear r5
  check_64bit_result  0x0, 0x0, r1, r0

; Test case 3
; reg2 <- reg3, limm
; 0xc0000000_80000000 = 0x7ffffffff*0x80000000
; -4611686016279904256= 2147483647 * -2147483648
  prep_test_case
  mov     r3, 0x7fffffff      ; biggest 32-bit positive number
  clear_N_set_V               ; (Z,N,C,V)=(0,0,0,1)
  mpyd.f  r2, r3, 0x80000000  ; smallest 32-bit negative number
  lr      r5, [status32]      ; take a snapshot of statu32 as is
  check_N_is_set   r5
  check_V_is_clear r5
  check_64bit_result  0xc0000000, 0x80000000, r3, r2

; Test case 4
; reg2 <- limm, reg3
; 0xffffffff_87654321 = 0x87654321 * 1
; This is like a sign extension
  prep_test_case
  mov     r3, 1
  clear_N_set_V               ; (Z,N,C,V)=(0,0,0,1)
  mpyd.f  r2, 0x87654321, r3
  lr      r5, [status32]      ; take a snapshot of statu32 as is
  check_N_is_set   r5
  check_V_is_clear r5
  check_64bit_result  0xffffffff, 0x87654321, r3, r2

; Test case 5
; reg0 <- limm, limm
; 0x3fffffff_00000001 = 0x7fffffff*0x7fffffff
; 4611686014132420609 = 2147483647*2147483647
  prep_test_case
  set_N_set_V                 ; (Z,N,C,V)=(0,1,0,1)
  mpyd    r0, 0x7fffffff, 0x7fffffff
  lr      r5, [status32]      ; take a snapshot of statu32 as is
  check_N_is_set r5
  check_V_is_set r5
  check_64bit_result 0x3fffffff, 0x00000001, r1, r0

; Test case 6
; 0 <- limm, limm       only (acch,accl) will be set.
; It is expected that V=0 and N=0
; 4761 = 69 * 69
  prep_test_case
  set_N_set_V               ; (Z,N,C,V)=(0,1,0,1)
  mpyd.f  0, 69, 69
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_N_is_clear r5
  check_V_is_clear r5
  check_64bit_result 0, 4761, r59, r58

; Test case 7
; 0 <- limm, u6         only (acch,accl) will be set.
; Checking that a result of 0 does not set the Z flag.
; 0 = 0x12345678 * 0
  prep_test_case
  set_N_set_V               ; (Z,N,C,V)=(0,1,0,1)
  mpyd.f  0, 0x12345678, 0
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_Z_is_clear r5       ; Z must have remained 0
  check_N_is_clear r5
  check_V_is_clear r5
  check_64bit_result 0, 0, r59, r58

; Test case 8
; 0 <- reg2, limm    (V is already 1)
; Nothing should change, other than (acch,accl).
; 0x2468a = 2 * 0x12345
  prep_test_case
  mov     r2, 2
  clear_N_set_V             ; (Z,N,C,V)=(0,0,0,1)
  mpyd    0, r2, 0x12345
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_V_is_set r5
  check_64bit_result 0, 0x2468a, r59, r58

; Test case 9
; reg0 <- reg2, u6
; -63 = -1 * 63
  prep_test_case
  mov     r2, -1
  mpyd    r0, r2, 63
  check_64bit_result 0xffffffff, 0xffffffc1, r1, r0

; Test case 10
; reg2 <- limm, u6
; 0x2_7d27d268 = 0x12345678 * 35
  prep_test_case
  mpyd    r2, 0x12345678, 35
  check_64bit_result 0x00000002, 0x7d27d268, r3, r2

; Test case 11
; reg4 <- reg4, s12
; 0x0000002f_1c71c71c =  0x87654321 * 0xf9c
;        202340681500 = -2023406815 * -100
  prep_test_case
  mov     r4, 0x87654321
  mpyd    r4, r4, -100
  check_64bit_result 0x0000002f, 0x1c71c71c, r5, r4

; Test case 12
; 0 <- limm, s12
; It is expected that V is cleared and N=1
; -1250000 = -10000 * 125
  prep_test_case
  clear_N_set_V             ; (Z,N,C,V)=(0,0,0,1)
  mpyd.f  0, -10000 , 125
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_N_is_set   r5
  check_V_is_clear r5
  check_64bit_result -1, -1250000, r59, r58

; Test case 13
; Testing when cc condition is met
; 0 <- limm, u6      (V is already set)
; It is expected that V is cleared and N=1
; -126 = -2 * 63
  prep_test_case
  clear_N_set_V             ; (Z,N,C,V)=(0,0,0,1)
  mpyd.v.f 0, -2, 63
  lr       r5, [status32]   ; take a snapshot of statu32 as is
  check_N_is_set   r5
  check_V_is_clear r5
  check_64bit_result -1, -126, r59, r58

; Test case 14
; Testing when cc condition is not met
; reg0 <- reg0, reg2 (V is already set)
; It is expected that V is remanins set
  prep_test_case
  clear_N_set_V             ; (Z,N,C,V)=(0,0,0,1)
  mov       r0, 0xc0de      ; must remain ...
  mov       r1, 0x1337      ; ... (0x1337,0xc0de)
  mov       r2, 0xf00d      ; don't care ...
  mov       r3, 0xbad       ; as long as not (0x0,0x1)
  mov       r4, r58         ; record accl
  mov       r5, r59         ; record acch
  mpyd.nv.f r0, r0, r2
  lr        r2, [status32]  ; take a snapshot of statu32 as is
  check_V_is_set r2
  cmp       r1, 0x1337
  bne       @fail
  cmp       r0, 0xc0de
  bne       @fail
  check_64bit_result r5, r4, r59, r58

; Test case 15
; Raise an Illegal Instruction exception if an odd register as dest.
  prep_test_case
  set_except_params @test_15_exception, @test_15_end
test_15_exception:
  mpyd  r3, r2, r4
  b     @fail
test_15_end:
  ; Fall through

; Test case 16
; Raise an Illegal Instruction exception if an odd register as dest.
; The exception should be made even if the CC indicates no execution.
  prep_test_case
  set_except_params @test_16_exception, @test_16_end
  add.f   0,0,1         ; (Z,N,C,V)=(0,0,0,0)
test_16_exception:
  mpyd.z  r1, r1, r4
  b       @fail
test_16_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; MPYDU ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case 17
; reg2 <- reg2, reg2
; 1 = (-1)*(-1)
; 0xfffffffe_00000001 = 0xffffffff * 0xffffffff
  prep_test_case
  mov     r2, -1
  mpydu   r2, r2, r2
  check_64bit_result  0xfffffffe, 0x00000001, r3, r2

; Test case 18
; reg2 <- reg3, reg2
; 0 = 0 * 0x22334455
  prep_test_case
  mov     r2, 0x22334455    ; bogus data
  mov     r3, 0
  set_N_set_V               ; (Z,N,C,V)=(0,1,0,1)
  mpydu.f r2, r3, r2
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_Z_is_clear r5
  check_N_is_set   r5
  check_V_is_clear r5
  check_64bit_result  0x0, 0x0, r3, r2

; Test case 19
; reg2 <- reg3, limm
; 0x3fffffff_80000000 = 0x7ffffffff*0x80000000
; 4611686016279904256 = 2147483647 * 2147483648
  prep_test_case
  mov     r3, 0x7fffffff      ; what used to be the largest 32-bit number
  clear_N_set_V               ; (Z,N,C,V)=(0,0,0,1)
  mpydu.f r2, r3, 0x80000000  ; just another positive number
  lr      r5, [status32]      ; take a snapshot of statu32 as is
  check_V_is_clear r5
  check_64bit_result  0x3fffffff, 0x80000000, r3, r2

; Test case 20
; reg4 <- limm, reg5
; 0x00000000_87654321 = 0x87654321 * 1
; This is like an unsigned extension
  prep_test_case
  mov     r5, 1
  set_N_set_V                 ; (Z,N,C,V)=(0,1,0,1)
  mpydu.f r4, 0x87654321, r5
  lr      r3, [status32]      ; take a snapshot of statu32 as is
  check_N_is_set   r3
  check_V_is_clear r3
  check_64bit_result  0x00000000, 0x87654321, r5, r4

; Test case 21
; reg0 <- limm, limm
; 0x40000000_00000000 = 0x80000000*0x80000000
; 4611686018427387904 = 2147483648*2147483648
  prep_test_case
  set_N_set_V                 ; (Z,N,C,V)=(0,1,0,1)
  mpydu   r0, 0x80000000, 0x80000000
  lr      r5, [status32]      ; take a snapshot of statu32 as is
  check_N_is_set r5
  check_V_is_set r5
  check_64bit_result 0x40000000, 0x00000000, r1, r0

; Test case 22
; 0 <- limm, limm       only (acch,accl) will be set.
; It is expected that V=0 and N=0
; 3876961 = 1969 * 1969
  prep_test_case
  set_N_set_V               ; (Z,N,C,V)=(0,1,0,1)
  mpydu.f 0, 1969, 1969
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_N_is_set   r5
  check_V_is_clear r5
  check_64bit_result 0, 3876961, r59, r58

; Test case 23
; 0 <- limm, u6         only (acch,accl) will be set.
; Checking that a result of 0 does not set the Z flag.
; 0 = 0x12345678 * 0
  prep_test_case
  set_N_set_V               ; (Z,N,C,V)=(0,1,0,1)
  mpydu.f 0, 0x12345678, 0
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_Z_is_clear r5       ; Z must have remained 0
  check_N_is_set   r5
  check_V_is_clear r5
  check_64bit_result 0, 0, r59, r58

; Test case 24
; 0 <- reg2, limm    (V is already 1)
; Nothing should change, other than (acch,accl).
; 0x00001eac_0d5d17a4 = 0x1af54154 * 0x12345
;      33724307412900 = 452280660  * 74565
  prep_test_case
  mov     r2, 0x1af54154    ; I let an ant walk on the keyboard
  clear_N_set_V             ; (Z,N,C,V)=(0,0,0,1)
  mpydu   0, r2, 0x12345
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_V_is_set r5
  check_64bit_result 0x1eac, 0x0d5d17a4, r59, r58

; Test case 25
; reg0 <- reg2, u6
; 0x3e_ffffffc1 = 0xffffffff * 0x3f
;  270582939585 = 4294967295 * 63
  prep_test_case
  mov     r2, -1
  mpydu   r0, r2, 63
  check_64bit_result 0x3e, 0xffffffc1, r1, r0

; Test case 26
; reg4 <- limm, u6
; 0x2_7d27d268 = 0x12345678 * 35
  prep_test_case
  mpydu   r4, 0x12345678, 35
  check_64bit_result 0x00000002, 0x7d27d268, r5, r4

; Test case 27
; reg2 <- reg2, s12
; 0x000003e3_8e36b328 =  0xfedcba09 * 0x3e8
;      4275878409000  =  4275878409 *  1000
  prep_test_case
  mov     r2, 0xfedcba09
  mpydu   r2, r2, 1000
  check_64bit_result 0x000003e3, 0x8e36b328, r3, r2

; Test case 28
; 0 <- limm, s12
; It is expected that V is cleared
; 1250000 = 10000 * 125
  prep_test_case
  clear_N_set_V             ; (Z,N,C,V)=(0,0,0,1)
  mpydu.f 0, 10000 , 125
  lr      r5, [status32]    ; take a snapshot of statu32 as is
  check_V_is_clear r5
  check_64bit_result 0, 1250000, r59, r58

; Test case 29
; Testing when cc condition is met
; 0 <- limm, u6      (V is already set)
; It is expected that V is cleared and N=1
; 1781818164 = 28282828 * 63
  prep_test_case
  set_N_set_V               ; (Z,N,C,V)=(0,1,0,1)
  mpydu.n.f 0, 28282828, 63
  lr        r5, [status32]  ; take a snapshot of statu32 as is
  check_N_is_set   r5
  check_V_is_clear r5
  check_64bit_result 0, 1781818164, r59, r58

; Test case 30
; Testing when cc condition is not met
; reg0 <- reg0, reg2 (V is already set)
; It is expected that V is remanins set
  prep_test_case
  set_N_set_V               ; (Z,N,C,V)=(0,1,0,1)
  mov       r0, 0xc0de      ; must remain ...
  mov       r1, 0x1337      ; ... (0x1337,0xc0de)
  mov       r2, 0xf00d      ; don't care ...
  mov       r3, 0xbad       ; as long as not (0x0,0x1)
  mov       r4, r58         ; record accl
  mov       r5, r59         ; record acch
  mpyd.p.f r0, r0, r2       ; execute only if positive (N==0)
  lr        r2, [status32]  ; take a snapshot of statu32 as is
  check_V_is_set r2
  cmp       r1, 0x1337
  bne       @fail
  cmp       r0, 0xc0de
  bne       @fail
  check_64bit_result r5, r4, r59, r58

; Test case 31
; Raise an Illegal Instruction exception if an odd register as dest.
  prep_test_case
  set_except_params @test_31_exception, @test_31_end
test_31_exception:
  mpydu r1, r4, r0
  b     @fail
test_31_end:
  ; Fall through

; Test case 32
; Raise an Illegal Instruction exception if an odd register as dest.
; The exception should be made even if the CC indicates no execution.
  prep_test_case
  set_except_params @test_32_exception, @test_32_end
  add.f   0,0,1         ; (Z,N,C,V)=(0,0,0,0)
test_32_exception:
  mpydu.v r5, r5, r4
  b       @fail
test_32_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Reporting ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

valhalla:
  print "[PASS]"
  b @1f

; If a test fails, it jumps here. Although, for the sake of uniformity,
; the printed output does not say much about which test case failed,
; one can uncomment the print_number line below or set a breakpoint
; here to check the R0 register for the test case number.
fail:
  ld r0, [test_nr]
  print_number r0
  print "[FAIL]"
1:
  print " mpyd: mpyd mpydu\n"
  end
