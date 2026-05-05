---
name: qemu-issue-helper
description: Summarize QEMU issue analysis for main agent. Helps sub-agents report findings including build config, CLI, tests, and GitLab issue data. Trigger when analyzing QEMU bugs or issues reported on GitLab.
license: GPL-2.0-or-later
---

# QEMU Issue Helper

Assist sub-agent in summarizing issue analysis for main agent.

## Fetching Issue Data

Use `glab` to retrieve issue details from GitLab. QEMU primary repo: `qemu-project/qemu`.

### Commands
- **View issue**: `glab issue view <ID_OR_URL> -R qemu-project/qemu`
- **View comments**: `glab issue view <ID_OR_URL> -R qemu-project/qemu --comments`
- **Search issues**: `glab issue list -R qemu-project/qemu --search "<KEYWORDS>"`

## Report Format

Sub-agent MUST provide a summary of the GitLab issue discussion and findings in this format:

### 1. Issue Context
- **Source**: GitLab URL/ID.
- **Title**: Short issue description.
- **Reporter**: User who found it.
- **Relevant Commits**: List any commits mentioned in the issue that are related to the bug or previous attempts to fix it.

### 2. Build & Reproduction (from issue)
- **Reported Environment**: Host OS, CPU, QEMU version.
- **Build Configuration**: Required `configure` flags mentioned in the issue.
- **Reproduction CLI**: Exact QEMU command used to reproduce.

### 3. Proposed Fixes & Series
- **Proposed Fixes**: Flag any specific code snippets or logic fixes suggested in the comments.
- **Patch Series**: Note if any patch series or Merge Requests have been linked.

### 4. Discussion Summary
- **Current Consensus**: What is the community's current understanding of the bug?
- **Key Constraints**: Note any blockers, requirements, or specific feedback from maintainers.
- **Next Steps**: What is needed to move the issue forward?

## Rules
- **No Independent Analysis**: Do not perform your own root cause analysis. Summarize ONLY what is present in the issue tracker.
- **Terse**: Use brief technical English.
- **Links**: Provide direct links to relevant comments or patches if available.
