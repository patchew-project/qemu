---
name: checkpatch
description: run checkpatch on a file or patch to validate style issues
---

# Instructions
Run `./scripts/checkpatch.pl [FILE]` to check a file for style issues.

You can also use a GIT-REV-LIST to check against git, e.g. run `./scripts/checkpatch.pl HEAD^..` to run checkpatch on the last commit.
