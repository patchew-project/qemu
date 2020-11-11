  .include "macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; exception facilitators
  .align 4
ecr_ref    : .word 0x0
efa_ref    : .word 0x0
eret_ref   : .word 0x0
cont_addr  : .word 0x0
test_number: .word 0x0

; macro:      set_excep_params
; regs used:  r11
;
; this macro writes the provided parameters to a temporary place holder
; later it will be used by SWI exception routine as a reference
.macro set_excep_params ecr, efa, eret, continue, test_num
  mov  r11, \ecr
  st   r11, [ecr_ref]
  mov  r11, \efa
  st   r11, [efa_ref]
  mov  r11, \eret
  st   r11, [eret_ref]
  mov  r11, \continue
  st   r11, [cont_addr]
  mov  r11, \test_num
  st   r11, [test_number]
.endm

; exception: software interrupt
; regs used: r11, r12
;
; this is a parameterized SWI exception that will check the followings:
; ecr   == ecr_ref
; efa   == efa_ref
; eret  == eret_ref
; if everything passes, it will jump to 'cont_addr' parameter.
; the parameters must be set beforehand using 'set_except_params' macro.
; last but not least, this requires ivt.S file to be compiled and linked.
  .align 4
  .global EV_SWI
  .type EV_SWI, @function
EV_SWI:
  ld   r11, [ecr_ref]
  lr   r12, [ecr]
  brne r12, r11, @exc_fail
  ld   r11, [eret_ref]
  lr   r12, [eret]
  brne r12, r11, @exc_fail
  ld   r11, [efa_ref]
  lr   r12, [efa]
  brne r12, r11, @exc_fail
  ; going back to the given address
  ld   r11, [cont_addr]
  sr   r11, [eret]
  rtie
exc_fail:
	ld   r11, [test_number]
	print "[FAIL] "
  print_number r11
  print ": exception is not sane!\n"
  end

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; let the test code begin
  start

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; check swi_s with no argument
test01:
  set_excep_params ecr      = SOFTWARE_INTERRUPT, \
                   efa      = @test01_swis_addr , \
                   eret     = @test01_swis_addr , \
                   continue = @test02           , \
                   test_num = 0x01

test01_swis_addr:
  swi_s

  assert_eq 0, 1, 1          ; exception must have been raised

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; check swi_s with a u6 immediate
test02:
  .equ INTERRUPT_NUM  , 42
  .equ TEST02_EXCP_REF, SOFTWARE_INTERRUPT | INTERRUPT_NUM
  set_excep_params ecr      = TEST02_EXCP_REF,\
                   efa      = @test02_swis_addr , \
                   eret     = @test02_swis_addr , \
                   continue = @test03           , \
                   test_num = 0x02

test02_swis_addr:
  swi_s INTERRUPT_NUM

  assert_eq 0, 1, 2          ; exception must have been raised

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; check swi
test03:
  set_excep_params ecr      = SOFTWARE_INTERRUPT, \
                   efa      = @test03_swi_addr  , \
                   eret     = @test03_swi_addr  , \
                   continue = @finish           , \
                   test_num = 0x01

test03_swi_addr:
  swi

  assert_eq 0, 1, 3          ; exception must have been raised


finish:
  print "[PASS] Software Interrupt\n"
  end
