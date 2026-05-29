---
name: qemu-code-reviewer
description: Pull and apply patch series from mailing lists for review and testing in QEMU, including style and build validation.
license: GPL-2.0-or-later
---

# QEMU Code Reviewer Skill

Get patch series from QEMU mailing list (`qemu-devel@nongnu.org`) using `b4` or manual mbox.

## Pre-Application & Dependency Validation (CRITICAL)

Check prerequisites/dependencies before applying patches to avoid build fails or merge conflicts:

1. **Get Cover Letter First**:
   - `b4 am <message-id-or-url>` auto-generates `.cover` file or prints it.
   - Or fetch raw cover letter email (suffix `-0-` or `-00-`) from lore.kernel.org.
2. **Scan for "Based-on" / Prerequisite Headers**:
   - Parse cover letter for `Based-on: <message-id>`, `Prerequisites:`, or "depends on" text.
3. **Handle Dependencies**:
   - If dependency commits missing from branch, fetch and apply prerequisite series first.
   - **CRITICAL**: Ask user permission (using `eca__ask_user`) before changing branches or resetting git tree.

## Use b4 (Recommended)

`b4` is best tool for public-inbox (`lore.kernel.org`).

### 1. Fetch series
Download series for `git am`:
```bash
b4 am <message-id-or-url>
```
Saves as `.mbx` file.

### 2. Apply series directly
Apply to current branch:
```bash
b4 shazam <message-id-or-url>
```

### 3. Apply with trust
```bash
b4 am -t <message-id-or-url>
git am ./*.mbx
```

## Manual mbox Retrieval (Alternative)

If `b4` missing, get mbox from lore.kernel.org.

### 1. Find thread
Go to [lore.kernel.org/qemu-devel/](https://lore.kernel.org/qemu-devel/).

### 2. Download mbox
```bash
curl -L "https://lore.kernel.org/qemu-devel/<message-id>/raw" -o series.mbox
```

### 3. Apply
```bash
git am series.mbox
```

## Post-Application Validation

### 1. Style Check
Run QEMU checkpatch script.

- **Applied patches**:
  ```bash
  ./scripts/checkpatch.pl master..HEAD
  ```
- **Specific commit**:
  ```bash
  ./scripts/checkpatch.pl <commit-hash>^..
  ```
- **Specific file**:
  ```bash
  ./scripts/checkpatch.pl -f <file-path>
  ```
- **Strict mode**:
  ```bash
  ./scripts/checkpatch.pl --strict <commit-range>
  ```

### 2. Build and Test
See `qemu-build` and `qemu-testing` skills.
- Use clean build dir.
- Run `ninja` or `make` via sub-agent.
- Run tests (`make check-qtest`).

### 3. Review Code
See `qemu-code-explorer` to trace functions.

## Troubleshooting

- **Apply fails**: Try `git am --3way` or resolve conflicts manually.
- **Missing deps**: Update base branch to latest `master`.
