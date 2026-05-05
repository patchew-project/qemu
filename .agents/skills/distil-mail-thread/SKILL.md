---
name: distil-mail-thread
description: Extract and summarize reviewer comments from a QEMU or kernel mailing list thread dump (like a b4 mbox or text export). Use this when the user asks to "distil", "parse", or "extract feedback" from a mail thread file.
license: GPL-2.0-or-later
---

# Distil Mail Thread

This skill helps you extract reviewer comments and feedback from a long mailing list thread file, filtering out quoted text, patch diffs, and headers. It relies on a Python script included in this skill's `scripts/` directory.

## How to use this skill

1. **Locate the target file**: Identify the mail thread file the user wants to parse (e.g., `wxft-rfc-mail-thread.txt`).
2. **Execute the script**: Run the included Python script against the file. The script is located in the `scripts/` directory of this skill.

   ```bash
   python /path/to/distil-mail-thread/scripts/parse_mail.py <path_to_mail_thread_file.txt>
   ```
   *(Note: Adjust the path to the script based on where this skill is installed. You can use your filesystem tools to locate `distil-mail-thread/scripts/parse_mail.py`.)*

3. **Read the output**: The script will generate a file named `parsed_comments.txt` in the current working directory. Use your file reading tools to examine its contents.
4. **Analyze and Summarize**: Read through the extracted comments and provide a structured summary to the user, correlating feedback to specific patches if necessary.

## Expected Output
The `parsed_comments.txt` will look like this:
```
--- REPLY FROM Reviewer Name <email@example.com> ---
Subject: Re: [PATCH 01/10] ...
Comment text here...
============================================================
```

Use this structured text to efficiently analyze the feedback.
