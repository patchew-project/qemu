---
name: qemu-code-reviewer
description: Pull and apply patch series from mailing lists for review and testing in QEMU.
license: GPL-2.0-or-later
---

# QEMU Code Reviewer Skill

This skill provides instructions on how to retrieve patch series submitted to the QEMU mailing list (`qemu-devel@nongnu.org`) using `b4` or manual methods.

## Using b4 (Recommended)

`b4` is the preferred tool for working with patch series from public-inbox instances like `lore.kernel.org`.

### 1. Fetching a series
To download a series and prepare it for `git am`:
```bash
b4 am <message-id-or-url>
```
This creates a `.mbx` file containing the entire series, properly ordered.

### 2. Applying a series directly
To apply a series directly to your current branch:
```bash
b4 shazam <message-id-or-url>
```
This is often the fastest way to get a series ready for testing.

### 3. Creating a local branch for the series
```bash
b4 am -t <message-id-or-url>
git am ./*.mbx
```
The `-t` flag (or `--trust-all`) can be useful if you know the source.

## Manual mbox Retrieval (Alternative)

If `b4` is unavailable, you can fetch the mbox manually from `lore.kernel.org`.

### 1. Locate the thread
Find the patch series on [lore.kernel.org/qemu-devel/](https://lore.kernel.org/qemu-devel/).

### 2. Download the mbox
Every thread on lore has an `mbox.gz` link. You can use `curl` or `wget`:
```bash
curl -L "https://lore.kernel.org/qemu-devel/<message-id>/raw" -o series.mbox
```
*Note: Appending `/raw` to the message URL usually provides the mbox format.*

### 3. Apply with git am
```bash
git am series.mbox
```

## Post-Application Steps

Once the patches are applied, you should perform initial validation:

### 1. Style Check
Run the QEMU checkpatch script:
```bash
./scripts/checkpatch.pl master..HEAD
```

### 2. Build and Test
Refer to the `AGENTS.md` or the `qemu-code-explorer` skill for build and test instructions.
- Ensure you are in a clean build directory.
- Run `ninja` or `make`.
- Run relevant tests (e.g., `make check-qtest`).

## Common Troubleshooting

- **Applying fails**: If `git am` fails due to conflicts, you may need to use `git am --3way` or manually resolve conflicts.
- **Missing dependencies**: Ensure your tree is up to date with the base branch the patches were intended for (usually `master`).
