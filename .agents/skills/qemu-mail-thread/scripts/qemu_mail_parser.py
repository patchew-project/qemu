# SPDX-License-Identifier: GPL-2.0-or-later
import sys
import os
import mailbox


def is_metadata_line(line):
    """Check if a line is metadata (quotes, diff, etc.)"""
    return (line.startswith(">") or
            line.startswith("---") or
            line.startswith("diff "))


def parse_raw_text(text, output_f):
    # Split by the separator used in lore.kernel.org / b4 dumps
    messages = text.split("----------------------------------------")
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

        is_reply = subject and ("Re: " in subject or subject.startswith("Re:"))

        if is_reply and author != "" and not author.startswith("qemu-devel"):
            output_f.write(f"--- REPLY FROM {author} ---\nSubject: {subject}\n")

            for line in lines[body_start:]:
                if not is_metadata_line(line):
                    output_f.write(line + "\n")
            output_f.write("="*60 + "\n\n")


def parse_mbox(mbox_path, output_f):
    mbox = mailbox.mbox(mbox_path)
    for message in mbox:
        subject = message['subject']
        if subject and 'Re: ' in subject:
            author = message['from']
            output_f.write(f"--- REPLY FROM {author} ---\nSubject: {subject}\n")

            payload = message.get_payload()
            body = ""
            if isinstance(payload, list):
                # Handle multipart
                for part in payload:
                    if part.get_content_type() == 'text/plain':
                        body = part.get_payload(decode=True).decode('utf-8', errors='ignore')
                        break
            else:
                body = message.get_payload(decode=True).decode('utf-8', errors='ignore')

            # Simple heuristic to extract comments
            for line in body.split('\n'):
                if line.strip() and not is_metadata_line(line.strip()):
                    output_f.write(line + "\n")
            output_f.write("="*60 + "\n\n")


def main():
    if len(sys.argv) < 2:
        print("Usage: python qemu_mail_parser.py <mail_thread_file>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = "parsed_comments.txt"

    if not os.path.exists(input_file):
        print(f"Error: File not found - {input_file}")
        sys.exit(1)

    with open(output_file, "w", encoding="utf-8") as out_f:
        # Detect if it's an mbox or raw text
        with open(input_file, 'rb') as f:
            header = f.read(15)
            is_mbox = header.startswith(b'From mboxrd@z ')

        if is_mbox:
            print(f"Parsing {input_file} as mbox...")
            parse_mbox(input_file, out_f)
        else:
            print(f"Parsing {input_file} as raw text dump...")
            with open(input_file, "r", encoding="utf-8", errors='ignore') as f:
                text = f.read()
            parse_raw_text(text, out_f)

    print(f"Done. Extracted comments saved to {output_file}")

if __name__ == "__main__":
    main()
