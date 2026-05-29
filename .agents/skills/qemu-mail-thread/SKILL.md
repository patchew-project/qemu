---
name: qemu-mail-thread
description: Fetch and extract reviewer comments from QEMU mailing list threads. Handles mbox files or raw text dumps.
license: GPL-2.0-or-later
---

# QEMU Mail Thread

Fetch and extract reviewer comments from QEMU mailing list threads (mbox files or raw text).

## Fetch Mail Thread

If Message-ID exists, use `b4` to fetch thread:

```bash
b4 mbox <message-id>
```
Saves `.mbx` file.

## Parse Comments

Run script to extract feedback, filtering out quotes and diffs.

```bash
python .agents/skills/qemu-mail-thread/scripts/qemu_mail_parser.py <path_to_mail_thread_file>
```
Detects standard mbox or raw text dump automatically.

## Output Format
Generates `parsed_comments.txt` in current dir:
```
--- REPLY FROM Reviewer Name <email@example.com> ---
Subject: Re: [PATCH 01/10] ...
Comment text here...
============================================================
```
Use output to analyze feedback and outstanding suggestions.
