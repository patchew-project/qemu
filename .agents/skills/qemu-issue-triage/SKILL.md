---
name: qemu-issue-triage
description: Use this skill to triage and label GitLab issues for the QEMU project
license: GPL-2.0-or-later
---

# Instructions

This skill provides specialized instructions for triaging GitLab issues for the QEMU project.

## Parameter Handling & Execution Strategy (CRITICAL)
1. **Parameters**: If the user invokes this skill with multiple arguments or a list of IDs (e.g., `#3430, #3426`), treat each as an independent target.
2. **Sub-Agent Mandate**: To prevent polluting the main conversation context, **you MUST ALWAYS spawn a sub-agent** to perform the actual triage.
   - **Parallelization**: When multiple IDs are provided, spawn one sub-agent per issue in parallel to improve efficiency.
   - Do NOT run `glab` commands or read the label cache directly in the main context.
   - In the `task` parameter for the sub-agent, provide the target `<issue-id>` and explicitly instruct the sub-agent to follow the "Triage Workflow" and "Asset Management" rules defined below.
   - Wait for the sub-agent to finish and simply report its summary to the user.

## Goal
Automate the initial triage of new bug reports and feature requests in the QEMU GitLab repository.

## Prerequisites
- `glab` CLI tool installed.
- An authentication method: Use `glab auth status` to check. You can pass a token by using `env GITLAB_TOKEN=<token>` in front of commands if you have it.
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
glab issue view <issue-id> -R qemu-project/qemu --comments
```

### 2. Evaluate Completeness
Analyze the issue against the bug template requirements:
- **Host Arch/OS**: Is the host environment specified?
- **Guest Arch/OS**: Is the guest environment specified?
- **QEMU Version**: Is the version mentioned?
- **Reproduction Steps**: Are there clear steps to reproduce?
- **Expected vs Actual**: Is the bug clearly described?

**Workflow::Needs Info Triggers (CRITICAL):**
Request more information and apply the `workflow::Needs Info` label if any of the following are true:
- **Missing Command Line**: The full QEMU command line (`qemu-system-* ...`) used to reproduce the issue is missing.
- **Old QEMU Version**: The reported version is older than the last two major releases. Ask the reporter to re-test with the current upstream master or the latest stable release.
- **Distro Version**: The version string suggests a downstream/distro-specific package (e.g., contains suffixes like `.el9`, `.fc40`, `-ubuntu`, or long strings like `7.2.0-14.sc05...`). Ask the reporter to reproduce the issue using a clean build from the current upstream source to rule out distro-specific patches.

**Actions:**
- If information is missing based on the triggers above, add the `workflow::Needs Info` label and post a polite comment asking for the specific missing details. (Remember to sign the comment as "Issue Agent Bot on behalf of the user" per the Guidelines).
- If the issue is well-defined and uses a recent upstream version, proceed to categorization.

### 3. Categorization & Labelling
Apply labels based on the issue content. **Crucially, consult `assets/labels.txt` to find the exact matching labels for the categories below.**

#### Scoped Labels (Prefix::Label)
QEMU uses scoped labels (e.g., `kind::Bug`, `workflow::Triaged`) to group related categories.
- **Prefixes:** Common prefixes include `kind::`, `workflow::`, `Closed::`, `Audit Tooling::`, `GUI::`.
- **Constraint:** Only one label from that scope can be active at a time. Applying a new scoped label of the same type (e.g `workflow::`) will remove the other.

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

#### Workflow Management
The `workflow::` labels track the lifecycle of an issue.
- **Single Workflow Label:** An issue can only have one `workflow::` label at a time.
- **Transitioning:** Adding a new workflow label will automatically remove the old one (e.g., adding `workflow::Patch available` will remove `workflow::Triaged`).

#### Other comments
- If other developers have commented see if those comments imply additional tags should be applied.

### 4. Updating the Issue
Apply the labels and optionally assign a priority if clear.
**Transitioning Workflow Example:**
```bash
glab issue update <issue-id> -R qemu-project/qemu --label "workflow::Triaged,kind::Bug"
```

## Guidelines
- Be polite and professional in comments.
- **IMPORTANT:** Any comments added to the issue MUST include the phrase: "Issue Agent Bot on behalf of the user" (e.g., as a sign-off at the end of the message).
- Avoid commenting unless additional information is needed, specifically a comment to acknowledge the report is superfluous.
- Use `workflow::Triaged` once categorization is complete.
- Avoid assigning issues to specific people unless they are explicitly mentioned or are the known maintainer for a very specific subsystem.
- Use the `scripts/get_maintainer.pl` logic (via file paths mentioned in the issue) to identify potential subsystems.
