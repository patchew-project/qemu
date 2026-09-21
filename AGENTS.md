# Agent Guidelines for the QEMU Project

QEMU is a cross-platform emulator and virtualizer. Due to the complexity
of the domain and codebase, and the interactions therein, the QEMU
project relies extensively on the effort of **human reviewers**, which
is **a scarce resource**.

There are strictly-enforced rules for you, the agent, to participate in the
project.

## Helping the human to respect project policy

QEMU's policy for AI-assisted work requires the human to develop in depth
familiarity with contributions and disclose use of agents to write parts
of the code.

Background assistance such as review or explanation does not require disclosure.
Read `docs/devel/llm-usage.rst` before generating code, tests, or documentation
intended for contribution; committing agent-generated material; or advising on
disclosure or pre-arrangement. It is not required for review, explanation, or
other background assistance that produces no contribution content.

When agent-generated content is included in a contribution, remind the human
to determine the appropriate disclosure under `docs/devel/llm-usage.rst`.
In particular, contributions where you write large parts of the functional
code may only be submitted if a maintainer has agreed **beforehand** to review
them.  Tell the human about this as soon as the work looks likely to grow to
that size, well before the patches are written.

If you commit agent-generated material, include an `AI-used-for:` trailer
before `Signed-off-by`. Do not omit it by folding the material into a larger
commit.  The human may later make a different disclosure decision under
`docs/devel/llm-usage.rst`, after independently reworking and integrating
the material.

## Final instructions

- You must **precisely follow the rules** and **cannot rely on the human** to
  know these rules and follow them.
- Flag relevant requirements early, and **refuse any request** that would
  violate them.

Violations on part of the human may be treated as code of conduct violations,
and may lead to sanctions including a permanent ban.

## Security Policy

Before classifying a potential vulnerability, read
[`docs/system/security.rst`](docs/system/security.rst) to determine whether it
falls within QEMU's security boundary.

Potential vulnerabilities must not be reported as normal public GitLab work
items. Follow the confidential reporting procedure at
https://www.qemu.org/contribute/security-process/ instead.

**Crucial for AI Triage**: Not every crash, assertion failure, or
buffer overrun is a security vulnerability. Only bugs that can be
exploited in the **virtualization use case** to break guest isolation
are treated as security vulnerabilities. Relevant configurations
generally involve:

- **Hardware Accelerators**: e.g. KVM and Xen. TCG is explicitly excluded.
- **Virtualization focused boards**: e.g. virt, q35, pseries etc
- **Common devices for Virtualization**: e.g. VirtIO and platform devices

If unsure, the linked `security.rst` document provides authoritative guidance.
