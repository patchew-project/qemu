---
name: qemu-issue-helper
description: Summarize QEMU issue analysis. Help sub-agents report findings (build config, CLI, tests, GitLab issue data). Trigger when analyzing QEMU bugs/GitLab issues.
license: GPL-2.0-or-later
---

# QEMU Issue Helper

Help sub-agent summarize issue analysis for main agent.

## Fetch Issue Data

Use `glab` for GitLab issues. Repo: `qemu-project/qemu`.

### Commands
- **View issue**: `glab issue view <ID_OR_URL> -R qemu-project/qemu`
- **View comments**: `glab issue view <ID_OR_URL> -R qemu-project/qemu --comments`
- **Search issues**: `glab issue list -R qemu-project/qemu --search "<KEYWORDS>"`

## Report Format

Sub-agent MUST use this format for GitLab issue summary:

### 1. Issue Context
- **Source**: GitLab URL/ID.
- **Title**: Brief description.
- **Reporter**: Username.
- **Relevant Commits**: Related commits mentioned in issue.

### 2. Build & Reproduction (from issue)
- **Environment**: Host OS, CPU, QEMU version.
- **Build Config**: Required `configure` flags.
- **Reproduction CLI**: Exact QEMU command.

### 3. Proposed Fixes & Series
- **Proposed Fixes**: Suggested code snippets or logic fixes.
- **Patch Series**: Linked patch series or MRs.

### 4. Discussion Summary
- **Consensus**: Community understanding of bug.
- **Constraints**: Blockers, requirements, maintainer feedback.
- **Next Steps**: Action needed to proceed.

## Rules
- **No Independent Analysis**: Summarize ONLY issue tracker data. No self-made root cause analysis.
- **Terse**: Keep it brief and technical.
- **Links**: Include direct links to comments or patches.
