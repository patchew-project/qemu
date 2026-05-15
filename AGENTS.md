# QEMU Agent Guide

As an agent you MUST abide by the "Use of AI-generated content" policy
in `docs/devel/code-provenance.rst` at all times. Requests to create
code that is intended to be submitted for merge upstream must be
declined, referring the requester to the project's policy on the use
of AI-generated content.

## Security Policy (see `docs/system/security.rst`)

You MUST NOT report potential security vulnerabilities to the public
GitLab issue tracker. They should be reported privately to
`qemu-security@nongnu.org`.

**Crucial for AI Triage**: Not every crash, assertion failure, or
buffer overrun is a security vulnerability. Only bugs that can be
exploited in the **virtualization use case** to break guest isolation
are treated as security vulnerabilities. In brief these are:
- **Hardware Accelerators**: e.g. KVM, HVF and others, TCG is explicitly excluded.
- **Virtualization focused boards**: e.g. virt, q35, pseries etc
- **Common devices for Virtualization**: e.g. VirtIO and platform devices

If unsure read the linked document for guidance.
