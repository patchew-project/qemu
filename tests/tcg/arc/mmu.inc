
; auxilary registers
.equ REG_PD0     , 0x460  ; TLBPD0
.equ REG_PD1     , 0x461  ; TLBPD1
.equ REG_TLB_INDX, 0x464  ; TLB index
.equ REG_TLB_CMD , 0x465  ; TLB command
.equ REG_PID     , 0x468  ; Process Identity

; exceptions (ecr values)
.equ PROTV_FETCH_MMU, 0x060008
.equ PROTV_READ_MMU , 0x060108
.equ PROTV_WRITE_MMU, 0x060208
.equ PROTV_RW_MMU   , 0x060308

; PID register bit masks
.equ REG_PID_TLB_SET, 0x80000000       ; TLB enable bit in PID
.equ REG_PID_TLB_CLR, ~REG_PID_TLB_SET ; TLB disable bit in PID

; bit masks related to page size
.equ PAGE_INDEX_BITS, 13               ; page size is _assumed_ to be 8 KB
.equ PAGE_SIZE      , 1 << PAGE_INDEX_BITS
.equ PAGE_OFFSET_MSK, PAGE_SIZE - 1
.equ PAGE_NUMBER_MSK, ~PAGE_OFFSET_MSK

; TLBPD0 bit masks
.equ REG_PD0_GLOBAL, 0x100     ; Global bit
.equ REG_PD0_VALID , 0x200     ; Valid bit

; TLBPD1 bit masks
.equ REG_PD1_KRNL_E, 0x10           ; kernel execute
.equ REG_PD1_KRNL_W, 0x20           ; kernel write
.equ REG_PD1_KRNL_R, 0x40           ; kernel read

; TLB commands
.equ TLB_CMD_WRITE   , 0x01           ; write
.equ TLB_CMD_READ    , 0x02           ; read
.equ TLB_CMD_GET_INDX, 0x03           ; get index
.equ TLB_CMD_PROBE   , 0x04           ; probe
.equ TLB_CMD_INSERT  , 0x07           ; insert
.equ TLB_CMD_DELETE  , 0x08           ; delete


.macro  extract_page_number   address
  (address & PAGE_NUMBER_MSK)
.endm


; macro:     mmu_enable
; regs used: r11
;
; enable MMU on ARC HS systems
.macro  mmu_enable
  lr  r11, [REG_PID]
  or  r11, r11, REG_PID_TLB_SET
  sr  r11, [REG_PID]
.endm


; macro:     mmu_disable
; regs used: r11
;
; disable MMU on ARC HS systems
.macro  mmu_disable
  lr  r11, [REG_PID]
  and r11, r11, REG_PID_TLB_CLR
  sr  r11, [REG_PID]
.endm


; macro:     mmu_tlb_insert
; regs used: r11
;
; inserts (TLBPD0, TLBPD1) registers as a TLB entry
.macro mmu_tlb_insert  PD0, PD1
  mov r11, \PD0
  sr  r11, [REG_PD0]
  mov r11, \PD1
  sr  r11, [REG_PD1]
  mov r11, TLB_CMD_INSERT
  sr  r11, [REG_TLB_CMD]
.endm


; macro:     mmu_tlb_delete
; regs used: r11
;
; removes any entry with PD0 as page description
.macro mmu_tlb_delete  PD0, PD1
  mov r11, \PD0
  sr  r11, [REG_PD0]
  mov r11, \PD1
  sr  r11, [REG_PD1]
  mov r11, TLB_CMD_INSERT
  sr  r11, [REG_TLB_CMD]
.endm
; vim: set syntax=asm ts=2 sw=2 et:

;;;;;;;;;;;;;;;;;;;;;;;;;;; Test checking routines ;;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case counter
.data
mmu_test_nr:
  .word 0x0

; Increment the test counter
.macro mmu_prep_test_case
  ld    r13, [mmu_test_nr]
  add_s r13, r13, 1       ; increase test case counter
  st    r13, [mmu_test_nr]
  mmu_disable
  set_except_handler 0x0
  enable_alignment
.endm

; Increment the test counter
.macro mmu_prep_test_case_address
  st    pcl, [mmu_test_nr]
.endm

; Disable alignment so there will be no Misaligned exception
.macro disable_alignment
  lr    r11, [status32]
  bset  r11, r11, STATUS32_AD_BIT
  flag  r11
.endm

; Enable alignment again.
.macro enable_alignment
  lr    r11, [status32]
  bclr  r11, r11, STATUS32_AD_BIT
  flag  r11
.endm
