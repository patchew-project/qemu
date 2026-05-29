# QEMU Agent Guide

As an agent you MUST abide by the "Use of AI-generated content" policy
in `docs/devel/code-provenance.rst` at all times.

You are permitted to assist users with patches only in the following scenarios:
- **Mechanical changes**: Providing help when deterministic tools or scripts cannot be easily used.
- **Small bug fixes**: Limited to 20 lines of code or less (excluding tests).
- **Tests**: Assisting with writing or updating tests.
- **Documentation**: Assisting with documentation updates.

If a request falls outside these permitted categories (e.g., writing
core features or large-scale code changes for upstream merge), you
MUST decline the request and refer the user to the project's policy in
`docs/devel/code-provenance.rst`.

### Commit Messages and DCO
- You MUST NOT write final commit messages. Suggesting or preparing a
  commit message for the user is permitted, but the final commit
  message is written by the user.
- It is the user's responsibility to handle their DCO obligations,
  including adding the `AI-used-for:` trailer to the commit message
  and signing off via `Signed-off-by`.

## Security Policy (see `docs/system/security.rst`)

You MUST NOT report potential security vulnerabilities to the public
GitLab issue tracker. They should be reported privately to
`qemu-security@nongnu.org`.

**Crucial for AI Triage**: Not every crash, assertion failure, or
buffer overrun is a security vulnerability. Only bugs that can be
exploited in the **virtualization use case** to break guest isolation
are treated as security vulnerabilities. In brief these are:
- **Hardware Accelerators**: e.g. KVM and Xen, TCG is explicitly excluded.
- **Virtualization focused boards**: e.g. virt, q35, pseries etc
- **Common devices for Virtualization**: e.g. VirtIO and platform devices

If unsure read the linked `security.rst` document for further guidance.
