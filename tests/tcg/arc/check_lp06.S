; check_lp06.S
;
; Tests for Zero overhead loop: interrupting the loop
; If the test fails, check the end of this file for how to troubleshoot.

  .include "macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;; Test checking routines ;;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case counter
.data
test_nr:
  .word 0x0

; Increment the test counter.
.macro prep_test_case
  ld    r13, [test_nr]
  add_s r13, r13, 1       ; increase test case counter
  st    r13, [test_nr]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;; Exception related code ;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Handler of the day.
  .align 4
handler : .word 0x0

; An exception handler routine that merely jumps to whatever address
; it was told to by the test. See set_except_handler macro. This
; requires ivt.S file to be compiled and linked.
  .align 4
  .global EV_Trap
  .global EV_SWI
  .type EV_Trap, @function
  .type EV_SWI, @function
EV_SWI:
EV_Trap:
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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; ZOL ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Let the tests begin
  start

; Test case 1
; Having a 'trap_s' at the end of a loop body. The exception handler
; must return to the _next instruction_ after the trap which is the
; LOOP_END. Consequently, it should end up in the LOOP_START if the
; LP_COUNT != 1. To cut a long story short:
; next instruction(trap) = loop_start and all iterations should finish
; before getting out of the loop.
  prep_test_case
  set_except_handler @test_1_except_handler
  mov    r0, 0
  mov    lp_count, 0x1f
  lp     @test_1_loop_end
  trap_s 0
test_1_loop_end:
  cmp    r0, 0x1f      ; has the loop finished completely?
  bne    @fail
  b      @test_1_end   ; success
test_1_except_handler:
  add    r0, r0, 1
  rtie
test_1_end:
  ; Fall through

; Test case 2
; Having a 'swi' at the end of a loop body. The exception handler
; must return to the _last instruction_ of the loop body and the
; whole loop must finish completely.
; Going back to 'swi' is tricky because it keeps triggering the
; exception. So, after the first trigger, we change it to NOPs.
  prep_test_case
  set_except_handler @test_2_except_handler
  mov    r0, 0          ; exception trigger mark
  mov    r1, 0          ; loop counting
  mov    lp_count, 0x1f
  lp     @test_2_loop_end
  add    r1, r1, 1
test_2_last_insn_loop:
  swi
test_2_loop_end:
  cmp    r1, 0x1f       ; has the loop finished completely?
  bne    @fail
  cmp    r0, 1          ; exception triggered?
  bne    @fail
  b      @test_2_end    ; success
test_2_except_handler:
  add    r0, r0, 1
  mov    r11, @test_2_last_insn_loop
  mov    r12, 0x78e0          ; NOP_S opcode
  sth.ab r12, [r11,2]         ; write two NOP_S instead of one NOP
  sth    r12, [r11]           ; to avoid misaligned exception.
  rtie
test_2_end:
  ; Fall through

; Test case 3
; Check if _any_ fetch of instruction at address LOOP_END trigger
; going back to the loop start if the LP_COUNT is not 1. To test
; that:
; Jump out of the loop prematurely.
; Then outside the loop jump back inside the lopp.
; This should trigger going back to the loop, but do not jump out
; prematurely anymore.
  prep_test_case
  mov    r0, 0                ; loop counter
  mov    r2, 0                ; indicator if we jumped to LOOP_END before
  mov    lp_count, 17
  lp     @test_3_loop_end
  cmp    r2, 1
  bne    @test_3_outside_loop
test_3_last_insn_loop:
  add    r0, r0, 1
test_3_loop_end:
  add    r3, r2, r0           ; r3 = 1 + 17
test_3_outside_loop:
  add    r2, r2, 1
  cmp    r2, 1
  beq    @test_3_last_insn_loop
  cmp    r0, 17               ; sanity checks begin
  bne    @fail
  cmp    r2, 2                ; once we jumped there, once fall through.
  bne    @fail
  cmp    r3, 18
  bne    @fail

; Next test cases
; Timer interrupt and a single insn ZOL. We need to check if indeed we get multiple interrupts, while in ZOL.
; Timer interrupt and CLRI/SETI body ZOL. The same as above, 2 tests with seti/clri and clri/seti instruction order.
; Last instruction of a ZOL gets a MMU TLBI miss.
; Last instruction of a ZOL gets a MMU TLBD miss (load/store).
; Last instruction of a ZOL gets a MMU TLBI fallowed by a MMU TLBD miss.

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
  print " Zero overhead loop: interrupting the loop\n"
  end
