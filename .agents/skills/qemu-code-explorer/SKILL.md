---
name: qemu-code-explorer
description: Advanced QEMU code search (generated code, complex macros, QOM) when standard search fails. Fallback for build artifacts and hidden symbols.
license: GPL-2.0-or-later
---

# QEMU Advanced Code Explorer

If standard search fails, symbol is likely generated at build time or hidden by macros. Use these strategies.

## 1. Search Generated Code
Generated files exist in build dir, not source tree.

### QAPI (QEMU Interface)
- **Source**: `qapi/*.json`
- **Generated**: `build/qapi/` (headers, C files)
- **Patterns**:
  - `qmp_marshal_...`: Command marshallers.
  - `qapi_free_...`: Type cleanup functions.
  - `visit_type_...`: Visitor functions.
- **Action**: Search `build/` text.

### Tracing
- **Source**: `trace-events` files.
- **Generated**: `build/trace/` (e.g., `trace/trace-hw_block.h`)
- **Patterns**: `trace_...` functions.
- **Action**: Search build dir/generated headers.

### Decodetree (Instruction Decoding)
- **Source**: `target/.../*.decode`
- **Generated**: Included as `.c.inc` in build tree.
- **Patterns**: `trans_...` handwritten, decoder calling them generated.

### Configuration
- `build/config-host.h`: Global host config.
- `build/config-target.h`: Target-specific config (e.g., `TARGET_X86_64`).

## 2. Expand Complex Macros
QEMU uses deep macro nesting (TCG, softfloat).

### `scripts/expand-macro.py`
Get C code after preprocessing for specific line range.

- **Usage**:
  ```bash
  python3 ./scripts/expand-macro.py FILE --context CONTEXT_FILE --range START_LINE-END_LINE
  ```
- **Example** (TCG helpers, softfloat):
  ```bash
  python3 ./scripts/expand-macro.py fpu/softfloat-parts.c.inc --context fpu/softfloat.c --range 191-264
  ```

## 3. QEMU Symbol Patterns
Hidden by boilerplate-reducing macros.

### QOM (QEMU Object Model)
`OBJECT_DECLARE_SIMPLE_TYPE` or `OBJECT_DECLARE_TYPE` expand to multiple functions/casts.
- `MY_DEVICE(obj)` likely defined via `OBJECT_DECLARE_TYPE(...)`.
- Search type name string (e.g., `"my-device"`) to find `TypeInfo` structure.

### TCG Helpers
- **Pattern**: `HELPER(foo)`
- Defined in `helper.h`, implemented as `helper_foo`.
- Search `HELPER(foo)` pattern if `helper_foo` not found.

### Error Handling
- `ERRP_GUARD()`: Starts functions for `error_propagate`.
- `error_setg(errp, ...)`: Reports errors.

## Decision Matrix

| Scenario | Strategy |
|------|--------|
| Standard function/struct | Use standard search tools. |
| Symbol not found in source | Search `build/` for generated code. |
| Macro expansion opaque | Use `scripts/expand-macro.py`. |
| QOM cast macro (`TYPE_...`) | Search `TypeInfo` or `OBJECT_DECLARE_...` usage. |
| TCG helper missing | Search `HELPER(name)`. |
| Instruction decoding | Search `.decode` and `trans_...` functions. |

## Workflow Tips
1. **Find Build Dir**: Check `build/` or other build subdirs.
2. **Combine Tools**: Try standard search first, then search build dir.
3. **Check Context**: For `.c.inc` files, find the parent `.c` file that includes it.
