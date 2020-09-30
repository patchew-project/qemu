; check_manip_5_mmu.S
;
; Tests for MMU: manipulate MMU table in exception routines.
; If the test fails, check the end of this file for how to troubleshoot.
; The running code for this test needs to be in address range >= 0x8000_0000.

  .include "macros.inc"
  .include "mmu.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;; Bunch of constants ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

  .equ INT_VECT_ADDRESS, 0x80000000 ; physical address for IVT
  .equ STATUS32_AD_BIT , 19         ; Alignment Disable bit
  ; courtesy of macros.inc and mmu.inc
  .extern REG_IVT_BASE
  .extern PAGE_NUMBER_MSK
  .extern REG_PD0_GLOBAL
  .extern REG_PD0_VALID
  .extern REG_PD1_KRNL_W

;;;;;;;;;;;;;;;;;;;;;;;;; Exception related code ;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Handler of the day.
  .align 4
handler : .word 0x0

; An exception handler routine that merely jumps to whatever address
; it was told to by the test. See set_except_handler macro. This
; requires ivt.S file to be compiled and linked.
  .align 4
  .global EV_TLBMissI
  .global EV_TLBMissD
  .global EV_ProtV
  .type   EV_TLBMissI, @function
  .type   EV_TLBMissD, @function
  .type   EV_ProtV, @function
EV_TLBMissI:
EV_TLBMissD:
EV_ProtV:
  ld  r11, [handler]
  j   [r11]

; macro:      set_except_handler
; regs used:  r11
;
; This macro writes the provided ADDR to a temporary place holder
; that later the exception handler routine will jump to.
.macro set_except_handler   addr
  mov  r11, \addr
  st   r11, [handler]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Tests ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Let the tests begin
  start
  ; use physicall address range for handling exceptions (ivt)
  mov   r0, INT_VECT_ADDRESS
  sr    r0, [REG_IVT_BASE]

; Test case 5
; Like previous test but with a "branch and link". This is even trickier.
; BL needs to decode the delay instruction to know its length. It uses
; this information to determine what value should "BLINK" register hold.
; Below is the pertinent semantic:
;
;   delay_insn_addr = bl_insn_addr + bl_insn_len
;   delay_insn_len  = decode(delay_insn_addr)
; BLINK = bl_insn_addr + bl_insn_len + delay_insn_len
;
; If the "delay slot" instruction is on a missing page, a TLBMissI is
; raised during "decode(delay_insn_addr)". This all happens while the
; "BL" instruction is being handled (and not the delay slot):
;
; ecr   = 0x40000 (TLBMissI)
; eret  = bl_insn_addr   --> for previous test, this is delay_insn_addr
; efa   = delay_insn_addr
; blink = old value (not updated)
  .equ T5_VIRT_ADDR, 0x00602000      ; virtual page address
  .equ T5_PHYS_ADDR, 0xA0008000      ; physical page address
  .equ T5_ADDR_OFS,  0x00001FF8      ; the offset in the page
  .equ T5_PD0, ((T5_VIRT_ADDR+T5_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD0_GLOBAL | REG_PD0_VALID)
  .equ T5_PD1, ((T5_PHYS_ADDR+T5_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD1_KRNL_R | REG_PD1_KRNL_E)
  .equ T5_size, test_5_embedded_code_end - test_5_embedded_code_start

  mmu_prep_test_case
  mmu_prep_test_case
  ; Copy the embedded code into physical page
  xor_s   r3, r3, r3
  mov     r0, @test_5_embedded_code_start
  mov     r1, @T5_PHYS_ADDR+T5_ADDR_OFS
test_5_copy:
  ldb.ab  r2, [r0, 1]
  stb.ab  r2, [r1, 1]
  add_s   r3, r3, 1
  cmp     r3, T5_size
  blt     @test_5_copy
  ; Add MMU
  set_except_handler @test_5_except_handler
  mmu_tlb_insert T5_PD0, T5_PD1
  mmu_enable
  lr      r4, [bta]         ; remember the old bta value
  mov     r0, 0x80000000    ; will be used by the code to be executed
  mov     r1, T5_VIRT_ADDR+T5_ADDR_OFS  ; jump to the copied code
  j       [r1]
  ; Have embedded code word-aligned at a place where it will be put.
  .align 4
test_5_embedded_code_start:
  nop
  bl.d     @test_5_virt_finish
  ld      r1, [r0]
  nop
  j       @fail
  nop
test_5_virt_finish:
  j       @test_5_end
test_5_embedded_code_end:
; Exception routine that will add entry for the second page
test_5_except_handler:
  mmu_prep_test_case_address
  lr      r9, [ecr]
  print_number_hex r9
  cmp     r9, 0x40000                ; TLBMissI?
  bne     @fail
  mmu_prep_test_case_address
  lr      r9, [eret]
  print_number_hex r9
  cmp     r9, @T5_VIRT_ADDR+0x2000-4 ; Beginning of second page?
  jne     @fail
  mmu_prep_test_case_address
  lr      r9, [efa]
  print_number_hex r9
  cmp     r9, @T5_VIRT_ADDR+0x2000   ; Beginning of second page?
  jne     @fail
  mmu_prep_test_case_address
  lr      r9, [bta]
  print_number_hex r9
  cmp     r9, r4                     ; BTA not updated? (still old?)
  jne     @fail
  mmu_prep_test_case_address
  lr      r9, [erbta]
  cmp     r9, r4                     ; ERBTA same as not updated BTA?
  jne     @fail
  mmu_tlb_insert T5_PD0+0x2000, T5_PD1+0x2000
  rtie
test_5_end:
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
  ld r0, [mmu_test_nr]
  ;print_number r0
  print "[FAIL]"
1:
  print " MMU: manipulate MMU table in exception routines\n"
  end
