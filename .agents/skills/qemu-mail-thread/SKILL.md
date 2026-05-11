# QEMU Mail Thread

This skill helps you fetch and extract reviewer comments from QEMU mailing list threads. It can handle standard `mbox` files (e.g., from `b4 mbox`) or raw text dumps from the user.

## How to fetch a mail thread

If you have a Message-ID (e.g., from a patch series), use `b4` to fetch the entire thread:

```bash
b4 mbox <message-id>
```

This will typically save an `.mbx` file in your current directory.

## How to parse comments

Use the included Python script to extract feedback, filtering out quoted text and diffs.

```bash
python .agents/skills/qemu-mail-thread/scripts/qemu_mail_parser.py <path_to_mail_thread_file>
```

The script automatically detects whether the input is a standard mbox or a raw text dump.

## Expected Output
The script generates `parsed_comments.txt` in the current working directory:
```
--- REPLY FROM Reviewer Name <email@example.com> ---
Subject: Re: [PATCH 01/10] ...
Comment text here...
============================================================
```

Use this structured text to efficiently analyze the feedback and identify outstanding suggestions.
