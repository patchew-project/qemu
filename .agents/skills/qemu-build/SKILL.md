---
name: qemu-build
description: Step-by-step configure and build QEMU. Trigger when user asks build QEMU or debug build failures. Includes build dir reuse and spawning sub-agents.
license: GPL-2.0-or-later
---

# Instructions

## Check and Reuse Build Dirs
Check if existing build dir reusable before new one. QEMU uses out-of-tree builds (often `build` or `builds/`).

1. **Check configs**: Read `config.log` in existing build dir. Run `head -n 2 builds/<dir>/config.log` to get full configure command.
2. **Reuse and Reconfigure**: Reuse general-purpose dirs (like `builds/debug`). Reconfigure same flags with new `--target-list` if needed:
   ```bash
   cd builds/debug
   ../../configure <old-flags> --target-list=<new-targets>
   ```

## Launch Builds via Sub-Agent
**CRITICAL**: NEVER build in main agent context. ALWAYS spawn sub-agent.
Pass exact build commands and directory in sub-agent `task` argument. Instruct sub-agent what to verify and report.
Example: `task: "Go to builds/debug, run ninja. If fail, report exact compiler errors."`

## Configure New Build
If no good dir exists, make new.

1. **Make dir**: `mkdir -p builds/test-target; cd builds/test-target`
2. **Configure**: `../../configure --target-list=[targets]`
   - Targets: `x86_64-softmmu`, `aarch64-softmmu`, `riscv64-softmmu`, `x86_64-linux-user`.
3. **Common flags**:
   - `--enable-debug-info`: Symbols.
   - `--enable-debug`: Assertions.
4. **Sanitizers**:
   - `--enable-asan`: Address Sanitizer.
   - `--enable-tsan`: Thread Sanitizer.
   - `--enable-ubsan`: Undefined Behavior Sanitizer.
5. **Help**:
   - `--help`: All options.

## Build
**Important**: Re-build after modifying source code.

## Report Results
**CRITICAL**: Provide concise build summary to user.
1. **Summary**: Success or failure.
2. **Failures**: Include exact compiler/linker error log excerpt if fail.

## Debug
- **Verbose**: `V=1` for full output.
