; check_vsub.S
;
; Tests for vsub: vsub2 vsub2h vsub4h
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

; Checks if (Z,N,C,V) == (0,0,0,0). This relies on "ADD.F 0,0,1"
; instruction in PREP_TEST_CASE macro. From a PREP_TEST_CASE macro
; in a test case, and thence to a VECTOR instruction and finally to
; this macro, none of the aforementioned flags must have been set,
; because VECTOR instructions aren't supposed to do so.
.macro  check_flags_remained_zero
  lr    r11, [status32]
  mov   r12, REG_STAT_Z
  or    r12, r12, REG_STAT_N
  or    r12, r12, REG_STAT_C
  or    r12, r12, REG_STAT_V
  and   r11, r11, r12
  cmp   r11, 0
  bne   @fail
.endm

; pair(HI, LOW) == pair(REG_HI, REG_LO)
; HI, LO:         32-bit
; REG_HI, REG_LO: 32-bit
.macro  check_64bit_double      hi, low, reg_hi, reg_lo
  check_flags_remained_zero
  mov   r11, \hi
  mov   r10, \low
  cmp   r11, \reg_hi
  bne   @fail
  cmp   r10, \reg_lo
  bne   @fail
.endm

; REG == (HI, LO)
; HI, LO: 16-bit
; REG:    32-bit
.macro  check_32bit_double      hi, low, reg
  check_flags_remained_zero
  mov   r11, \hi
  and   r11, r11, 0xffff
  lsl16 r11, r11
  mov   r12, \low
  and   r12, r12, 0xffff
  or    r11, r11, r12
  cmp   r11, \reg
  bne   @fail
.endm

; quartet(q3, q2, q1, q0) == pair64(REG_HI, REG_LO)
; Q3, Q2, Q1, Q0: 16-bit
; REG_HI, REG_LO: 32-bit
.macro  check_64bit_quadruple   q3, q2, q1, q0, reg_hi, reg_lo
  check_flags_remained_zero
  mov   r11, \q3
  and   r11, r11, 0xffff
  lsl16 r11, r11
  mov   r12, \q2
  and   r12, r12, 0xffff
  or    r11, r11, r12
  mov   r10, \q1
  and   r10, r10, 0xffff
  lsl16 r10, r10
  mov   r12, \q0
  and   r12, r12, 0xffff
  or    r10, r10, r12
  cmp   r11, \reg_hi
  bne   @fail
  cmp   r10, \reg_lo
  bne   @fail
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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; VSUB2 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Let the tests begin
  start

; Test case 1
; reg2 <- reg2, reg2
; (0x00000000,0x00000000)=(0x80000000,0xffffffff)-(0x80000000,0xffffffff)
; The Z flag must remain 0.
  prep_test_case
  mov   r2, 0xffffffff
  mov   r3, 0x80000000
  vsub2 r2, r2, r2
  check_64bit_double 0, 0, r3, r2

; Test case 2
; reg0 <- reg2, reg0
; (0x7fffffff,0x80000000)=(-1,0x7fffffff)-(0x80000000,-1)
; The V and N flags must remain zero.
  prep_test_case
  mov   r3, 0xffffffff
  mov   r2, 0x7fffffff
  mov   r1, 0x80000000
  mov   r0, 0xffffffff
  vsub2 r0, r2, r0
  check_64bit_double 0x7fffffff, 0x80000000, r1, r0

; Test case 3
; reg0 <- limm, reg4
; (0x90abcdef,0x00000001)=(0x12345678,0x12345678)-(0x81888889,0x12345677)
  prep_test_case
  mov   r0, 0x11111111            ; bogus data
  mov   r1, 0x22222222            ; bogus data
  mov   r4, 0x12345677
  mov   r5, 0x81888889
  vsub2 r0, 0x12345678, r4
  check_64bit_double 0x90abcdef, 0x01, r1, r0

; Test case 4
; reg4 <- reg2, limm
; (-999999999,-999999998)=(1,2)-(1000000000,1000000000)
; The N flag must not be set, irrespective of having negative results.
  prep_test_case
  mov   r2, 2
  mov   r3, 1
  vsub2 r4, r2, 0x3b9aca00    ; 0x3b9aca00=1000000000
  check_64bit_double -999999999, -999999998, r5, r4

; Test case 5
; reg2 <- limm, limm  (both limm should be the same)
; (0x00,0x00)=(0x12345678,0x12345678)-(0x12345678,0x12345678)
  prep_test_case
  vsub2 r2, 0x12345678, 0x12345678
  check_64bit_double 0, 0, r3, r2

; Test case 6
; reg4 <- limm, u6
; (0x010202c5,0x010202c5)=(0x01020304,0x01020304)-(0x3f,0x3f)
  prep_test_case
  vsub2 r4, 0x01020304, 63
  check_64bit_double 0x010202c5, 0x010202c5, r5, r4

; Test case 7
; reg2 <- reg4, 0(u6)
; (0x08070605,0x04030201)=(0x08070605,0x04030201)-(0,0)
  prep_test_case
  mov   r5, 0x08070605
  mov   r4, 0x04030201
  vsub2 r2, r4, 0
  check_64bit_double 0x08070605, 0x04030201, r3, r2

; Test case 8
; reg0 <- reg0, s12
; (2048,-200000000)=(0,-2000002048)-(-2048,-2048)
  prep_test_case
  mov   r1, 0
  mov   r0, -2000002048
  vsub2 r0, r0, -2048
  check_64bit_double 2048, -2000000000, r1, r0

; Test case 9
; 0 <- limm, s12
; (X,X)=(0xffeeddbb,0xffeeddbb)-(-2048,-2048)
  prep_test_case
  vsub2 0, 0xffeeddbb, -2048

; Test case 10
; Testing when cc condition is met
; (0,0)=(3,2)+(3,2)
  prep_test_case
  mov     r2, 2
  mov     r3, 3
  mov     r4, 0x80000000  ; setting...
  add.f   0,r4,r4         ; ...C=1
  vsub2.c r2, r2, r2
  add.f   0,0,1           ; so that CHECK_FLAGS_REMAINED_ZERO won't fail.
  check_64bit_double 0, 0, r3, r2

; Test case 11
; Testing when cc condition is not met
; (2,0)
  prep_test_case
  mov     r2, 0
  mov     r3, 2
  vsub2.z r2, r2, r2  ; Z=0 because of PREP_TEST_CASE
  check_64bit_double 2, 0, r3, r2

; Test case 12
; Raise an Illegal Instruction exception if an odd register is used.
; Even if there is no register to save the result to.
  prep_test_case
  set_except_params @test_12_exception, @test_12_end
test_12_exception:
  vsub2 0, r5, r0
  b     @fail
test_12_end:
  ; Fall through

; Test case 13
; Raise an Illegal Instruction exception if an odd register is used.
; The exception should be made even if the CC indicates no execution.
  prep_test_case        ; (Z,N,C,V)=(0,0,0,0)
  set_except_params @test_13_exception, @test_13_end
test_13_exception:
  vsub2.c r1, r1, r0
  b       @fail
test_13_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; VSUB2H ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case 14
; reg1 <- reg1, reg1
; (0x0000,0x0000)=(0x8000,0xffff)-(0x8000,0xffff)
; To boot, the Z flag must remain unsett.
  prep_test_case
  mov    r1, 0x8000ffff
  vsub2h r1, r1, r1
  check_32bit_double 0, 0, r1

; Test case 15
; reg1 <- reg1, reg3
; (0x7fff,0x8000)=(-1,0x7fff)-(0x8000,-1)
; The V and N flags must remain zero.
  prep_test_case
  mov    r1, 0xffff7fff
  mov    r3, 0x8000ffff
  vsub2h r1, r1, r3
  check_32bit_double 0x7fff, 0x8000, r1

; Test case 16
; reg0 <- limm, reg4
; (0x4321,0x0001)=(0x1234,0x5678)-(0xcf13,0x5677)
  prep_test_case
  mov    r0, 0x11111111           ; bogus data
  mov    r4, 0xcf135677
  vsub2h r0, 0x12345678, r4
  check_32bit_double 0x4321, 0x0001, r0

; Test case 17
; reg5 <- reg3, limm
; (-9999,-9998)=(1,2)-(10000,10000)
; The N flag must not be set, irrespective of having negative results.
  prep_test_case
  mov    r3, 0x00010002           ; (1,2)
  vsub2h r5, r3, 0x27102710       ; (1,2)-(10000,10000)
  check_32bit_double -9999, -9998, r5

; Test case 18
; reg1 <- limm, limm  (both limm should be the same)
; (0x00,0x00)=(0x1234,0x5678)-(0x1234,0x5678)
  prep_test_case
  vsub2h r1, 0x12345678, 0x12345678
  check_32bit_double 0, 0, r1

; Test case 19
; reg0 <- limm, u6
; (0x00c3,0x02c5)=(0x0102,0x0304)-(0x3f,0x3f)
  prep_test_case
  vsub2h r0, 0x01020304, 63
  check_32bit_double 0x00c3, 0x02c5, r0

; Test case 20
; reg1 <- reg0, 0(u6)
; (0x0403,0x0201)=(0x0403,0x0201)-(0,0)
  prep_test_case
  mov    r0, 0x04030201
  vsub2h r1, r0, 0
  check_32bit_double 0x0403, 0x0201, r1

; Test case 21
; reg5 <- reg5, s12
; (66,-20415)=(1,-20480)-(-65,-65)
  prep_test_case
  mov    r5, 0x0001b000   ; (1,-20480)
  vsub2h r5, r5, -65
  check_32bit_double 66, -20415, r5

; Test case 22
; 0 <- limm, s12
; (X,X)=(0xffee,0xddbb)-(-2048,-2048)
  prep_test_case
  vsub2h 0, 0xffeeddbb, -2048

; Test case 23
; Testing when cc condition is met
; (0,0)=(3,2)+(3,2)
  prep_test_case
  mov      r1, 0x00030002
  mov      r0, 0x80000000 ; setting...
  add.f    0,r0,r0        ; ...V=1
  vsub2h.v r1, r1, r1
  add.f    0,0,1          ; so that CHECK_FLAGS_REMAINED_ZERO won't fail.
  check_32bit_double 0, 0, r1

; Test case 24
; Testing when cc condition is not met
; (2,0)
  prep_test_case
  mov      r4, 0x00020000
  vsub2h.n r4, r4, r4  ; N=0 because of PREP_TEST_CASE
  check_32bit_double 2, 0, r4

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; VSUB4H ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case 25
; reg2 <- reg2, reg2
; (0x0000,0x0000,0x0000,0x0000)=(0x8000,0x7000,0xfffe,0xffff)-
;                               (0x8000,0x7000,0xfffe,0xffff)
; The Z flag must remain 0.
  prep_test_case
  mov    r2, 0xfffeffff
  mov    r3, 0x80007000
  vsub4h r2, r2, r2
  check_64bit_quadruple 0, 0, 0, 0, r3, r2

; Test case 26
; reg0 <- reg2, reg0
; (0x7fff,-2,977,0x8000)=(-1,10,1000,0x7fff)-(0x8000,12,23,-1)
; The V and N flags must remain zero.
  prep_test_case
  mov    r3, 0xffff000a     ; (-1    ,     10)
  mov    r2, 0x03e87fff     ; (1000  , 0x7fff)
  mov    r1, 0x8000000c     ; (0x8000,     12)
  mov    r0, 0x0017ffff     ; (23    ,     -1)
  vsub4h r0, r2, r0
  check_64bit_quadruple 0x7fff, -2, 977, 0x8000, r1, r0

; Test case 27
; reg0 <- limm, reg4
; (0x4321,0x8765,0x90ab,0xcdef)=(0x1234,0x5678,0x1234,0x5678)-
;                               (0xcf13,0xcf13,0x8189,0x8889)
  prep_test_case
  mov    r0, 0x11111111           ; bogus data
  mov    r1, 0x22222222           ; bogus data
  mov    r5, 0xcf13cf13
  mov    r4, 0x81898889
  vsub4h r0, 0x12345678, r4
  check_64bit_quadruple 0x4321, 0x8765, 0x90ab, 0xcdef, r1, r0

; Test case 28
; reg4 <- reg2, limm
; (-9999,-10,-9653,417)=(1,2,347,429)-(10000,12,10000,12)
; The N flag must not be set, irrespective of having negative results.
  prep_test_case
  mov    r3, 0x00010002         ; (1  ,   2)
  mov    r2, 0x015b01ad         ; (347, 429)
  vsub4h r4, r2, 0x2710000c     ; (0x2710,000c)=(10000,12)
  check_64bit_quadruple -9999, -10, -9653, 417, r5, r4

; Test case 29
; reg2 <- limm, limm  (both limm should be the same)
; (0x0000,0x0000,0x0000,0x0000)=(0x1234,0x5678,0x1234,0x5678)-
;                               (0x1234,0x5678,0x1234,0x5678)
  prep_test_case
  vsub4h r2, 0x12345678, 0x12345678
  check_64bit_quadruple 0, 0, 0, 0, r3, r2

; Test case 30
; reg4 <- limm, u6
; (0x00c3,0x02c5,0x00c3,0x02c5)=(0x0102,0x0304,0x0102,0x0304)-
;                               (  0x3f,  0x3f,  0x3f,  0x3f)
  prep_test_case
  vsub4h r4, 0x01020304, 63
  check_64bit_quadruple 0x00c3,0x02c5, 0x00c3, 0x02c5, r5, r4

; Test case 31
; reg0 <- reg4, 0(u6)
; (0x1122,0x3344,0x5566,0x7788)=(0x1122,0x3344,0x5566,0x7788)-
;                               (0x0000,0x0000,0x0000,0x0000)
  prep_test_case
  mov    r5, 0x11223344
  mov    r4, 0x55667788
  vsub4h r0, r4, 0
  check_64bit_quadruple 0x1122, 0x3344, 0x5566, 0x7788, r1, r0

; Test case 32
; reg2 <- reg2, s12
; (-4094,1,-2035,-2049)=(-2047,2048,12,-2)-(2047,2047,2047,2047)
  prep_test_case
  mov    r3, 0xf8010800
  mov    r2, 0x000cfffe     ; (12, -2)
  vsub4h r2, r2, 2047
  check_64bit_quadruple -4094, 1, -2035, -2049, r3, r2

; Test case 33
; 0 <- limm, s12
; (X,X,X,X)=(0xffee,0xddbb,0xffee,0xddbb)-(-2048,-2048,-2048,-2048)
  prep_test_case
  vsub4h 0, 0xffeeddbb, -2048

; Test case 34
; Testing when cc condition is met
; (0,0,0,0)=(3,2,1,0)-(3,2,1,0)
  prep_test_case
  mov      r3, 0x00030002
  mov      r2, 0x00010000
  mov      r4, 0x80000000   ; setting...
  add.f    0,r4,r4          ; ...C=1
  vsub4h.c r2, r2, r2
  add.f    0,0,1            ; so that CHECK_FLAGS_REMAINED_ZERO won't fail.
  check_64bit_quadruple 0, 0, 0, 0, r3, r2

; Test case 35
; Testing when cc condition is not met
; (2,0,2,0)
  prep_test_case
  mov      r3, 0x00020000
  mov      r2, 0x00020000
  vsub4h.z r2, r2, r2       ; Z=0 because of PREP_TEST_CASE
  check_64bit_quadruple 2, 0, 2, 0, r3, r2

; Test case 36
; Raise an Illegal Instruction exception if an odd register is used.
; Even if there is no register to save the result to.
  prep_test_case
  set_except_params @test_36_exception, @test_36_end
test_36_exception:
  vsub4h 0, r3, r0
  b      @fail
test_36_end:
  ; Fall through

; Test case 37
; Raise an Illegal Instruction exception if an odd register is used.
; The exception should be made even if the CC indicates no execution.
  prep_test_case        ; (Z,N,C,V)=(0,0,0,0)
  set_except_params @test_37_exception, @test_37_end
test_37_exception:
  vsub4h.v r5, r5, r0
  b        @fail
test_37_end:
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
  ;print_number r0
  print "[FAIL]"
1:
  print " vsub: vsub2 vsub2h vsub4h\n"
  end
