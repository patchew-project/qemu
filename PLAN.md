# Plan: Extend WFX Support for A-profile

This document outlines the plan to extend WFE support to A-profile ARM and ensure proper semantics for WFI, WFE, WFIT, and WFET instructions, including full ISS field support for traps.

## 1. Syndrome Enhancements

### 1.1 Update `syn_wfx` in `target/arm/syndrome.h`
- Modify `syn_wfx` to include `rd` (RN) and `rv` (Register Valid) fields.
- Ensure the bitfields match the ARM ARM for EC 0x01.
- Bit layout:
  - ISS[24]: CV
  - ISS[23:20]: COND
  - ISS[19:15]: RN (rd)
  - ISS[14]: RV
  - ISS[1:0]: TI

### 1.2 Update WFX Syndrome Construction in Helpers
- In `target/arm/tcg/op_helper.c`, update `HELPER(wfi)`, `HELPER(wfit)`, and implement `HELPER(wfet)` to:
  - Check `is_a64(env)`.
  - For AArch64: Set `cv = 0` and `cond = 0xf`.
  - For AArch32: Maintain `cv = 1` and `cond = 0xe` (or pass the actual condition).
  - Pass the correct `rd` and `rv` based on the instruction.

## 2. Instruction Helpers and Translation

### 2.1 Update `HELPER(wfit)` and `trans_WFIT`
- Change `HELPER(wfit)` to accept the register number `rd`.
- Update `trans_WFIT` in `target/arm/tcg/translate-a64.c` to pass `a->rd`.

### 2.2 Implement `HELPER(wfet)` and update `trans_WFET`
- Create `HELPER(wfet)` in `op_helper.c`. It should:
  - Check for traps using `check_wfx_trap(env, true, &excp)`.
  - If trapped, raise exception with `ti=1` and `rv=true, rd=rd`.
  - If not trapped, check `event_register` and timeout.
- Update `trans_WFET` to call the new helper instead of just setting `DISAS_WFE`.

### 2.3 Refactor `HELPER(wfe)` for A-profile
- Update `HELPER(wfe)` to handle A-profile:
  - Check for traps using `check_wfx_trap(env, true, &excp)`.
  - If trapped, raise exception.
  - If not trapped:
    - If `env->event_register` is set, clear it and return.
    - Otherwise, halt the CPU (`cs->halted = 1`, `cs->exception_index = EXCP_HLT`, `cpu_loop_exit`).

## 3. Wake-up Logic and Event Register

### 3.1 Update `HELPER(sev)`
- Ensure `event_register` is set for all CPUs, not just M-profile.
- Kick all other CPUs to wake them from `EXCP_HLT`.

### 3.2 Update `arm_cpu_has_work`
- Modify `target/arm/cpu.c` to check `event_register` for A-profile CPUs as well.
- This ensures that a CPU halted by WFE wakes up when the event register is set.

## 4. Testing and Verification

### 4.1 TCG Tests
- Add a new test case in `tests/tcg/aarch64` that:
  - Executes WFI, WFE, WFIT, WFET.
  - Verifies that WFE/WFI halt the CPU and can be woken up.
  - (Optional) Exercises traps by running at EL0 with SCTLR_EL1.nTWE/nTWI = 0 and verifying the syndrome in a signal handler or a small EL1 kernel.

### 4.2 Regression Testing
- Ensure no regressions for M-profile WFE.
- Run `make check-qtest` and `make check-tcg`.

## Commit Breakdown (Proposed)

1. `target/arm: Update syn_wfx to include RN and RV`
2. `target/arm: Update HELPER(wfi) to use correct CV/COND for AArch64`
3. `target/arm: Update HELPER(wfit) to accept rd and use correct syndrome`
4. `target/arm: Implement HELPER(wfet) and update trans_WFET`
5. `target/arm: Update HELPER(wfe) to implement proper A-profile semantics`
6. `target/arm: Update HELPER(sev) and arm_cpu_has_work for A-profile event register`
7. `tests/tcg/aarch64: Add tests for WFX instructions`

🤖 Generated with [eca](https://eca.dev)
