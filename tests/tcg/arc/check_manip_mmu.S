; check_manip_mmu.S
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
  .global instruction_error
  .type   EV_TLBMissI, @function
  .type   EV_TLBMissD, @function
  .type   EV_ProtV, @function
  .type   instruction_error, @function
EV_TLBMissI:
EV_TLBMissD:
EV_ProtV:
instruction_error:
  ld  r11, [handler]
  j   [r11]

; macro:      set_except_handler
; regs used:  r11
;
; This macro writes the provided ADDR to a temporary place
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

; Test case 1:
; Reading from a virtual address that has no entry in TLB. This will
; cause a TLBMissD exception. In return, the exception routine handler
; will add the corresponding entry:
; ,-----------------.------------------.----------------------.
; | virtual address | physical address | (kernel) permissions |
; |-----------------+------------------+----------------------|
; | 0x1337_1334     | 0x7331_1334      | R--                  |
; `-----------------^------------------^----------------------'
; After returning from the exception, the "ld" should go OK.
; Then there comes a write ("st") that will trigger a ProtV exception.
; This time, we allow writing as well:
; ,-----------------.------------------.----------------------.
; | virtual address | physical address | (kernel) permissions |
; |-----------------+------------------+----------------------|
; | 0x1337_1334     | 0x7331_1334      | RW-                  |
; `-----------------^------------------^----------------------'
; the "st" to the same address should go fine.
  .equ T1_VIRT_ADDR, 0x13371334     ; the virtual address; word aligned
  .equ T1_PHYS_ADDR, 0x73311334     ; the physical address (same page offset as VA)
  .equ T1_DATA_1   , 0x00BADB07     ; the test value to read and verify
  .equ T1_DATA_2   , 0x00B07BAD     ; the test value to write and verify
  .equ T1_PD0   , ((T1_VIRT_ADDR & PAGE_NUMBER_MSK) | REG_PD0_GLOBAL | REG_PD0_VALID)
  .equ T1_PD1_R , ((T1_PHYS_ADDR & PAGE_NUMBER_MSK) | REG_PD1_KRNL_R)
  .equ T1_PD1_RW, (T1_PD1_R | REG_PD1_KRNL_W)
  mmu_prep_test_case
  mov     r2, 0               ; exception handler counter
  mov     r1, T1_DATA_1       ; plant the data ...
  st      r1, [T1_PHYS_ADDR]  ; ... into the physical address
  set_except_handler @test_1_except_handler
  mmu_enable
test_1_ld:
  ld      r0, [T1_VIRT_ADDR]  ; TLBMissD causing instruction
  cmp     r0, T1_DATA_1
  bne     @fail
  mov     r0, T1_DATA_2
test_1_st:
  st      r0, [T1_VIRT_ADDR]  ; TLBProtV causing instruction
  mmu_disable                 ; MMU bye-bye!
  ld      r1, [T1_PHYS_ADDR]  ; Load the final destination of "st"
  cmp     r1, T1_DATA_2       ; was it written successfuly?
  bne     @fail
  b       @test_1_end
test_1_except_handler:
  add_s   r2, r2, 1
  cmp     r2, 1               ; TLBMissD while loading?
  bne     @1f
  lr      r11, [ecr]
  cmp     r11, TLB_MISS_D_READ; TLBMissD during a load?
  bne     @fail
  lr      r11, [eret]
  cmp     r11, @test_1_ld     ; instruction causing the exception
  lr      r11, [efa]
  cmp     r11, T1_VIRT_ADDR   ; faulty address is correct?
  jne     @fail
  mov     r11, 0
  sr      r11, [efa]          ; clearing EFA
  mmu_tlb_insert T1_PD0, T1_PD1_R
  rtie
1:
  cmp     r2, 2               ; ProtV during write?
  bne     @fail
  lr      r11, [ecr]
  cmp     r11, 0x60208        ; ProtV from MMU during a write?
  bne     @fail
  lr      r11, [eret]
  cmp     r11, @test_1_st     ; instruction causing the exception
  lr      r11, [efa]
  cmp     r11, T1_VIRT_ADDR   ; faulty address is correct?
  jne     @fail
  mmu_tlb_insert T1_PD0, T1_PD1_RW
  rtie
test_1_end:
  ; Fall through

; Test case 2
; Load a double word data straddled over two consecutive virtual pages:
; ,-------------------------------.,-----------------------------.
; |               ... x0 x1 x2 x3 || x4 x5 x6 x7 x8 ...          |
; `-------------------------------'`-----------------------------'
;  virt=0x0050_2000..0x0050_4000      virt=0x0050_4000..0x050_6000
;
; Only the first page has an entry in TLB:
; ,-----------------.------------------.----------------------.
; | virtual address | physical address | (kernel) permissions |
; |-----------------+------------------+----------------------|
; | 0x0050_2000     | 0x3000_8000      | R--                  |
; `-----------------^------------------^----------------------'
;
; An "ldd" from the last 4 byte of the first page will span to
; the second page. This will lead to an exception (TLBMissD).
  .equ T2_VIRT_ADDR, 0x00502000      ; virtual page address
  .equ T2_PHYS_ADDR, 0x30008000      ; physical page address
  .equ T2_ADDR_OFS,  0x00001FFC      ; the offset in the page
  .equ T2_PD0, ((T2_VIRT_ADDR+T2_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD0_GLOBAL | REG_PD0_VALID)
  .equ T2_PD1, ((T2_PHYS_ADDR             & PAGE_NUMBER_MSK) | REG_PD1_KRNL_R)

  mmu_prep_test_case
  mov     r2, 0               ; exception handler counter
  set_except_handler @test_2_except_handler
  mmu_tlb_insert T2_PD0, T2_PD1
  mmu_enable
test_2_ldd:
  ldd     r0, [T2_VIRT_ADDR+T2_ADDR_OFS]
  cmp     r2, 1
  bne     @fail
  b       @test_2_end         ; success!
test_2_except_handler:
  add     r2, r2, 1           ; increase the counter
  lr      r11, [ecr]
  cmp     r11, 0x50100        ; TLBMissD during a load?
  bne     @fail
  lr      r11, [eret]
  cmp     r11, @test_2_ldd
  jne     @fail
  lr      r11, [efa]
  cmp     r11, T2_VIRT_ADDR+T2_ADDR_OFS+4 ; beginning of next page
  jne     @fail
  mmu_disable
  rtie
test_2_end:
  ; Fall through

; Test case 3
; Load a data word (0x12345678) straddled over two consecutive
; virtual pages:
; ,--------------------.,--------------------.
; |           ... 0x78 || 0x56 0x34 0x12 ... |
; `--------------------'`--------------------'
;  virt=0x0000...0x2000  virt=0x2000...0x4000
;
; Only the first page has an entry in TLB:
; ,-----------------.------------------.----------------------.
; | virtual address | physical address | (kernel) permissions |
; |-----------------+------------------+----------------------|
; | 0x0000_0000     | 0x7000_0000      | R--                  |
; `-----------------^------------------^----------------------'
;
; An "ld" (word-sized) from the last byte of the first page will
; span to the first 3 bytes of the second page. This will lead
; to an exception (TLBMissD). The exception routine will add the
; entry for the second page:
; ,-----------------.------------------.----------------------.
; | virtual address | physical address | (kernel) permissions |
; |-----------------+------------------+----------------------|
; | 0x0000_0000     | 0x7000_0000      | R--                  |
; | 0x0000_2000     | 0x6000_2000      | R--                  |
; `-----------------^------------------^----------------------'
;
; And in the end, we must have fetched the data (0x12345678).
; To make the test realistic, the physical page addresses are not
; consecutive as opposed to their virtual counter parts.
; The alignment check should be disabled for this test.
  .equ T3_VIRT_ADDR_1, 0x00000000      ; two virtual page addresses ...
  .equ T3_VIRT_ADDR_2, 0x00002000      ; ... that are consecutive.
  .equ T3_PHYS_ADDR_1, 0x70000000      ; two physical page addresses ...
  .equ T3_PHYS_ADDR_2, 0x60002000      ; ... that are inconsecutive.
  .equ T3_ADDR_1_OFS,  0x00001FFF      ; the offset in the first pages.
  .equ T3_PD0_ENT1, ((T3_VIRT_ADDR_1+T3_ADDR_1_OFS & PAGE_NUMBER_MSK) | REG_PD0_GLOBAL | REG_PD0_VALID)
  .equ T3_PD1_ENT1, ((T3_PHYS_ADDR_1               & PAGE_NUMBER_MSK) | REG_PD1_KRNL_R)
  .equ T3_PD0_ENT2, ((T3_VIRT_ADDR_2               & PAGE_NUMBER_MSK) | REG_PD0_GLOBAL | REG_PD0_VALID)
  .equ T3_PD1_ENT2, ((T3_PHYS_ADDR_2               & PAGE_NUMBER_MSK) | REG_PD1_KRNL_R)
  mmu_prep_test_case
  ; Plant data at the physical addresses
  mov     r1, 0x12345678
  stb     r1, [T3_PHYS_ADDR_1+T3_ADDR_1_OFS]  ; 0x78 at the end of first page
  lsr8    r1, r1
  sth     r1, [T3_PHYS_ADDR_2]                ; 0x56 0x34 at the beginning of 2nd page
  lsr16   r1, r1
  stb     r1, [T3_PHYS_ADDR_2+2]              ; 0x12 The 3rd byte on the 2nd page
  mov     r1, 0                               ; exception handler counter
  disable_alignment
  set_except_handler @test_3_except_handler
  mmu_tlb_insert T3_PD0_ENT1, T3_PD1_ENT1
  mmu_enable
  ; Exception-causing instruction
test_3_ld:
  ld      r0, [T3_VIRT_ADDR_1+T3_ADDR_1_OFS]
  mov     r3, 0x12345678
  cmp     r0, r3
  bne     @fail
  cmp     r1, 1
  bne     @fail
  b       @test_3_end         ; success!
test_3_except_handler:
  add     r1, r1, 1           ; increase the counter
  lr      r11, [ecr]
  cmp     r11, 0x50100        ; TLBMissD during a load?
  bne     @fail
  lr      r11, [eret]
  cmp     r11, @test_3_ld
  jne     @fail
  lr      r11, [efa]
  cmp     r11, @T3_VIRT_ADDR_2
  jne     @fail
  mmu_tlb_insert T3_PD0_ENT2, T3_PD1_ENT2
  rtie
test_3_end:
  ; Fall through

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
  mov     r1, T4_VIRT_ADDR+T4_ADDR_OFS  ; jump to the copied code
  j       [r1]
  ; Have embedded code word-aligned at a place where it will be put.
  .align 4
test_4_embedded_code_start:
  nop
  b.d     @test_4_virt_finish
  add     r1, r0, r0
  nop
  j       @fail
  nop
test_4_virt_finish:
  j       @test_4_end
test_4_embedded_code_end:
; Exception routine that will add entry for the second page
test_4_except_handler:
  lr      r11, [ecr]
  cmp     r11, TLB_MISS_I
  bne     @fail
  lr      r11, [eret]
  cmp     r11, @T4_VIRT_ADDR+0x2000   ; Beginning of second page?
  jne     @fail
  lr      r11, [efa]
  cmp     r11, @T4_VIRT_ADDR+0x2000   ; Beginning of second page?
  jne     @fail
  lr      r11, [bta]
  cmp     r11, @T4_VIRT_ADDR+T4_ADDR_OFS+T4_size-8    ; BTA correct?
  jne     @fail
  lr      r11, [erbta]
  cmp     r11, @T4_VIRT_ADDR+T4_ADDR_OFS+T4_size-8    ; ERBTA correct?
  jne     @fail
  mmu_tlb_insert T4_PD0+0x2000, T4_PD1+0x2000
  rtie
test_4_end:
   ; Fall through

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
; bta   = old value (not updated)
; blink = old value (not updated)
  .equ T5_VIRT_ADDR, 0x00602000      ; virtual page address
  .equ T5_PHYS_ADDR, 0xA0008000      ; physical page address
  .equ T5_ADDR_OFS,  0x00001FF8      ; the offset in the page
  .equ T5_PD0, ((T5_VIRT_ADDR+T5_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD0_GLOBAL | REG_PD0_VALID)
  .equ T5_PD1, ((T5_PHYS_ADDR+T5_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD1_KRNL_R | REG_PD1_KRNL_E)
  .equ T5_size, test_5_embedded_code_end - test_5_embedded_code_start

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
  mov     r5, blink         ; remember the old blink value
  mov     r1, T5_VIRT_ADDR+T5_ADDR_OFS  ; jump to the copied code
  j       [r1]
  ; Have embedded code word-aligned at a place where it will be put.
  .align 4
test_5_embedded_code_start:
  nop
  bl.d    @test_5_virt_finish
  add     r1, r0, r0
  nop
  j       @fail
  nop
test_5_virt_finish:
  j       @test_5_end
test_5_embedded_code_end:
; Exception routine that will add entry for the second page
test_5_except_handler:
  lr      r11, [ecr]
  cmp     r11, TLB_MISS_I
  bne     @fail
  lr      r11, [eret]
  cmp     r11, @T5_VIRT_ADDR+0x2000-4 ; Last instruction of the first page (bl)?
  jne     @fail
  lr      r11, [efa]
  cmp     r11, @T5_VIRT_ADDR+0x2000   ; Beginning of second page?
  jne     @fail
  lr      r11, [bta]
  cmp     r11, r4                     ; BTA not updated? (still old?)
  jne     @fail
  lr      r11, [erbta]
  cmp     r11, r4                     ; ERBTA same as not-updated-BTA?
  mov     r11, blink
  cmp     r11, r5                     ; BLINK not updated? (still old?)
  jne     @fail
  mmu_tlb_insert T5_PD0+0x2000, T5_PD1+0x2000
  rtie
test_5_end:
   ; Fall through

; Test case 6: BLINK register must be updated immediately after "BL".
  mmu_prep_test_case
  bl.d    @test_6_branch_taken
  mov     r0, blink
test_6_after_delay_slot:
  b       @fail
  .align 4
test_6_branch_taken:
  mov     r1, @test_6_after_delay_slot
  cmp     r0, r1
  bne     @fail

; Test case 7: BTA register must be updated immediately after "BL".
  mmu_prep_test_case
  bl.d    @test_7_branch_taken
  lr      r0, [bta]
  b       @fail
  .align 4
test_7_branch_taken:
  mov     r1, @test_7_branch_taken
  cmp     r0, r1
  bne     @fail

;; Test case 8: Exceptions other than TLBMissI for the delay slot of BL
;; In this case, such exceptions are deep in decoding pipeline and should
;; cause a normal exception like any other instructions, where ERET is
;; pointing to the delay slot and not the BL instruction, like the previous
;; tests.
;  mmu_prep_test_case
;  set_except_handler @test_8_except_handler
;  bl.d    @test_8_end
;test_8_delay_slot:
;  lr      r0, [blink]              ; InstructionError
;  b       @fail
;; Exception routine that will add entry for the second page
;test_8_except_handler:
;  lr      r11, [ecr]
;  cmp     r11, ILLEGAL_INSTRUCTION
;  bne     @fail
;  lr      r11, [eret]
;  cmp     r11, @test_8_delay_slot
;  jne     @fail
;  lr      r11, [efa]
;  cmp     r11, @test_8_delay_slot
;  jne     @fail
;  lr      r11, [erbta]
;  cmp     r11, @test_8_end
;  jne     @fail
;  lr      r11, [bta]
;  cmp     r11, @test_8_end
;  jne     @fail
;  sr      r11, [eret]             ; Get out of delay slot by jumping to BTA
;  lr      r11, [erstatus]
;  bclr    r11, r11, 6             ; Clear delay slot execution flag
;  sr      r11, [erstatus]
;  rtie
;  b       @fail
;  .align 4
;test_8_end:
;  ; Fall through

; Test case 9
; Like test case 5, but the CC is false here. Although, there is no need
; for the calculation of BLINK and the _early_ decode of delay slot
; instruction, still TLBMissI exception for the delay slot instruction
; happens during the execution of "BLne.D". This is how the hardware
; works.
; ecr   = 0x40000 (TLBMissI)
; eret  = bl_insn_addr
; efa   = delay_insn_addr
; bta   = old value (not updated)
; blink = old value (not updated)
  .equ T9_VIRT_ADDR, 0x00606000      ; virtual page address
  .equ T9_PHYS_ADDR, 0xA000A000      ; physical page address
  .equ T9_ADDR_OFS,  0x00001FF4      ; the offset in the page
  .equ T9_PD0, ((T9_VIRT_ADDR+T9_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD0_GLOBAL | REG_PD0_VALID)
  .equ T9_PD1, ((T9_PHYS_ADDR+T9_ADDR_OFS & PAGE_NUMBER_MSK) | REG_PD1_KRNL_R | REG_PD1_KRNL_E)
  .equ T9_size, test_9_embedded_code_end - test_9_embedded_code_start

  mmu_prep_test_case
  ; Copy the embedded code into physical page
  xor_s   r3, r3, r3
  mov     r0, @test_9_embedded_code_start
  mov     r1, @T9_PHYS_ADDR+T9_ADDR_OFS
test_9_copy:
  ldb.ab  r2, [r0, 1]
  stb.ab  r2, [r1, 1]
  add_s   r3, r3, 1
  cmp     r3, T9_size
  blt     @test_9_copy
  ; Add MMU
  set_except_handler @test_9_except_handler
  mmu_tlb_insert T9_PD0, T9_PD1
  mmu_enable
  lr      r4, [bta]                     ; remember the old bta value
  mov     r1, T9_VIRT_ADDR+T9_ADDR_OFS  ; jump to the copied code
  j       [r1]
  ; Have embedded code word-aligned at a place where it will be put.
  .align 4
test_9_embedded_code_start:
  add.f   0, 0, 0
  blne.d  @fail
  add     r0, r0, r0
  j       @test_9_end
test_9_embedded_code_end:
; Exception routine that will add entry for the second page
test_9_except_handler:
  lr      r11, [ecr]
  cmp     r11, TLB_MISS_I
  bne     @fail
  lr      r11, [eret]
  cmp     r11, @T9_VIRT_ADDR+0x2000-4 ; Last instruction of the first page (blne.d)?
  jne     @fail
  lr      r11, [efa]
  cmp     r11, @T9_VIRT_ADDR+0x2000   ; Beginning of second page?
  jne     @fail
  lr      r11, [bta]
  cmp     r11, r4                     ; BTA not updated? (still old?)
  jne     @fail
  lr      r11, [erbta]
  cmp     r11, r4                     ; ERBTA same as not updated BTA?
  jne     @fail
  mmu_tlb_insert T9_PD0+0x2000, T9_PD1+0x2000
  rtie
test_9_end:
   ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Reporting ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

valhalla:
  print "[PASS]"
  b @1f

.align 4
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
