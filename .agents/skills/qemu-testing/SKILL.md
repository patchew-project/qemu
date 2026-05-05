---
name: qemu-testing
description: Provides instructions on how to find, list, and run individual tests for QEMU (Unit, QTest, Functional, TCG). You MUST use this skill whenever the user asks about tests, wants to list available tests, or wants to run any test in a build directory, even for simple exploratory queries like "what tests can I run". It includes details about spawning sub-agents.
license: GPL-2.0-or-later
---

# Instructions
To run QEMU tests, you can use several different suites depending on what you are testing. **Note**: Ensure you have a recent build of QEMU before running tests. Use the `qemu-build` skill if you need to configure or run a build.

## General Test Suites
- **Unit Tests**: `make check-unit`
- **QTest (Device emulation)**: `make check-qtest`
- **Functional Tests (Python based)**: `make check-functional`
- **TCG Tests (CPU instruction tests)**: `make check-tcg`

## Launching Tests
**Crucial**: You MUST NEVER run a test suites directly in the main agent context. You MUST ALWAYS launch them by spawning a sub-agent. Pass the specific build or test commands, along with the required build directory, in the `task` argument. Give the subagent explicit instructions on what to verify and what to report back to you.
For example: `task: "Navigate to builds/debug and check what meson tests touch replay and report back."` or `task: "Run make check-tcg in builds/arm and summarize any failing tests."`

### From the Build Directory
Most individual tests from within a build directory. Most (unit, qtest, block, functional) can be individually selected and run via meson.

As QEMU often needs a newer meson than the build host you should use the build `pyenv` to launch it:
- **Example**: `./pyvenv/bin/meson test --suite thorough --list` to see what tests are in the thorough test suite

## Running Individual Tests

### Meson Test Runner (Unit, QTest, Functional, softfloat etc)
To run a single test, you can use the meson test runner from within your pyvenv:
`./pyvenv/bin/meson test [testname]`
Example: `./pyvenv/bin/meson test qtest-x86_64/boot-serial-test`

### TCG Tests
To run individual TCG tests for a specific architecture:
1. Navigate to the relevant build directory, e.g.: `cd tests/tcg/aarch64-softmmu`
2. Run a specific test with make: `make run-[testname]`
   Example: `make run-memory-sve`
3. Use `make help` within the architecture directory to see the full list of available tests.

### Functional Tests
Individual functional tests can be run directly using the run script although from the source directory:
- **Example**: `./builds/all/run tests/functional/aarch64/test_virt_vbsa.py`

### Environment Variables
- `V=1` for verbose output from tests.
- `SPEED=slow` to run slower tests that are normally skipped.

## Reporting Results
**Crucial**: After completing the build and test tasks, provide a concise summary of the results to the calling agent.
1. **Summary**: State whether the build and tests passed or failed.
2. **Failure Excerpts**: If any task fails, include relevant excerpts from the logs (e.g., compiler errors, test failures).
3. **Full Paths**: Always provide the **absolute file paths** to the full logs and result sets for further inspection.

