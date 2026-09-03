---
name: qemu-codebase
description: Orientation for QEMU — tree structure, build system, documentation pointers
---

# Useful reminders for working on QEMU

## Finding things

`MAINTAINERS` is the authoritative map from file patterns to subsystems
and maintainers; use it to work out who owns code and which tree a
change goes through. `docs/devel/codebase.rst` is a guided tour of every
top-level directory.  Here are some important ones:

- **target/** holds CPU models and the TCG frontends
- **tcg/** holds the backends and the IR. See `docs/devel/tcg.rst`,
  `docs/devel/tcg-ops.rst`.
- **hw/** is devices and boards, categorized by type.
- **linux-user/** and **bsd-user/** are almost entirely separate, and only
  have parts of **hw/core/** and **accel/**'s CPU emulation infrastructure
  in common with system emulation

Other directories include the back-end subsystems, for example **`block/`**
for the block layer.

The `include/` tree mostly mirrors the top-level tree.

## Build layout

Build is always out-of-tree; the build directory is created by
`configure` and a checkout can have several. Determine it from context
(`ls */meson-info`) rather than assuming.

Every path below that starts with `pyvenv/` is relative to the build
directory: `configure` creates a Python venv there, and `pyvenv/bin/meson`
is the meson that must be used.

`make` at the top level forwards to `ninja` in the configured build
directory, and any `build.ninja` target can be invoked that way.

## Build & Test
- **Build**: `ninja -C build` (from build directory) or `make -jN`
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

# Documentation pointers

Developer docs live in `docs/devel`.  A `kernel-doc::` directive includes
documentation comments from source files when Sphinx builds the documentation.
These comments be consulted just as easily in the source tree without going
through e.g. `make html`.

Here are some useful pointers.

## Core abstractions

- **QOM** (`qom/`, `include/qom/`) is the type/object system underneath
  everything.  It includes class and interface hierarchies, properties, and
  the object composition tree.  See `docs/devel/qom.rst`.
- **qdev** builds devices on top of QOM, adding for example buses, the
  realize/unrealize lifecycle (including hot-plug/unplug), and reset.  See
  `docs/devel/qdev-api.rst` and `docs/devel/reset.rst`.
- **MemoryRegion** (`system/memory.c`, `include/system/memory.h`) is the
  guest address-space model: regions, aliases, address spaces, dirty tracking,
  load/store and map/unmap operations, etc.  See `docs/devel/memory.rst`.

## Concurrency

Getting the threading model wrong is a common source of subtle bugs here.

- The **BQL** (big QEMU lock) protects most device emulation; vCPU threads
  hold it when exiting to emulation.  See `include/qemu/main-loop.h`.
  Memory regions can (carefully) opt out of the BQL.
- **AioContext**/iothreads: block devices and their virtio front-ends can run
  outside the BQL.  See `docs/devel/multiple-iothreads.rst`.
- The **block layer** is coroutine-based.  `co_` prefixes and `coroutine_fn`
  annotations are advisory but relevant for reviewers.  Coroutines have their
  own locking primitives.
- **RCU** is used for hot, rarely-modified structures such as memory maps.
  Because of the BQL, RCU is mostly used with `call_rcu()` rather than
  `synchronize_rcu()`.  See `docs/devel/rcu.rst` and `docs/devel/atomics.rst`.
