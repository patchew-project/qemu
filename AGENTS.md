# QEMU Agent Guide

## Build & Test
- **Build**: `ninja -C build` (from build directory) or `make`
- **Test All**: `make check`
- **Single Test**: `./pyvenv/bin/meson test <testname>` (e.g., `meson test qtest-x86_64/boot-serial-test`)
- **Suites**: `make check-unit`, `make check-qtest`, `make check-functional`, `make check-rust`
- **Debug**: Append `V=1` for verbose output or `DEBUG=1` for interactive test debugging.

## Code Style
- **Formatting**: 4-space indents, NO tabs, 80-char line limit (max 100).
- **C Braces**: Mandatory for all blocks (if/while/for). Open brace on same line (except functions).
- **C Includes**: `#include "qemu/osdep.h"` MUST be the first include in every `.c` file.
- **C Comments**: Use `/* ... */` only. No `//` comments.
- **Naming**: `snake_case` for variables and functions; `CamelCase` for types and enums.
- **Memory**: Use GLib (`g_malloc`, `g_free`, `g_autofree`) or QEMU (`qemu_memalign`) APIs. No `malloc`.
- **Errors**: Use `error_report()` or `error_setg()`. Avoid `printf` for errors.
- **Lints**: Run `./scripts/checkpatch.pl` on C patches. Use `make clippy` and `make rustfmt` for Rust.
