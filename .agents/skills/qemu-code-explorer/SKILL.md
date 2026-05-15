---
name: qemu-code-explorer
description: Advanced QEMU code exploration for special cases (generated code, complex macros, QOM) that are not easily handled by standard code exploration tools. Use this as a fallback strategy for build-time artifacts or hidden symbols.
license: GPL-2.0-or-later
---

# QEMU Advanced Code Explorer

When standard code navigation tools fail to find a symbol, it is often because the code is generated at build time or hidden behind complex macros. This skill covers how to find and understand those special cases.

## 1. Searching Generated Code

Generated source files do not reside in the source tree but in the build directory. If a primary symbol search fails, the symbol is likely generated.

### QAPI (QEMU Interface)
- **Source**: `qapi/*.json`
- **Generated**: `build/qapi/` (headers and C files)
- **Patterns**:
  - `qmp_marshal_...`: Command marshallers.
  - `qapi_free_...`: Type cleanup functions.
  - `visit_type_...`: Visitor functions.
- **Search**: Search the `build/` directory using standard text search tools.

### Tracing
- **Source**: `trace-events` files throughout the tree.
- **Generated**: `build/trace/` (e.g., `trace/trace-hw_block.h`)
- **Patterns**: `trace_...` functions.
- **Search**: Check the build directory or the generated headers.

### Decodetree (Instruction Decoding)
- **Source**: `target/.../*.decode` files.
- **Generated**: Usually included into source files as `.c.inc` artifacts in the build tree.
- **Patterns**: `trans_...` functions are handwritten, but the decoder that calls them is generated.

### Configuration
- `build/config-host.h`: Global host configuration.
- `build/config-target.h`: Target-specific configuration (e.g., `TARGET_X86_64`).

## 2. Expanding Complex Macros

QEMU uses deep macro nesting (especially in TCG and softfloat) which can be opaque to static analysis.

### `scripts/expand-macro.py`
Use this tool to see the exact C code after preprocessing for a specific range in a file.

- **Usage**:
  ```bash
  python3 ./scripts/expand-macro.py FILE --context CONTEXT_FILE --range START_LINE-END_LINE
  ```
- **Example**:
  To see how TCG helpers or softfloat parts expand:
  ```bash
  python3 ./scripts/expand-macro.py fpu/softfloat-parts.c.inc --context fpu/softfloat.c --range 191-264
  ```

## 3. QEMU-Specific Symbol Patterns

Some symbols are "hidden" from simple search tools because of boilerplate-reducing macros.

### QOM (QEMU Object Model)
Macros like `OBJECT_DECLARE_SIMPLE_TYPE` or `OBJECT_DECLARE_TYPE` expand to multiple function declarations and casting macros.
- If you see `MY_DEVICE(obj)`, it is likely defined via `OBJECT_DECLARE_TYPE(...)`.
- Search for the type name string (e.g., `"my-device"`) to find the `TypeInfo` structure, which links everything together.

### TCG Helpers
- **Pattern**: `HELPER(foo)`
- Defined in `helper.h` or similar, implemented as `helper_foo`.
- If searching for `helper_foo` fails, search for the `HELPER(foo)` pattern in the source.

### Error Handling
- `ERRP_GUARD()`: Used at the start of functions to handle `error_propagate`.
- `error_setg(errp, ...)`: Common pattern for reporting errors.

## Decision Matrix

| Scenario | Strategy |
|------|--------|
| Standard function/struct | Use primary code navigation tools. |
| Symbol not found in source | Search `build/` directory for generated code. |
| Macro expansion looks wrong | Use `scripts/expand-macro.py`. |
| QOM casting macro (`TYPE_...`) | Search for the `TypeInfo` or `OBJECT_DECLARE_...` usage. |
| TCG helper missing | Search for `HELPER(name)` pattern. |
| Instruction decoding | Check `.decode` files and corresponding `trans_...` functions. |

## Workflow Tips
1. **Locate the Build Directory**: QEMU developers often use `build/` but it could be named differently. Always check.
2. **Combine Tools**: Use primary tools first. If they fail, search the `build/` directory.
3. **Check the Context**: If a `.c.inc` file is confusing, find the `.c` file that includes it to understand the macro context.
