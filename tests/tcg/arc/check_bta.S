; check_bta.S
; Tests for setting Branch Target Address register.
; The BTA register is updated if and only if the
; branch is going to be taken (cc = true) AND there
; is a delay slot:
;                 ,-----------.-----------.
;                 | not taken |   taken   |
; ,---------------|-----------+-----------|
; | no delay slot |     -     |     -     |
; |---------------|-----------+-----------|
; |    delay slot |     -     |  UPDATE   |
; `---------------^-----------^-----------'
; In other words, BTA is updated only when STATUS32.DE is set.
;
; TODO: Add test cases for Bcc, JL, JLcc, BBITn
; TODO: the following test cases fail in QEMU: 3, 9
;       the condition of the tests are (not taken, delay slot)
;       and yet QEMU insists on updating the BTA.

  .include "macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;; Test checking routines ;;;;;;;;;;;;;;;;;;;;;;;;;;

.data
; Test case counter
test_nr:
  .word 0x0
; Saved BTA
saved_bta:
  .word 0x0

; Increment the test counter
.macro prep_test_case
  ld    r13, [test_nr]
  add_s r13, r13, 1       ; Increase test case counter
  st    r13, [test_nr]
  lr    r13, [bta]
  st    r13, [saved_bta]  ; Some tests need this to check if BTA changed.
.endm

; Compares the current BTA with the value saved at the start of a
; test by PREP_TEST_CASE. If not the same, the test will fail.
.macro check_bta_remained_intact
  ld    r13, [saved_bta]
  lr    r12, [bta]
  cmp   r13, r12
  bne   @fail
.endm

; Checks the BTA against the EXPECTED_BTA.
; If they're not the same, the test will fail.
.macro check_bta    expected_bta
  mov   r13, \expected_bta
  lr    r12, [bta]
  cmp   r13, r12
  bne   @fail
.endm

; Checks if the given REGs are equal. Fails, if not.
.macro check_equal  reg, expected_reg
  cmp   \expected_reg, \reg
  bne   @fail
.endm

; Checks if REG is one number bigger than ORIG_REG.
.macro check_one_above    reg, orig_reg
  mov   r13, \orig_reg
  add_s r13, r13, 1
  cmp   r13, \reg
  bne   @fail
.endm

;;;;;;;;;;;;;;;;;;;;;;;;; Conditonal branches ;;;;;;;;;;;;;;;;;;;;;;;;;

start

; Test case 1
; Conditional branch is not taken and there is no delay slot.
; BTA mustn't be updated.
  prep_test_case
  ld     r0, [test_nr]
  add    r1, r0, 1
  breq   r0, r1, @test_01_target
  check_bta_remained_intact
  b      @test_01_end
test_01_target:
  b      @fail
test_01_end:
  ; Fall through

; Test case 2
; Conditional branch is taken but there is no delay slot.
; BTA mustn't be updated.
  prep_test_case
  ld     r0, [test_nr]
  mov    r1, r0
  breq   r0, r1, @test_02_target
  b      @fail
test_02_target:
  check_bta_remained_intact

; Test case 3
; Conditional branch is not taken but there is a delay slot.
; BTA mustn't be updated.
  prep_test_case
  ld     r0, [test_nr]
  add    r1, r0, 1
  breq.d r0, r1, @test_03_target
  add_s  r0, r0, 1
  check_bta_remained_intact
  check_equal r0, r1
  b      @test_03_end
test_03_target:
  b      @fail
test_03_end:
  ; Fall through

; Test case 4
; Conditional branch is taken AND there is a delay slot.
; BTA must be updated.
  prep_test_case
  ld     r0, [test_nr]
  mov    r1, r0
  breq.d r0, r1, @test_04_target
  add_s  r0, r0, 1
  b      @fail
test_04_target:
  check_bta   @test_04_target
  check_one_above r0, r1

;;;;;;;;;;;;;;;;;;;;;;;; Unconditonal branches ;;;;;;;;;;;;;;;;;;;;;;;;

; Test case 5
; Branch unconditionally but there is no delay slot.
; BTA mustn't be updated.
  prep_test_case
  b      @test_05_target
  b      @fail
test_05_target:
  check_bta_remained_intact

; Test case 6
; Branch unconditionally AND there is a delay slot.
; BTA must be updated.
  prep_test_case
  ld     r0, [test_nr]
  mov    r1, r0
  b.d    @test_06_target
  add_s  r0, r0, 1
  b      @fail
test_06_target:
  check_bta   @test_06_target
  check_one_above r0, r1

;;;;;;;;;;;;;;;;;;;;;;;;;; Conditonal jumps ;;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case 7
; Conditional jump is not taken and there is no delay slot.
; BTA mustn't be updated.
  prep_test_case
  ld     r0, [test_nr]
  add    r1, r0, 1
  cmp    r0, r1
  jz     @test_07_target
  check_bta_remained_intact
  b      @test_07_end
test_07_target:
  b      @fail
test_07_end:
  ; Fall through

; Test case 8
; Conditional jump is taken but there is no delay slot.
; BTA mustn't be updated.
  prep_test_case
  ld     r0, [test_nr]
  mov    r1, r0
  cmp    r0, r1
  jz     @test_08_target
  add_s  r0, r0, 1

  b      @fail
test_08_target:
  check_bta_remained_intact

; Test case 9
; Conditional jump is not taken but there is a delay slot.
; BTA mustn't be updated.
  prep_test_case
  ld     r0, [test_nr]
  add    r1, r0, 1
  mov    r2, @test_09_target
  cmp    r0, r1
  jz.d   [r2]
  add_s  r0, r0, 1
  check_bta_remained_intact
  check_equal r0, r1
  b      @test_09_end
test_09_target:
  b      @fail
test_09_end:
  ; Fall through

; Test case 10
; Conditional jump is taken AND there is a delay slot.
; BTA must be updated.
  prep_test_case
  ld     r0, [test_nr]
  mov    r1, r0
  mov    r2, @test_10_target
  cmp    r0, r1
  jz.d   [r2]
  add_s  r0, r0, 1
  b      @fail
test_10_target:
  check_bta   @test_10_target
  check_one_above r0, r1

;;;;;;;;;;;;;;;;;;;;;;; Conditonal short jumps ;;;;;;;;;;;;;;;;;;;;;;;

; Test case 11
; Conditional short jump is not taken (there can't be a delay slot).
; BTA mustn't be updated.
  prep_test_case
  ld     r0, [test_nr]
  add    r1, r0, 1
  mov    blink, @test_11_target
  cmp    r0, r1
  jeq_s  [blink]
  check_bta_remained_intact
  check_one_above r1, r0
  b      @test_11_end
test_11_target:
  b      @fail
test_11_end:
  ; Fall through

; Test case 12
; Conditional short jump is taken (there can't be a delay slot).
; BTA mustn't be updated.
  prep_test_case
  ld     r0, [test_nr]
  add    r1, r0, 1
  mov    blink, @test_12_target
  cmp    r0, r1
  jne_s  [blink]
  add_s  r0, r0, 1
  b      @fail
test_12_target:
  check_bta_remained_intact
  check_one_above r1, r0

;;;;;;;;;;;;;;;;;;;;;;;;; Unconditonal jumps ;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case 13
; Jump unconditionally but there is no delay slot.
; BTA mustn't be updated.
  prep_test_case
  j      @test_13_target
  b      @fail
test_13_target:
  check_bta_remained_intact

; Test case 14
; Jump unconditionally AND there is a delay slot.
; BTA must be updated.
  prep_test_case
  ld     r0, [test_nr]
  mov    r1, r0
  mov    r2, @test_14_target
  j.d    [r2]
  add_s  r0, r0, 1
  b      @fail
test_14_target:
  check_bta   @test_14_target
  check_one_above r0, r1

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
  print " BTA register (implicit) writing\n"
  end
