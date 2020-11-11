;; These are the tests cases for verifying the functionality of
;; enter_s and leave_s. It is assumed that there are 32 general
;; purpose registers available (r0 ... r31). It is also good to
;; remark the aliases for some of the registers:
;; r27: fp
;; r28: sp
;; r31: blink

  .include "macros.inc"

;;;;;;;;;;;;;;;;;;; / Exception Verification Helpers \ ;;;;;;;;;;;;;;;;;;;;;;
; these are the parameters that the exception routine uses as reference
  .data
  .align 4
ecr_ref    : .word 0x0
eret_ref   : .word 0x0
efa_ref    : .word 0x0
erbta_ref  : .word 0x0
cont_addr  : .word 0x0
test_number: .word 0x0
  .text
  .align 4

; macro:      set_excep_params
; regs used:  r11
;
; this macro writes the provided parameters to a temporary place holder
; that later will be used by ProtV exception above to verify as reference
.macro set_excep_params ecr, eret, efa, erbta, continue, test_num
  mov  r11, \ecr
  st   r11, [ecr_ref]
  mov  r11, \efa
  st   r11, [efa_ref]
  mov  r11, \eret
  st   r11, [eret_ref]
  mov  r11, \erbta
  st   r11, [erbta_ref]
  mov  r11, \continue
  st   r11, [cont_addr]
  mov  r11, \test_num
  st   r11, [test_number]
.endm

; exception verification routine
; regs used: r11, r12
;
; this is a parameterized exception that will check the followings:
; ecr   == ecr_ref
; efa   == efa_ref
; eret  == eret_ref
; if everything passes, it will jump to 'cont_addr' parameter. it will clear
; the user bit before the jump, ie if an exception  is  raised in user mode,
; the continuation after exception will be in kernel mode.
; the parameters must be set beforehand using 'set_except_params' macro.
; last but not least, this requires ivt.S file to be compiled and linked.
  .align 4
  .global instruction_error
  .global EV_Misaligned
  .type instruction_error, @function
  .type EV_Misaligned, @function
instruction_error:
EV_Misaligned:
  ld   r11, [ecr_ref]
  lr   r12, [ecr]
  brne r12, r11, @exc_fail
  ld   r11, [eret_ref]
  lr   r12, [eret]
  brne r12, r11, @exc_fail
  ld   r11, [efa_ref]
  lr   r12, [efa]
  brne r12, r11, @exc_fail
  ld   r11, [erbta_ref]
  lr   r12, [erbta]
  brne r12, r11, @exc_fail
  ; do not pursue the branch target anymore
  lr   r11, [erstatus]
  and  r11, r11, ~0x8040    ; clear ES and DE bit
  sr   r11, [erstatus]
  ; going back to given address
  ld   r11, [cont_addr]
  sr   r11, [eret]
  rtie
exc_fail:
  ld   r11, [test_number]
  print_number r11
  print "[FAIL] :exception is not sane:"
  b @endtest
;;;;;;;;;;;;;;;;;;; \ Exception Verification Helpers / ;;;;;;;;;;;;;;;;;;;;;;

  start
  mov     sp , 0x1000     ; let's set sp to 0x100 for all the tests

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; u6=0 leads to doing nothing
test00:
  mov     r10, sp
  enter_s 0                ; enter_s []
  brne    sp, r10, @test00_fail
  j       @test01

test00_fail:
  print "[FAIL] :test00:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; saving first 3 general purpose registers
test01:
  mov     r10, sp       ; ,-- top ---.
  mov     r13, 13       ; | r13 = 13 |
  mov     r14, 14       ; | r14 = 14 |
  mov     r15, 15       ; | r15 = 15 |
  enter_s [r13-r15]     ; `- bottom -'
  pop     r3
  pop     r4
  pop     r5
  brne    r3,  13, @test01_fail
  brne    r4,  14, @test01_fail
  brne    r5,  15, @test01_fail
  brne    sp, r10, @test01_fail
  j       @test02

test01_fail:
  print "[FAIL] :test01:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; saving blink register
test02:
  mov     r10, sp
  mov     blink, 0x123
  enter_s [blink]
  pop     r1
  brne    r1, 0x123, @test02_fail
  brne    sp,   r10, @test02_fail
  j       @test03

test02_fail:
  print "[FAIL] :test02:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; saving fp register
test03:
  mov     r10, sp
  mov     fp, 0x321

  enter_s [fp]
  mov     r9, sp     ; save current sp before poping
  pop     r1
  brne    r1, 0x321, @test03_fail
  brne    fp,    r9, @test03_fail
  brne    sp,   r10, @test03_fail
  j       @test04

test03_fail:
  print "[FAIL] :test03:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; saving all registers
  .data
  .align 4
sp_orig: .word 0x0
  .text
test04:
  st      sp   , [sp_orig]          ; ,----- top -----.
  mov     blink, 0x456              ; | blink = 0x456 |
  mov     r13  , 13                 ; | r13   = 13    |
  mov     r14  , 14                 ; | r14   = 14    |
  mov     r15  , 15                 ; | r15   = 15    |
  mov     r16  , 16                 ; | r16   = 16    |
  mov     r17  , 17                 ; | r17   = 17    |
  mov     r18  , 18                 ; | r18   = 18    |
  mov     r19  , 19                 ; | r19   = 19    |
  mov     r20  , 20                 ; | r20   = 20    |
  mov     r21  , 21                 ; | r21   = 21    |
  mov     r22  , 22                 ; | r22   = 22    |
  mov     r23  , 23                 ; | r23   = 23    |
  mov     r24  , 24                 ; | r24   = 24    |
  mov     r25  , 25                 ; | r25   = 25    |
  mov     r26  , 26                 ; | r26   = 26    |
  mov     fp   , 0x789              ; | fp    = 0x789 |
  enter_s [r13-r26, fp, blink]      ; `--- bottom ----'
  mov     r0, sp                    ; save current sp before poping
  pop     r1                        ; blink
  pop     r3                        ; r13
  pop     r4                        ; r14
  pop     r5                        ; r15
  pop     r6                        ; r16
  pop     r7                        ; r17
  pop     r8                        ; r18
  pop     r9                        ; r19
  pop     r10                       ; r20
  pop     r11                       ; r21
  pop     r12                       ; r22
  pop     r13                       ; r23
  pop     r14                       ; r24
  pop     r15                       ; r25
  pop     r16                       ; r26
  pop     r2                        ; fp
  brne    fp,    r0, @test04_fail   ; sp value before all the pops
  brne    r1, 0x456, @test04_fail   ; blink value during save
  brne    r2, 0x789, @test04_fail   ; frame pointer value during save
  brne    r3,    13, @test04_fail   ; stored r13 value
  brne    r4,    14, @test04_fail   ; stored r14 value
  brne    r5,    15, @test04_fail   ; stored r15 value
  brne    r6,    16, @test04_fail   ; stored r16 value
  brne    r7,    17, @test04_fail   ; stored r17 value
  brne    r8,    18, @test04_fail   ; stored r18 value
  brne    r9,    19, @test04_fail   ; stored r19 value
  brne    r10,   20, @test04_fail   ; stored r20 value
  brne    r11,   21, @test04_fail   ; stored r21 value
  brne    r12,   22, @test04_fail   ; stored r22 value
  brne    r13,   23, @test04_fail   ; stored r23 value
  brne    r14,   24, @test04_fail   ; stored r24 value
  brne    r15,   25, @test04_fail   ; stored r25 value
  brne    r16,   26, @test04_fail   ; stored r26 value
  ld      r10, [sp_orig]            ; original sp value spilled
  brne    sp,   r10, @test04_fail   ; original sp value
  j       @test05

test04_fail:
  print "[FAIL] :test04:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; sp's value is not 32-bit aligned --> misaligned exception
; this happens irrelevant of status32.AD bit
test05:
  .equ  TEST05_SP        , 0x111  ; an address which is not 32-bit aligned
  .equ  TEST05_STACK_SIZE, 60     ; saving r13-r26 (14x4) + fp (4)
  .equ  TEST05_EFA       , TEST05_SP - TEST05_STACK_SIZE
  lr      r1, [status32]
  or      r1, r1, 0x80000         ; set AD bit
  mov     r2, @test05_excep_prep
  sr      r1, [erstatus]          ; enable AD bit
  sr      r2, [eret]              ; continue with the test
  rtie

test05_excep_prep:
  lr      r7, [erbta]             ; don't care for erbta
  set_excep_params ecr=MISALIGNED_DATA_ACCESS, \
                   eret=@test05_enter        , \
                   efa=TEST05_EFA            , \
                   erbta=r7                  , \
                   continue=@test05_wrapup   , \
                   test_num=5
  mov     r10, sp                 ; backup sp to restore later
  mov     sp, TEST05_SP           ; an address which is not 32-bit aligned
test05_enter:
  enter_s [r13-r26, fp]           ; just being flamboyant
  print "[FAIL] :test05:"        ; this code must not fall through
  b @endtest

test05_wrapup:
  mov     sp, r10
  lr      r1, [status32]
  and     r1, r1, ~0x80000        ; clear AD bit
  mov     r2, @test06             ; go to next test
  sr      r1, [erstatus]          ; disable AD bit
  sr      r2, [eret]              ; continue with next test
  rtie

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; a delay slot instruction --> illegal instruction sequence exception
; the tricky thing in this test is that gas does not allow us to put
; an "enter_s" in a delay slot (good job there!). however, we work
; around it by writing the opcode at runtime.
test06:
  set_excep_params ecr=ILLEGAL_INSTRUCTION_SEQUENCE, \
                   eret=@test06_delay              , \
                   efa=@test06_delay               , \
                   erbta=@test06_fail              , \
                   continue=@test07                , \
                   test_num=6
  mov     r1, 0xc0e2          ; opcode for enter_s [r13]
  sth     r1, [test06_delay]
  b       @test06_dummy_tb    ; by having 'b' here, it is end of this tb.
test06_dummy_tb:              ; so this one will be decoded after mutation.
  b.d     @test06_fail
test06_delay:
  nop_s
  nop_s

test06_fail:
  print "[FAIL] :test06:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; an execution slot instruction --> illegal instruction sequence exception
; TODO (issue #73): enable this after EI_S has been implemented.
test07:
;  mov     r1, @test07_ei_table
;  sr      r1, [ei_base]
;  j       @test07_begin
;
;  .align 4
;test07_ei_table:
;  enter_s [r13]
;
;test07_begin:
;  set_excep_params ecr=ILLEGAL_INSTRUCTION_SEQUENCE, \
;                   eret=@test07_ei_table           , \
;                   efa=@test07_ei_table            , \
;                   erbta=@test07_fail              , \
;                   continue=@test08                , \
;                   test_num=7
;  ei_s    0
;
;test07_fail:
;  print "failed: test07\n"
;  end

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; it is allowed to have "enter_s 0" in delay/execution slot because it is
; like a nop then and is not a multi-cycle instruction.
; TODO (issue #73): enable the ei_check part after EI_S has been done.
test08:
  mov     r1, 0xc0e0          ; opcode for enter_s []
  sth     r1, [test08_delay]
  b       @test08_dummy_tb    ; by having 'b' here, it is end of this tb.
test08_dummy_tb:              ; so this one will be decoded after mutation.
  b.d     @test08_ei_check
test08_delay:
  nop_s                       ; at runtime this is enter_s [] and is ok
  nop_s

  .align 4
test08_ei_table:
  enter_s 0

test08_ei_check:
  ; TODO (issue #73): enable after EI_S is implemented.
  ;mov     r1, @test08_ei_table
  ;sr      r1, [ei_base]
  ;ei_s    0
  ; fall through to the next test

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 32 general registers configured AND u[3:0] > 14 (r13-r26)
;                           OR
; 16 general registers configured AND u[3:0] > 3 (r13-r15)
; --> illegal instruction exception
; TODO (issue #52): this test case must be executed with
;                   -global cpu.num-regs=16.
test09:
;  lr      r7, [bta]               ; don't care for erbta (use current bta)
;  set_excep_params ecr=ILLEGAL_INSTRUCTION, \
;                   eret=@test09_big_u3    , \
;                   efa=@test09_big_u3     , \
;                   erbta=r7               , \
;                   continue=@test10       , \
;                   test_num=9
;test09_big_u3:      ; enter_s encoding : 1100 00UU 111u uuu0
;  enter_s 4
;
;test09_fail:
;  print "failed: test09\n"
;  end
;;;;;;;;;;;;;;;;;;;;;;;;;;;; \ Enter Tests / ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;; / Leave Tests \ ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; u7=0 leads to doing nothing
test10:
  mov     r10, sp
  leave_s 0
  brne    sp,  r10, @test10_fail
  j       @test11

test10_fail:
  print "[FAIL] :test10:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; restoring first 3 general purpose registers
test11:
  mov     r10, sp       ; ,--- top ---.
  mov     r3, 113       ; | r13 = 113 |
  mov     r4, 114       ; | r14 = 114 |
  mov     r5, 115       ; | r15 = 115 |
  push    r5            ; `-- bottom -'
  push    r4
  push    r3
  leave_s [r13-r15]
  brne    r13, 113, @test11_fail
  brne    r14, 114, @test11_fail
  brne    r15, 115, @test11_fail
  brne    sp , r10, @test11_fail
  j       @test12

test11_fail:
  print "[FAIL] :test11:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; restoring blink register
test12:
  mov     r10, sp
  mov     r1, 0x10123
  push    r1
  leave_s [blink]
  brne    blink, 0x10123, @test12_fail
  brne    sp   ,     r10, @test12_fail
  j       @test13

test12_fail:
  print "[FAIL] :test12:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; restoring fp register
test13:
  mov     r10, sp
  mov     r1, 0x11321
  push    r1
  mov     fp, sp            ; fp is pointing current frame now
  mov     sp, 0x4009        ; botch sp
  leave_s [fp]              ; 'leave_s' must look into fp for restoring
  brne    fp, 0x11321, @test13_fail
  brne    sp,     r10, @test13_fail
  j       @test14

test13_fail:
  print "[FAIL] :test13"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; jumping to what blink holds
test14:
  mov     r10, sp
  mov     blink, @test14_cont
  leave_s [pcl]                     ; jump to whatever blink points to
  j       @test14_fail              ; this should not be reached
test14_cont:
  brne    sp, r10, @test14_fail
  j       @test15

test14_fail:
  print "[FAIL] :test14:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; restoring first 3 general purpose registers
test15:
  mov     r0 , sp                   ; ,--------- top ----------.
  mov     r1 , @test15_return       ; | blink = @test15_return |
  mov     r3 , 213                  ; | r3    = 213            |
  mov     r4 , 214                  ; | r4    = 214            |
  mov     r5 , 215                  ; | r5    = 215            |
  mov     r6 , 216                  ; | r6    = 216            |
  mov     r7 , 217                  ; | r7    = 217            |
  mov     r8 , 218                  ; | r8    = 218            |
  mov     r9 , 219                  ; | r9    = 219            |
  mov     r10, 220                  ; | r10   = 220            |
  mov     r11, 221                  ; | r11   = 221            |
  mov     r12, 222                  ; | r12   = 222            |
  mov     r13, 223                  ; | r13   = 223            |
  mov     r14, 224                  ; | r14   = 224            |
  mov     r15, 225                  ; | r15   = 225            |
  mov     r16, 226                  ; | r16   = 226            |
  mov     r2, 0x14456               ; | fp    = 0x14456        |
  push    r2                        ; `-------- bottom --------'
  push    r16
  push    r15
  push    r14
  push    r13
  push    r12
  push    r11
  push    r10
  push    r9
  push    r8
  push    r7
  push    r6
  push    r5
  push    r4
  push    r3
  push    r1
  mov     fp, sp
  mov     sp, 0x1337                ; both sp again
  leave_s [r13-r26, fp, blink, pcl] ; restore and do everything
  j       @test15_fail
test15_return:
  brne    sp   ,             r0, @test15_fail
  brne    blink, @test15_return, @test15_fail
  brne    r13  ,            213, @test15_fail
  brne    r14  ,            214, @test15_fail
  brne    r15  ,            215, @test15_fail
  brne    r16  ,            216, @test15_fail
  brne    r17  ,            217, @test15_fail
  brne    r18  ,            218, @test15_fail
  brne    r19  ,            219, @test15_fail
  brne    r20  ,            220, @test15_fail
  brne    r21  ,            221, @test15_fail
  brne    r22  ,            222, @test15_fail
  brne    r23  ,            223, @test15_fail
  brne    r24  ,            224, @test15_fail
  brne    r25  ,            225, @test15_fail
  brne    r26  ,            226, @test15_fail
  brne    fp   ,        0x14456, @test15_fail
  j       @test16

test15_fail:
  print "[FAIL] :test15:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; sp's value is not 32-bit aligned --> misaligned exception
; this happens irrelevant of status32.AD bit
test16:
  .equ  TEST16_FP, 0x777  ; an address which is not 32-bit aligned
  lr      r1, [status32]
  or      r1, r1, 0x80000         ; set AD bit
  mov     r2, @test16_excep_prep
  sr      r1, [erstatus]          ; enable AD bit
  sr      r2, [eret]              ; continue with the test
  rtie

test16_excep_prep:
  lr      r7, [erbta]             ; don't care for erbta
  set_excep_params ecr=MISALIGNED_DATA_ACCESS, \
                   eret=@test16_enter        , \
                   efa=TEST16_FP             , \
                   erbta=r7                  , \
                   continue=@test16_wrapup   , \
                   test_num=16
  mov     r10, sp                 ; backup sp to restore later
  mov     fp, TEST16_FP           ; an address which is not 32-bit aligned
test16_enter:
  leave_s [r13-r26, fp]           ; first fp's value is put into sp
  print "[FAIL] :test16:"        ; this code must not fall through
  b @endtest

test16_wrapup:
  mov     sp, r10
  lr      r1, [status32]
  and     r1, r1, ~0x80000        ; clear AD bit
  mov     r2, @test17             ; go to next test
  sr      r1, [erstatus]          ; disable AD bit
  sr      r2, [eret]              ; continue with next test
  rtie

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; a delay slot instruction --> illegal instruction sequence exception
; the tricky thing in this test is that gas does not allow us to put
; an "leave_s" in a delay slot (good job there!). however, we work
; around it by writing the opcode at runtime.
test17:
  set_excep_params ecr=ILLEGAL_INSTRUCTION_SEQUENCE, \
                   eret=@test17_delay              , \
                   efa=@test17_delay               , \
                   erbta=@test17_fail              , \
                   continue=@test18                , \
                   test_num=17
  mov     r1, 0xc0c2          ; opcode for leave_s [13]
  sth     r1, [test17_delay]
  b       @test17_dummy_tb    ; by having 'b' here, it is end of this tb.
test17_dummy_tb:              ; so this one will be decoded after mutation.
  b.d     @test17_fail
test17_delay:
  nop_s
  nop_s

test17_fail:
  print "[FAIL] :test17:"
  b @endtest

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; an execution slot instruction --> illegal instruction sequence exception
; TODO (issue #73): enable this after EI_S has been implemented.
test18:
;  mov     r1, @test18_ei_table
;  sr      r1, [ei_base]
;  j       @test18_begin
;
;  .align 4
;test18_ei_table:
;  leave_s [r13]
;
;test18_begin:
;  set_excep_params ecr=ILLEGAL_INSTRUCTION_SEQUENCE, \
;                   eret=@test18_ei_table           , \
;                   efa=@test18_ei_table            , \
;                   erbta=@test18_fail              , \
;                   continue=@test19                , \
;                   test_num=18
;  ei_s    0
;
;test18_fail:
;  print "[FAIL] : test18\n"
;  end

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; it is allowed to have "leave_s 0" in delay/execution slot because it is
; like a nop then and is not a multi-cycle instruction.
; TODO (issue #73): enable the ei_check part after EI_S has been done.
test19:
  mov     r1, 0xc0c0          ; opcode for leave_s []
  sth     r1, [test19_delay]
  b       @test19_dummy_tb    ; by having 'b' here, it is end of this tb.
test19_dummy_tb:              ; so this one will be decoded after mutation.
  b.d     @test19_ei_check
test19_delay:
  nop_s                       ; at runtime this is leave_s [] and is ok
  nop_s

  .align 4
test19_ei_table:
  leave_s 0

test19_ei_check:
  ; TODO (issue #73): enable after EI_S is implemented.
  ;mov     r1, @test19_ei_table
  ;sr      r1, [ei_base]
  ;ei_s    0
  ; fall through to the next test

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 32 general registers configured AND u[3:0] > 14 (r13-r26)
;                           OR
; 16 general registers configured AND u[3:0] > 3 (r13-r15)
; --> illegal instruction exception
; TODO (issue #52): this test case must be executed with
;                   -global cpu.num-regs=16.
test20:
;  lr      r7, [bta]               ; don't care for erbta (use current bta)
;  set_excep_params ecr=ILLEGAL_INSTRUCTION, \
;                   eret=@test20_big_u3    , \
;                   efa=@test20_big_u3     , \
;                   erbta=r7               , \
;                   continue=@test21       , \
;                   test_num=20
;test20_big_u3:      ; leave_s encoding : 1100 0UUU 110u uuu0
;  leave_s 4
;
;test20_fail:
;  print "[FAIL] : test20\n"
;  b @endtest
;;;;;;;;;;;;;;;;;;;;;;;;;;;; \ Leave Tests / ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;; / Enter/Leave Test \ ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; testing enter_s/leave_s together full fledged.
test21:
  mov     r0 , sp                   ; original sp
  mov     r13, 0x80000013
  mov     r14, 0x80000014
  mov     r15, 0x80000015
  mov     r16, 0x80000016
  mov     r17, 0x80000017
  mov     r18, 0x80000018
  mov     r19, 0x80000019
  mov     r20, 0x8000001a
  mov     r21, 0x8000001b
  mov     r22, 0x8000001c
  mov     r23, 0x8000001d
  mov     r24, 0x8000001e
  mov     r25, 0x8000001f
  mov     r26, 0x80000020
  mov     r27, 0x88888888           ; fp
  mov     r31, @test21_verify       ; blink

  enter_s [r13-r26, fp, blink]
  ; botching all except for fp. it's already changed to current sp.
  breq    r28, 0x88888888, @test21_fail   ; sanity check that fp changed
  mov     r1 , 0xdeadbeef
  mov     r13, r1
  mov     r14, r1
  mov     r15, r1
  mov     r16, r1
  mov     r17, r1
  mov     r18, r1
  mov     r19, r1
  mov     r20, r1
  mov     r21, r1
  mov     r22, r1
  mov     r23, r1
  mov     r24, r1
  mov     r25, r1
  mov     r26, r1
  mov     r28, r1                   ; botch sp
  mov     r31, r1                   ; botch blink
  leave_s [r13-r26, fp, blink, pcl]
  j       @test21_fail

test21_verify:
  brne    r13,     0x80000013, @test21_fail
  brne    r14,     0x80000014, @test21_fail
  brne    r15,     0x80000015, @test21_fail
  brne    r16,     0x80000016, @test21_fail
  brne    r17,     0x80000017, @test21_fail
  brne    r18,     0x80000018, @test21_fail
  brne    r19,     0x80000019, @test21_fail
  brne    r20,     0x8000001a, @test21_fail
  brne    r21,     0x8000001b, @test21_fail
  brne    r22,     0x8000001c, @test21_fail
  brne    r23,     0x8000001d, @test21_fail
  brne    r24,     0x8000001e, @test21_fail
  brne    r25,     0x8000001f, @test21_fail
  brne    r26,     0x80000020, @test21_fail
  brne    r27,     0x88888888, @test21_fail
  brne    r28,             r0, @test21_fail
  breq    r31, @test21_verify, @valhalla

test21_fail:
  print "[FAIL] :test20:"
  b @endtest
;;;;;;;;;;;;;;;;;;;;;;;;; \ Enter/Leave Test / ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

valhalla:
	print "[PASS]"
endtest:
	print " enter/leave\n"
  end


; vim: set syntax=asm ts=2 sw=2 et:
