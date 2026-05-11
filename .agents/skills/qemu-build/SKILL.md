---
name: qemu-build
description: Provides step-by-step instructions on configuring and building QEMU. You MUST trigger this skill whenever the user asks to build QEMU or debug build failures. It includes critical details on build directory reuse and spawning sub-agents.
license: GPL-2.0-or-later
---

# Instructions

## Examining and Re-using Build Directories
Before creating a new build directory, check if an existing one can be re-used. QEMU uses out-of-tree builds, typically in `build` or `builds/` sub-directories.

1. **Check existing configs**: You can examine how an existing build directory was configured by checking its `config.log`. Run `head -n 2 builds/<dir>/config.log`. The second line typically contains the full `../configure` command line used.
2. **Re-use and Reconfigure**: You have latitude to re-use existing directories when appropriate (e.g., `builds/debug` which is a general-purpose debug directory for whatever is currently going on). If an existing directory has the right flags (like debug/sanitizers) but the wrong target list, you can reconfigure it to keep the same config but change the `--target-list`:
   ```bash
   cd builds/debug
   # Check the old config.log, then re-run configure with the new target-list
   ../../configure <old-flags> --target-list=<new-targets>
   ```

## Launching Builds
**Crucial**: You MUST NEVER run builds directly in the main agent context. You MUST ALWAYS launch them by spawning a sub-agent.
Pass the specific build commands, along with the required working directory, in the `task` argument. Give the subagent explicit instructions on what to verify and what to report back to you.
For example: `task: "Navigate to builds/debug and run ninja. If it fails, report the exact compiler errors."`

## Configuring a New Build
If no suitable build directory exists, create a new one.

1. **Create build directory**: `mkdir -p builds/test-target; cd builds/test-target`
2. **Basic Configure**: `../../configure --target-list=[list of targets]`
   - Common targets: `x86_64-softmmu`, `aarch64-softmmu`, `riscv64-softmmu`, `x86_64-linux-user`.
3. **Common Options**:
   - `--enable-debug-info`: Include symbols.
   - `--enable-debug`: Enable assertions.
4. **Sanitizers**:
   - `--enable-asan`: Address Sanitizer.
   - `--enable-tsan`: Thread Sanitizer.
   - `--enable-ubsan`: Undefined Behavior Sanitizer.
5. **Help**:
   - `--help`: will give a comprehensive list of options

## Building
**Important**: Always re-run the build after making changes to the source code.

## Reporting Results
**Crucial**: After completing the build task, provide a concise summary of the results to the user.
1. **Summary**: State whether the build passed or failed.
2. **Failure Excerpts**: If the build fails, include relevant excerpts from the logs (e.g., compiler errors).

## Debugging and Environment
- **Verbose**: `V=1` for detailed output.
