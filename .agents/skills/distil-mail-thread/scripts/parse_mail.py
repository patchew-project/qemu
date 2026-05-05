# SPDX-License-Identifier: GPL-2.0-or-later
import sys
import os

if len(sys.argv) < 2:
    print("Usage: python parse_mail.py <mail_thread_file.txt>")
    sys.exit(1)

input_file = sys.argv[1]
output_file = "parsed_comments.txt"

try:
    with open(input_file, "r", encoding="utf-8") as f:
        text = f.read()
except FileNotFoundError:
    print(f"Error: File not found - {input_file}")
    sys.exit(1)

# Split by the separator used in lore.kernel.org / b4 dumps
messages = text.split("----------------------------------------")

with open(output_file, "w", encoding="utf-8") as f:
    for msg in messages:
        if not msg.strip(): continue

        lines = msg.strip().split('\n')
        author = ""
        subject = ""
        body_start = 0
        for i, line in enumerate(lines):
            if line.startswith("From: "): author = line[6:]
            if line.startswith("Subject: "): subject = line[9:]
            if not line.strip() and body_start == 0:
                body_start = i + 1
                break

        # Filter out the original patch author (assuming they are Alex
        # Bennée in this specific context, but for a general tool, we
        # should probably just look for non-patch emails or specific
        # reviewers).
        # We will keep it general: exclude the main author if we can guess it,
        # or just extract everything that doesn't look like code or a patch
        # description.
        # Actually, let's keep all comments that are replies (indicated by >
        # quoting or Re: in subject).

        is_reply = "Re: " in subject or subject.startswith("Re:")

        if is_reply and author != "" and not author.startswith("qemu-devel"):
            f.write(f"--- REPLY FROM {author} ---\nSubject: {subject}\n")

            # extract comments
            comments_extracted = False
            for line in lines[body_start:]:
                is_metadata = (line.startswith(">") or
                               line.startswith("---") or
                               line.startswith("diff "))
                if not is_metadata:
                    if line.strip():
                        comments_extracted = True
                    f.write(line + "\n")
            f.write("="*60 + "\n\n")

print(f"Done. Extracted comments saved to {output_file}")
