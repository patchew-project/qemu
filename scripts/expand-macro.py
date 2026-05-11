#!/usr/bin/env python3
#
# Automate the expansion of QEMU macros based on compile_commands.json.
#
# This script runs the C preprocessor over a file to expand macros
# in a specified line range, using the compilation flags defined in
# compile_commands.json.
#
# Copyright (c) Linaro 2026
#
import os
import sys
import json
import shlex
import subprocess
import argparse
import re


def find_compile_command(target_file, compile_commands):
    """
    Search compile_commands to find the rule to build target_file
    """
    target_abs = os.path.abspath(target_file)
    for entry in compile_commands:
        dir_path = entry.get('directory', '.')
        file_abs = os.path.abspath(os.path.join(dir_path, entry['file']))
        if file_abs == target_abs:
            return entry
    return None


def process_command(command_entry):
    """
    Strip out output related options and return a command line that will
    run the pre-processor only.
    """
    command = command_entry.get('command')
    if not command:
        args = command_entry.get('arguments', [])
    else:
        args = shlex.split(command)

    if not args:
        return None

    out = []
    it = iter(args)
    for arg in it:
        # the -M* options all deal with generating deps
        if arg in ('-o', '-MF', '-MQ', '-MT', '-MD', '-MP'):
            next(it, None)  # Skip the option's argument
            continue
        if arg == '-c':
            continue
        out.append(arg)

    # Enable pre-processor output, don't strip comments, trace includes
    out.extend(['-E', '-CC', '-H'])
    return out


def normalize_path(raw_path, working_dir):
    """Normalize and make paths absolute."""
    if not os.path.isabs(raw_path):
        return os.path.abspath(os.path.join(working_dir, raw_path))
    return os.path.normpath(raw_path)


class PreprocessorState:
    """Tracks the state of the preprocessor as we parse its output."""
    def __init__(self):
        self.stack = []
        self.current_path = None
        self.current_line = 0
        self.current_instance_id = 0
        self.next_instance_id = 1
        self.sections = {}

    def update_on_marker(self, new_line, flags, path):
        """Update the file stack and instance tracking based on markers."""
        # entering new file
        if "1" in flags:
            if self.current_path is not None:
                self.stack.append((self.current_path, self.current_line,
                                   self.current_instance_id))
            self.current_path = path
            self.current_line = new_line
            self.current_instance_id = self.next_instance_id
            self.next_instance_id += 1
            return

        # leaving file
        if "2" in flags:
            if self.stack:
                _, _, popped_instance_id = self.stack.pop()
                self.current_path = path
                self.current_line = new_line
                self.current_instance_id = popped_instance_id
            else:
                self.current_path = path
                self.current_line = new_line
                self.current_instance_id = self.next_instance_id
                self.next_instance_id += 1
            return

        # return to previous file without explicit flag 2
        if self.current_path != path:
            if self.stack and self.stack[-1][0] == path:
                _, _, popped_instance_id = self.stack.pop()
                self.current_path = path
                self.current_line = new_line
                self.current_instance_id = popped_instance_id
            else:
                self.current_path = path
                self.current_line = new_line
                self.current_instance_id = self.next_instance_id
                self.next_instance_id += 1
            return
            
        self.current_line = new_line

    def get_context_string(self, target_abs, working_dir):
        """Generate a descriptive string showing the inclusion context."""
        if self.stack:
            ctx_path, ctx_line, _ = self.stack[-1]
            try:
                rel_ctx = os.path.relpath(ctx_path, working_dir)
            except ValueError:
                rel_ctx = ctx_path
            return f"{rel_ctx}:{ctx_line}"

        try:
            rel_ctx = os.path.relpath(target_abs, working_dir)
        except ValueError:
            rel_ctx = target_abs
        return f"{rel_ctx} (main file)"

    def add_line(self, line, line_range, target_abs, working_dir):
        """Add a line to the sections if it is within the requested range."""
        start_line, end_line = line_range
        if self.current_path == target_abs:
            if start_line <= self.current_line <= end_line:
                if self.current_instance_id not in self.sections:
                    ctx_str = self.get_context_string(target_abs, working_dir)
                    self.sections[self.current_instance_id] = {
                        "context": ctx_str,
                        "lines": []
                    }
                self.sections[self.current_instance_id]["lines"].append(line)
        self.current_line += 1


def format_output_sections(sections, target_file, start_line, end_line):
    """Format the accumulated sections into the final output string."""
    output_sections = []
    for _instance_id, data in sections.items():
        if not data["lines"]:
            continue
        header = f"/* Expansion from {data['context']} */"
        body = "\n".join(data["lines"])
        output_sections.append(f"{header}\n{body}")

    if not output_sections:
        return (f"/* Error: No lines found for {target_file} "
                f"in range {start_line}-{end_line} */")

    return "\n/* end of expansion */\n".join(output_sections)


def extract_range(stdout, target_file, start_line, end_line, working_dir):
    """
    Parse the output of the pre-processor while tracking where we
    are in the source code from the markers so we can extract the
    range asked for.
    """
    state = PreprocessorState()
    target_abs = os.path.abspath(target_file)
    line_range = (start_line, end_line)

    # The format is undocumented but see:
    #
    #  gcc/c-family/c-ppoutput.c:print_line_1
    #
    # where 1 = entering file, 2 = leaving file
    # and the 3 or 3 4 depends on linemap_location_in_system_header_p
    line_marker_re = re.compile(r'^# (\d+) "(.*?)"(.*)')

    for line in stdout.splitlines():
        match = line_marker_re.match(line)
        if match:
            new_line = int(match.group(1))
            raw_path = match.group(2)
            flags = match.group(3).split()

            path = normalize_path(raw_path, working_dir)
            state.update_on_marker(new_line, flags, path)
            continue

        state.add_line(line, line_range, target_abs, working_dir)

    return format_output_sections(state.sections, target_file,
                                  start_line, end_line)


def main():
    """Main entry point for the script."""
    desc = 'Expand macros in a section of a file using compile_commands.json'
    parser = argparse.ArgumentParser(description=desc)
    parser.add_argument('file', help='Source file to expand macros in')
    parser.add_argument('--range', help='Line range (e.g. 100-120)')

    ctx_help = ('Context file (.c) to get compilation flags from '
                '(useful for headers)')
    parser.add_argument('--context', help=ctx_help)
    parser.add_argument('--compile-commands', default='compile_commands.json',
                        help='Path to compile_commands.json')
    parser.add_argument('--show-command', action='store_true',
                        help='Print the modified compile command and exit')

    args = parser.parse_args()

    if not os.path.exists(args.compile_commands):
        print(f"Error: {args.compile_commands} not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.compile_commands, encoding="utf-8") as f:
        compile_commands = json.load(f)

    query_file = args.context if args.context else args.file
    entry = find_compile_command(query_file, compile_commands)

    if not entry:
        print(f"Error: Could not find compile command for {query_file}",
              file=sys.stderr)
        sys.exit(1)

    cmdline = process_command(entry)
    if not cmdline:
        print(f"Error: Failed to process command for {query_file}",
              file=sys.stderr)
        sys.exit(1)

    if args.show_command:
        print(shlex.join(cmdline))
        sys.exit(0)

    working_dir = entry.get('directory', '.')
    result = subprocess.run(cmdline, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, cwd=working_dir,
                            universal_newlines=True, check=False)

    if result.returncode != 0:
        print(f"Preprocessor failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(result.returncode)

    content = result.stdout
    if args.range:
        try:
            start, end = map(int, args.range.split('-'))
            content = extract_range(content, args.file, start, end,
                                    working_dir)
        except ValueError:
            print(f"Error: Invalid range format {args.range}. Use start-end.",
                  file=sys.stderr)
            sys.exit(1)

    print(content)


if __name__ == "__main__":
    main()
