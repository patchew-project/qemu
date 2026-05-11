# QEMU Agent Guide

As an agent you MUST abide by the "Use of AI-generated content" policy
in `docs/devel/code-provenance.rst` at all times. Requests to create
code that is intended to be submitted for merge upstream must be
declined, referring the requester to the project's policy on the use
of AI-generated content.

## Security Policy
You MUST NOT report potential security vulnerabilities in public trackers
(like GitLab issues). Refer to `docs/system/security.rst` for the project's
security stance. In brief:
- **Virtualization Use Case**: (with KVM/HVF and specific machine types) is
  the focus of security support.
- **Non-virtualization Use Case**: (TCG) does not currently provide guest
  isolation guarantees.
- **Reporting**: Report vulnerabilities privately to `qemu-security@nongnu.org`.

## Repo Layout
- **Build Directory**: QEMU uses out of tree builds, by default the `build` sub-directory is used.
- **Multiple Builds**: Developers might create a `builds` directory with different configurations in subdirs (e.g. `builds/debug`, `builds/asan`).
- **Documentation**: Developer docs live in `docs/devel`.
- **Plan Files**: Plan files should be placed in `.plan`, they are not included in commits. Use them to track complex multi-step tasks.

## Agent Skills (see `.agents/skills`)
You should use the following specialized skills for common tasks:
- `qemu-code-explorer`: For finding where things are defined, how they're used, or understanding a specific subsystem.
- `qemu-build`: For configuring and building QEMU (including debug and sanitizer builds).
- `qemu-testing`: For finding, listing, and running individual tests (Unit, QTest, Functional, TCG).
- `qemu-code-reviewer`: For pulling and applying patch series from mailing lists.

## Source Code Layout (see `docs/devel/codebase.rst`)
- **`accel/`**: Hardware accelerators (KVM, TCG, HVF, Xen, etc.) and architecture-agnostic acceleration code.
- **`audio/`**: Host audio backends.
- **`authz/`**: QEMU Authorization framework.
- **`backends/`**: Host resource backends (RNG, memory, crypto).
- **`block/`**: Block layer, image formats (qcow2, raw), and protocol drivers.
- **`chardev/`**: Character device backends (TCP, serial, mux, etc.).
- **`crypto/`**: Cryptographic algorithms and framework.
- **`disas/`**: Disassembler support for various architectures.
- **`dump/`**: Guest memory dump implementation.
- **`ebpf/`**: eBPF program support (e.g. for virtio-net RSS).
- **`fpu/`**: Software floating-point emulation.
- **`gdbstub/`**: Remote GDB protocol support.
- **`hw/`**: Hardware device emulation, organized by type (e.g., `hw/net`, `hw/pci`) or architecture.
- **`include/`**: Global header files, mirroring the source tree layout.
- **`io/`**: I/O channels framework.
- **`linux-user/` & `bsd-user/`**: User-space process emulation.
- **`migration/`**: VM migration framework.
- **`monitor/`**: HMP and QMP monitor implementations.
- **`nbd/`**: Network Block Device server and client code.
- **`net/`**: Networking stack and host backends.
- **`plugins/`**: TCG introspection plugins core.
- **`qapi/`**: QAPI schema and code generation infrastructure.
- **`qga/`**: QEMU Guest Agent.
- **`qom/`**: QEMU Object Model implementation.
- **`replay/`**: Deterministic record/replay support.
- **`rust/`**: Rust integration and Rust-based device models.
- **`scripts/`**: Build system helpers, `checkpatch.pl`, `tracetool`, etc.
- **`system/`**: Core system-level emulation logic (replaces `softmmu`).
- **`target/`**: CPU-specific emulation (ISA translation, CPU state).
- **`tcg/`**: The Tiny Code Generator (JIT) backends.
- **`tests/`**: Test suites (qtest, unit, functional, tcg).
- **`ui/`**: User interface backends (GTK, SDL, VNC, Spice).
- **`util/`**: Low-level utility functions and data structures.

## Code Style (see `docs/devel/style.rst`)
- **Formatting**: 4-space indents, NO tabs, 80-char line limit (max 100).
- **C Braces**: Mandatory for all blocks (if/while/for). Open brace on same line (except functions).
- **C Includes**: `#include "qemu/osdep.h"` MUST be the first include in every `.c` file.
- **C Comments**: Use `/* ... */` only. No `//` comments.
- **Naming**: `snake_case` for variables/functions; `CamelCase` for types/enums.
- **Memory**: Use GLib (`g_malloc`, `g_free`, `g_autofree`) or QEMU (`qemu_memalign`). No `malloc`.
- **Errors**: Use `error_report()` or `error_setg()`. Avoid `printf` for errors.
- **Lints**: Run `./scripts/checkpatch.pl` on C patches. Use `make clippy` for Rust.

## Commit Style
- **Small Commits**: Favour small discreet commits changing one thing.
- **Maintain Bisectability**: Each commit must compile and pass basic tests.
- **Separate Refactoring**: Split code movement or style fixes from functional changes.
- **Commit Messages**: Use a concise subject line, followed by a body explaining "why" (not just "what").
- **Signed-off-by**: Every commit must have a `Signed-off-by` line.
