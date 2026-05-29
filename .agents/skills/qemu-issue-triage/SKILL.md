---
name: qemu-issue-triage
description: Triage and label GitLab issues for QEMU project.
license: GPL-2.0-or-later
---

# Instructions

Triage GitLab issues for QEMU project.

## Parameter Handling & Execution Strategy (CRITICAL)
1. **Multi-ID**: If multiple IDs (e.g., `#3430, #3426`), treat each as independent.
2. **Sub-Agent MANDATE**: NEVER triage in main context. ALWAYS spawn sub-agents to avoid context pollution.
   - **Parallel**: One sub-agent per issue ID in parallel.
   - Do NOT run `glab` or read label cache in main context.
   - Pass `<issue-id>` in `task` argument, instruct sub-agent to follow triage workflow/asset rules.
   - Report sub-agent summary back to user.

## Goal
Automate initial triage of new bugs and feature requests in QEMU GitLab repo.

## Prerequisites
- `glab` CLI tool.
- Auth check: `glab auth status`. Set token via `env GITLAB_TOKEN=<token>` if needed.
- Repo: `-R qemu-project/qemu`.

## Asset Management (Label Cache)
Cache list of labels avoids API calls.
- **Cache Location:** `assets/labels.txt` relative to skill.
- **Update Cache:** If requested or outdated, run script:
  ```bash
  cd .agents/skills/qemu-issue-triage/scripts && ./update_labels.sh
  ```
- **Use Cache**: ALWAYS read `assets/labels.txt` (or grep) before labeling. Do NOT guess names. Use exact casing/spelling.

## Triage Workflow

### 1. Information Gathering
```bash
glab issue view <issue-id> -R qemu-project/qemu --comments
```

### 2. Evaluate Completeness
Check template: Host Arch/OS, Guest Arch/OS, QEMU Version, Repro Steps, Expected vs Actual.

**workflow::Needs Info Triggers (CRITICAL):**
Request info and apply `workflow::Needs Info` label if:
- **Missing Command Line**: No full reproduction command (`qemu-system-* ...`).
- **Old QEMU Version**: Version older than last two major releases.
  - **CRITICAL**: No guess/hardcode. Read `VERSION` + `git tag -l "v*"` for real releases (e.g., `11.0.50` means `11.0.x` released, `11.1.0` in dev). Ask to re-test with master/latest stable found.
- **Distro Version**: Version has distro-specific suffix (e.g., `.el9`, `.fc40`, `-ubuntu`, `7.2.0-14.sc05...`). Ask to reproduce with clean upstream source build.

**Actions:**
- If info missing, add `workflow::Needs Info` label and comment politely asking for missing details. Sign comment as "Issue Agent Bot on behalf of the user".
- If issue is complete and on recent upstream version, proceed to categorization.

### 3. Categorization & Labeling
Add labels. Consult `assets/labels.txt` for exact names.

#### Scoped Labels (Prefix::Label)
Mutual exclusion: only one label per scope. Adding new removes old.
- **Prefixes:** `kind::`, `workflow::`, `Closed::`, `Audit Tooling::`, `GUI::`.

#### Kinds
- `kind::Bug`: Bad behavior.
- `kind::Feature Request`: New function.
- `kind::Task`: Research/investigations.

#### Targets (`target: *`) and Hosts (`host: *`)
Be conservative. Only apply if bug strictly architecture- or host-dependent (e.g., ARM CPU emulation, macOS-specific build). Do not label target/host if bug is in generic device (USB, PCI, block) that happens to be run on that target.

#### Accelerators (`accel: *`)
E.g., `accel: KVM`, `accel: TCG`, `accel: HVF`.

#### Subsystems
E.g., `Storage`, `Networking`, `device:virtio`, `Migration`.

#### Testcases
- Apply `TestCase` if minimal C program, shell script, or disk image provided.

#### Patches and Fixes
- Apply `workflow::Patch available` if link to patch on mailing list (`lore.kernel.org`, `patchew.org`) or patch mentioned.

#### Workflow Management
One `workflow::` label at a time. Transitioning auto-removes old (e.g., adding `workflow::Patch available` removes `workflow::Triaged`).

### 4. Update Issue
Apply labels. E.g.:
```bash
glab issue update <issue-id> -R qemu-project/qemu --label "workflow::Triaged,kind::Bug"
```

## Guidelines
- Comments MUST end with: "Issue Agent Bot on behalf of the user".
- Avoid comments unless info needed. No "thank you" comments.
- Apply `workflow::Triaged` once categorized.
- Do NOT assign to people unless explicitly requested or known maintainer.
- Use `scripts/get_maintainer.pl` on path to find subsystem.
- **No deep code dives**: No deep code dive. No long root-cause search. Focus labels and metadata.
- **Do NOT run tests**: No build, no test. Metadata triage only.
