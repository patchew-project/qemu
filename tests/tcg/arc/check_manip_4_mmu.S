; check_manip_4_mmu.S
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
; Test case 4
; Straddle a "branch" and its "delay slot" on two consecutive pages.
; The first virtual page has an entry in TLB, but the second one (which
; the delay slot is on) does not. We want to see when fetching the delay
; slot causes a TLBMissI, things will go back smoothly.
;
; first page with TLB entry
; ,-----.
; | ... |
; | nop |
; | b.d |  branch instruction as the last instruction of the page
; `-----'
; ,-----.
; | dly |  delay instruction on the next page
; | ... |
; |     |
; `-----'
; second page without TLB entry
  .equ T4_VIRT_ADDR, 0x00402000      ; virtual page address
  .equ T4_PHYS_ADDR, 0x90008000      ; physical page address
  .equ T4_ADDR_OFS,  0x00001FF8      ; the offset in the page
  .equ T4_PD0, ((T4_VIRT_ADDR+T4_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD0_GLOBAL | REG_PD0_VALID)
  .equ T4_PD1, ((T4_PHYS_ADDR+T4_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD1_KRNL_R | REG_PD1_KRNL_E)
  .equ T4_size, test_4_embedded_code_end - test_4_embedded_code_start

  mmu_prep_test_case
  ; Copy the embedded code into physical page
  xor_s   r3, r3, r3
  mov     r0, @test_4_embedded_code_start
  mov     r1, @T4_PHYS_ADDR+T4_ADDR_OFS
test_4_copy:
  ldb.ab  r2, [r0, 1]
  stb.ab  r2, [r1, 1]
  add_s   r3, r3, 1
  cmp     r3, T4_size
  blt     @test_4_copy
  ; Add MMU
  set_except_handler @test_4_except_handler
  mmu_tlb_insert T4_PD0, T4_PD1
  mmu_enable
  mov     r0, 0x80000000    ; will be used by the code to be executed
  mov     r1, T4_VIRT_ADDR+T4_ADDR_OFS  ; jump to the copied code
  j       [r1]
  ; Have embedded code word-aligned at a place where it will be put.
  .align 4
test_4_embedded_code_start:
  nop
  b.d     @test_4_virt_finish
  ld      r1, [r0]
  nop
  j       @fail
  nop
test_4_virt_finish:
  j       @test_4_end
test_4_embedded_code_end:
; Exception routine that will add entry for the second page
test_4_except_handler:
  mmu_prep_test_case_address
  lr      r9, [ecr]
  cmp     r9, 0x40000                ; TLBMissI?
  bne     @fail
  mmu_prep_test_case_address
  lr      r9, [eret]
  cmp     r9, @T4_VIRT_ADDR+0x2000   ; Beginning of second page?
  jne     @fail
  mmu_prep_test_case_address
  lr      r9, [efa]
  cmp     r9, @T4_VIRT_ADDR+0x2000   ; Beginning of second page?
  jne     @fail
  mmu_prep_test_case_address
  lr      r9, [bta]
  cmp     r9, @T4_VIRT_ADDR+T4_ADDR_OFS+T4_size-8    ; BTA correct?
  jne     @fail
  mmu_prep_test_case_address
  lr      r9, [erbta]
  cmp     r9, @T4_VIRT_ADDR+T4_ADDR_OFS+T4_size-8    ; ERBTA correct?
  jne     @fail
  mmu_tlb_insert T4_PD0+0x2000, T4_PD1+0x2000
  rtie
test_4_end:
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
