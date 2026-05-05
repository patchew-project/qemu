---
name: qemu-issue-triage
description: Use this skill to triage and label GitLab issues for the QEMU project
license: GPL-2.0-or-later
---

# Instructions

This skill provides specialized instructions for triaging GitLab issues for the QEMU project.

## Parameter Handling & Execution Strategy (CRITICAL)
1. **Parameters**: If the user invokes this skill with an argument (e.g., `/issue-triager 3463`), treat that argument as the target `<issue-id>`.
2. **Sub-Agent Mandate**: To prevent polluting the main conversation context, **you MUST ALWAYS spawn a sub-agent** to perform the actual triage.
   - Do NOT run `glab` commands or read the label cache directly in the main context.
   - Use the `eca__spawn_agent` tool (agent: `general`, activity: `Triaging issue <id>`).
   - In the `task` parameter for the sub-agent, provide the target `<issue-id>` and explicitly instruct the sub-agent to follow the "Triage Workflow" and "Asset Management" rules defined below.
   - Wait for the sub-agent to finish and simply report its summary to the user.

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
  cd .agents/skills/qemu-issue-triage/scripts && ./update_labels.sh
  ```
- **Using the Cache:** Before applying labels, ALWAYS read `assets/labels.txt` (or use `grep` on it) to review the available labels and their descriptions. This ensures you use the exact spelling and understand the intent behind the label (e.g., `kind::Bug`, `Storage`). Do NOT guess label names.

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
- If critical information is missing (especially repro steps), add the `workflow::Needs Info` label and post a polite comment asking for the missing details. (Remember to sign the comment as "Issue Agent Bot on behalf of the user" per the Guidelines).
- If the issue is well-defined, proceed to categorization.

### 3. Categorization & Labeling
Apply labels based on the issue content. **Crucially, consult `assets/labels.txt` to find the exact matching labels for the categories below.**

#### Kinds
- `kind::Bug`: For unexpected behavior.
- `kind::Feature Request`: For new functionality.
- `kind::Task`: For research, investigations, and miscellaneous issues.

#### Targets (target: *) and Hosts (host: *)
Detect the guest architecture (`target: *`) or host environment (`host: *`).
**IMPORTANT:** Be conservative when applying `target:` and `host:` labels. Many bugs (e.g., in generic devices like USB, PCI, or block controllers) apply to ANY guest that includes the device. The reproducer (like a `qtest` invocation) might just use a convenient target (e.g., `i386`) as an example. ONLY apply `target:` or `host:` labels if the bug is strictly architecture- or host-dependent (e.g., a bug in ARM CPU emulation, or a macOS-specific build failure).

#### Accelerators (accel: *)
Detect the accelerator mentioned (e.g., `accel: KVM`, `accel: TCG`, `accel: HVF`).

#### Subsystems
Identify the relevant subsystem (e.g., `Storage`, `Networking`, `device:virtio`, `Migration`).

#### Testcases
- If the issue provides a minimal C program, a shell script, or a specific disk image to reproduce the bug, apply the `TestCase` label.

#### Patches and Fixes
- If the issue description or comments contain a link to a patch on the mailing list (e.g., `lore.kernel.org`, `patchew.org`), or explicitly mention that a patch/fix has been submitted, apply the `workflow::Patch available` label.

### 4. Updating the Issue
Apply the labels and optionally assign a priority if clear:
```bash
env GITLAB_TOKEN=$(pass gitlab-api) glab issue update <issue-id> -R qemu-project/qemu --label "kind::Bug,target: arm,workflow::Triaged"
```

## Guidelines
- Be polite and professional in comments.
- **IMPORTANT:** Any comments added to the issue MUST include the phrase: "Issue Agent Bot on behalf of the user" (e.g., as a sign-off at the end of the message).
- Use `workflow::Triaged` once categorization is complete.
- Avoid assigning issues to specific people unless they are explicitly mentioned or are the known maintainer for a very specific subsystem.
- Use the `scripts/get_maintainer.pl` logic (via file paths mentioned in the issue) to identify potential subsystems.
