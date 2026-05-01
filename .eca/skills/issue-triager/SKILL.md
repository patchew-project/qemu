---
name: issue-triager
description: can help triaging GitLab issues for the QEMU project
---

# Instructions

This skill provides specialized instructions for triaging GitLab issues for the QEMU project.

## Goal
Automate the initial triage of new bug reports and feature requests in the QEMU GitLab repository.

## Prerequisites
- `glab` CLI tool installed.
- GitLab API Token: Use `env GITLAB_TOKEN=$(pass gitlab-api)` for all `glab` commands.
- Target Repo: Use `-R qemu-project/qemu` if not in a clone, or ensure the remote is set correctly.

## Asset Management (Label Cache)
This skill uses a cached list of labels to avoid unnecessary API calls and ensure consistent labeling.
- **Cache Location:** `assets/labels.txt` relative to this skill.
- **Updating the Cache:** If the user asks to update the labels, or if you suspect a label is missing, run the provided script:
  ```bash
  cd .eca/skills/issue-triager/scripts && ./update_labels.sh
  ```
- **Using the Cache:** Before applying labels, ALWAYS read `assets/labels.txt` (or use `grep` on it) to review the available labels and their descriptions. This ensures you use the exact spelling and understand the intent behind the label (e.g., `kind::Bug`, `subsystem::block`). Do NOT guess label names.

## Triage Workflow

### 1. Information Gathering
Fetch the issue details:
```bash
env GITLAB_TOKEN=$(pass gitlab-api) glab issue view <issue-id> -R qemu-project/qemu --comments
```

### 2. Evaluate Completeness
Analyze the issue against the bug template requirements:
- **Host Arch/OS**: Is the host environment specified?
- **Guest Arch/OS**: Is the guest environment specified?
- **QEMU Version**: Is the version mentioned?
- **Reproduction Steps**: Are there clear steps to reproduce?
- **Expected vs Actual**: Is the bug clearly described?

**Actions:**
- If critical information is missing (especially repro steps), add the `Workflow::Needs Info` label and post a polite comment asking for the missing details.
- If the issue is well-defined, proceed to categorization.

### 3. Categorization & Labeling
Apply labels based on the issue content. **Crucially, consult `assets/labels.txt` to find the exact matching labels for the categories below.**

#### Kinds
- `kind::Bug`: For unexpected behavior.
- `kind::Feature Request`: For new functionality.
- `kind::Cleanup`: For code refactoring or style issues.

#### Targets (target:*)
Detect the guest architecture being used (e.g., `target:arm`, `target:riscv`, `target:i386`).

#### Accelerators (accel:*)
Detect the accelerator mentioned (e.g., `accel:kvm`, `accel:tcg`, `accel:hvf`).

#### Subsystems (subsystem:*)
Identify the relevant subsystem (e.g., `subsystem:block`, `subsystem:net`, `subsystem:virtio`, `subsystem:migration`).

#### Testcases
- If the issue provides a minimal C program, a shell script, or a specific disk image to reproduce the bug, apply the `Testcase` label.

### 4. Updating the Issue
Apply the labels and optionally assign a priority if clear:
```bash
env GITLAB_TOKEN=$(pass gitlab-api) glab issue update <issue-id> -R qemu-project/qemu --label "kind::Bug,target:arm,Workflow::Triaged"
```

## Guidelines
- Be polite and professional in comments.
- Use `Workflow::Triaged` once categorization is complete.
- Avoid assigning issues to specific people unless they are explicitly mentioned or are the known maintainer for a very specific subsystem.
- Use the `scripts/get_maintainer.pl` logic (via file paths mentioned in the issue) to identify potential subsystems.
