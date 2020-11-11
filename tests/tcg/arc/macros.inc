.equ MAX_TESTNAME_LEN, 32
.macro test_name name
    .data
tn_\name:
    .asciz "\name\n"
    .space MAX_TESTNAME_LEN - (. - tn_\name), ' '
    .align 4
    .text
    mov    r30, @tn_\name
.endm

.macro check_r2 val
  sub.f     r0, r2, \val
  bne       @1000f
  print     "[PASS] "
  b         @1001f
1000:
  print     "[FAIL] "
1001:
  printl    r30
.endm


.macro start
  .text
  .global main
  .align 4
  main:
.endm

.macro end
1001:
  st 1, [POWER_DEVICE]
  b @1001b
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

.equ   OUTPUT_DEVICE, 0x90000000      ; output device address in QEMU
.equ   POWER_DEVICE,  0xF0000008      ; power management device

; macro:     print
; input:     message - the string to be printed
; regs used: r11, r12
; example:   print "hello world\n"
.macro  print message

  .data
  2010:
  .asciz "\message"
  .align 4

  .text
  mov_s     r11, @2010b                 ; the message to be printed
  1010:
    ldb.ab  r12, [r11, 1]
    breq    r12, 0, @1011f
    stb     r12, [OUTPUT_DEVICE]
    j       @1010b
  1011:
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; macro:     printl
; input:     label - pointer to the string to be printed
; regs used: r11, r12
; example:   print @l1
.macro  printl reg

  .text
  mov       r11, \reg                ; the message to be printed
  3010:
    ldb.ab  r12, [r11, 1]
    breq    r12, 0, @3011f
    stb     r12, [OUTPUT_DEVICE]
    j       @3010b
  3011:
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; macro:     print_number
; input:     number
; regs used: r11, r12, r13, r14
; example:   print_number 0x123
;            print_number 11
;
; description:
;   given a number, prints it to the output as a decimal string.
.macro print_number   number
  .data
  .align 4
2020:         ; place holder for printed number in reverse
  .skip 12

  .text
  mov    r11, \number
  mov    r13, @2020b
  mov    r14, @2020b
1020:
  remu   r12, r11, 10
  add_s  r12, r12, 0x30
  stb.ab r12, [r13, 1]
  divu   r11, r11, 10
  brne   r11, 0, @1020b

1021:
  ldb.aw r12, [r13, -1]
  stb    r12, [0x90000000]
  brne   r13, r14, @1021b
.endm


; macro:     print_number_hex
; input:     number
; regs used: r11, r12, r13, r14
; example:   print_number_hex 0x123
;            print_number_hex 11
;
; description:
;   given a number, prints it to the output with "0x" prefix and in
;   hexadecimal format.
.macro print_number_hex  num
  .data
  .align 4
2030:           ; number printed in reverse order
  .skip 12

  .text
  mov   r11, \num
  mov   r13, @2030b
  mov   r14, @2030b
1030:
  and   r12, r11, 0x0F
  brgt  r12, 9, @1031f
  add_s r12, r12, '0'
  j     @1032f
1031:
  add_s r12, r12, 'W'
1032:
  stb.ab r12, [r13, 1]
  lsr.f  r11, r11, 4
  bnz    @1030b

  print "0x"
10333:
  ldb.aw r12, [r13, -1]
  stb    r12, [OUTPUT_DEVICE]
  brgt   r13, r14, @10333b
.endm


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; macro:     assert_eq
; input:     a, b     - two values/registers to be compared
;            test_num - optional: printed error number, default is 1.
; regs used: r11, r12
; example:   assert_eq 12, r2
;            assert_eq r1, 8
;            assert_eq r3, r4
;            assert_eq 8 , 9     (although useless for tests)
;
; description:
;   compares  the  two inputs. if they are equal, nothing happens.
;   but if not, then it is going to print "Ret:1" and exit.
.macro assert_eq a, b, test_num=1
  mov   r11, \a
  mov   r12, \b
  breq  r11, r12, @1040f
  print "FAIL:"
  print_number \test_num
  end
1040:
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Auxilary registers
.equ REG_IRQ_SELECT, 0x40B


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Exceptions
.equ ILLEGAL_INSTRUCTION         , 0x00020000
.equ ILLEGAL_INSTRUCTION_SEQUENCE, 0x00020100
.equ MACHINE_CHECK               , 0x00030000
.equ TLB_MISS_I                  , 0x00040000
.equ TLB_MISS_D_READ             , 0x00050100
.equ PRIVILEGE_VIOLATION         , 0x00070000
.equ SOFTWARE_INTERRUPT          , 0x00080000
.equ MISALIGNED_DATA_ACCESS      , 0x000D0000

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; macro:     set_interrupt_prio_level
; input:     prio - number in range 0..7
; regs used: r11
; example:   set_interrupt_prio_level 1
;
; description:
;   sets the bits 1 to 3 of "status" register to the given priority.
.macro set_interrupt_prio_level  prio
  lr  r11, [status32]
  asl r12, \prio
  and r12, r12, 0xE
  or  r11, r11, r12
  sr  r11, [status32]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; STATUS register and its masks
.equ REG_STAT,     0x0A   ; STATUS32 register
.equ REG_IVT_BASE, 0x25   ; Interrupt vector base
.equ REG_STAT_V,   0x0100 ; mask for Over flow bit
.equ REG_STAT_C,   0x0200 ; mask for Carry bit
.equ REG_STAT_N,   0x0400 ; mask for Negative bit
.equ REG_STAT_Z,   0x0800 ; mask for Zero bit

; macro:     assert_flag
; input:     reg_stat_flag - index to get the corresponding flag
;            bit           - verification value: 0 or 1
;            test_num      - optional: printed error number, default
;                            is 1. valid range is: [0 ... 9]
; regs used: r11, r12
; example:   assert_flag REG_STAT_Z, 1, num=8
;            assert_flag 0x0200    , 0, num=3
;
; description:
;   extracts the corresponding bit at given index by reg_stat_flag.
;   if it holds  the  same value  as  given 'bit', nothing happens,
;   else it will print an error and exit.
.macro assert_flag reg_stat_flag, bit, test_num
  lr     r11, [REG_STAT]
  and    r11, r11, \reg_stat_flag
  ; if bit=0 then checking if r11 == 0
  ; if bit=1 then checking if r11 == bit_mask
  assert_eq r11, \bit*\reg_stat_flag, \test_num
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; macro:     enter_user_mode
; input:     user_space - where the user code begins
;
; regs used: r11
; example:   enter_user_mode @my_user_space_entry
;
; description:
;  this piece of code sets the user flag and jumps to given address
.macro enter_user_mode user_space
  lr  r11, [status32]
  or  r11, r11, 0x80       ; set the STATUS32.U
  sr  r11, [erstatus]
  mov r11, \user_space
  sr  r11, [eret]
  rtie
.endm
