---
name: qemu-testing
description: Find, list, and run QEMU tests (Unit, QTest, Functional, TCG). Trigger when user asks about tests, lists available tests, or runs tests in build dir. Includes sub-agent spawning.
license: GPL-2.0-or-later
---

# Instructions
Need recent QEMU build before testing. See `qemu-build` skill.
Official testing docs: `docs/devel/testing/` (specifically `main.rst`).

## Test Suite Classification Guide

Avoid running invalid/incompatible tests. Know the suites:

### 1. QTest (`make check-qtest` or `meson test --suite qtest`)
- **Scope**: **System Emulation Only** (needs `qemu-system-*` binary).
- **Nature**: Exercises virtual devices, registers, buses, boards. Host-side test communicating with guest over custom protocol. No real guest OS/BIOS booted.
- **When to run**: Modifying virtual devices (`hw/`), PCI/USB/ISA buses, interrupt controllers (`intc/`), machine/board definitions.

### 2. Functional Tests (`make check-functional` or run specific `test_*.py`)
- **Scope**: **System Emulation Only** (needs system binary).
- **Nature**: Python system integration tests. Boots real guest kernels, tests OS images, migration, end-to-end setups.
- **When to run**: Validating board features, system-level CPU features, full machine boot.

### 3. TCG Tests (`make check-tcg`)
- **Scope**: **Mostly User Mode** (needs guest user-space translation, e.g., `qemu-aarch64`), some system tests.
- **Nature**: Compiles/runs real guest programs (e.g., SVE vector math, float tests) inside emulated CPU. Verifies translation, GDB stub, plugins.
- **When to run**: Modifying CPU ISA translation (`target/`), emulation helpers (`target/*/tcg/`), software float (`fpu/`), TCG JIT compiler (`tcg/`).

### 4. Unit Tests (`make check-unit` or `meson test --suite unit`)
- **Scope**: **Independent/Global** (architecture/target agnostic).
- **Nature**: Standalone C unit tests for core parts (glib, QDict/QList, crypto, standalone float math).
- **When to run**: Modifying core utilities (`util/`), data formats (`qobject/`), crypto (`crypto/`), or floating-point math (`fpu/`).

## Launch Tests via Sub-Agent
**CRITICAL**: NEVER run tests in main agent context. ALWAYS spawn sub-agent.
Pass build/test commands and build directory in sub-agent `task` argument. Direct sub-agent what to verify and report.
Example: `task: "Go to builds/debug, run meson tests touching replay, report results."` or `task: "Run make check-tcg in builds/arm, summarize fails."`

### Meson from Build Directory
Use build `pyenv` for correct meson version:
- **Example**: `./pyvenv/bin/meson test --suite thorough --list` (lists thorough suite tests).

## Run Individual Tests

### Meson Test Runner (Unit, QTest, Functional, softfloat)
Run single test from within pyenv in build dir:
`./pyvenv/bin/meson test [testname]`
Example: `./pyvenv/bin/meson test qtest-x86_64/boot-serial-test`

### TCG Tests
Run single test for specific target:
1. Go to target test build dir: `cd tests/tcg/aarch64-softmmu`
2. Run test: `make run-[testname]`
   Example: `make run-memory-sve`
3. Check all tests: `make help` in that dir.

### Functional Tests
Run individual tests from source dir:
- **Example**: `./builds/all/run tests/functional/aarch64/test_virt_vbsa.py`

### Env Variables
- `V=1`: Verbose test output.
- `SPEED=slow`: Run slow tests normally skipped.

## Report Results
**CRITICAL**: Summarize results to caller.
1. **Summary**: Success or failure.
2. **Failures**: Excerpt exact logs (compiler errors, test fails).
3. **Full Paths**: Provide **absolute file paths** to logs/results for inspection.
