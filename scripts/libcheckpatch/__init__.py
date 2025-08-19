#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""

Check patches for submission.

Copyright (c) 2025 Linaro Ltd.

Authors:
 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>

History
=======

This file has been adapted from the Perl script checkpatch.pl included in the
QEMU tree, which was in turn imported verbatim from the Linux kernel tree in
2011. Over the years it was adapted for QEMU-specific checks.

Design / How to implement new checks
====================================

This script tries to detect the format of each file changed in a patch
according to its filename suffix and/or file path. Then, it performs checks
specific to the file format.

Each file format corresponds to a class that inherits from `FileFormat` class.
Each staticmethod whose name starts with `check_` is automatically executed for
each file diff (i.e. collection of hunks) for each patch.

To add new checks, simply add a new `check_DESCRIPTIVE_CHECK_NAME` staticmethod
in the class of the appropriate file format.

To "throw" a warning or an error, this script (ab)uses Python's warnings
feature. Warnings can be thrown freely by default by creating an `Error` or
`Warn` instance and collected using a context manager without breaking
execution of tests:

    # Throw error
    Error("line over 90 characters")
    # Or warn
    # Warn("line over 80 characters")

"""

# pylint: disable=pointless-exception-statement

import argparse
import os
import pathlib
import subprocess
import sys
import warnings

from .patch import *
from .checks import *
from .output import *


def top_of_kernel_tree(path: pathlib.Path) -> bool:
    """Verify path is the root directory of project"""
    for item in [
        "COPYING",
        "MAINTAINERS",
        "Makefile",
        "README.rst",
        "docs",
        "VERSION",
        "linux-user",
        "system",
    ]:
        if not (path / item).exists():
            return False
    return True


def main():
    """
    Read CLI arguments and print result to stdout
    """
    parser = argparse.ArgumentParser(prog="checkpatch.py")
    parser.add_argument(
        "FILE", type=argparse.FileType("r"), action="extend", nargs="*"
    )
    parser.add_argument("--version", action="version", version="%(prog)s 1.0")
    parser.add_argument("-q", "--quiet", action="store_true")
    parser.add_argument(
        "--no-tree", action="store_true", help="run without a qemu tree"
    )
    parser.add_argument(
        "--no-signoff",
        dest="signoff",
        action="store_false",
        help="do not check for 'Signed-off-by' line",
    )
    # TODO:
    # parser.add_argument(
    #     "--emacs", action="store_true", help="emacs compile window format"
    # )
    parser.add_argument(
        "--terse", action="store_true", help="one line per report"
    )
    parser.add_argument(
        "--strict", action="store_true", help="fail if only warnings are found"
    )
    # TODO:
    parser.add_argument(
        "--root", type=pathlib.Path, help="PATH to the qemu tree root"
    )
    parser.add_argument(
        "--no-summary",
        action="store_true",
        help="suppress the per-file summary",
    )
    parser.add_argument(
        "--mailback",
        action="store_true",
        help="only produce a report in case of warnings/errors",
    )
    parser.add_argument(
        "--summary-file",
        action="store_true",
        default=False,
        help="include the filename in summary",
    )
    # TODO:
    # parser.add_argument(
    #     "--debug",
    #     action="store_true",
    #     help=(
    #         "KEY=[0|1]turn on/off debugging of KEY, where KEY is one of"
    #         " 'values', 'possible', 'type', and 'attr' (default is all off)"
    #     ),
    # )
    parser.add_argument(
        "--test-only",
        type=str,
        metavar="WORD",
        help="report only warnings/errors containing WORD literally",
    )

    # TODO:
    # parser.add_argument(
    #     "--codespell",
    #     action="store_true",
    #     help=(
    #         "Use the codespell dictionary for spelling/typos (default:"
    #         " $codespellfile)"
    #     ),
    # )
    # parser.add_argument(
    #     "--codespellfile",
    #     action="store_true",
    #     help="Use this codespell dictionary",
    # )
    def parse_color(s: str) -> bool | None:
        if s == "always":
            return True
        if s == "never":
            return False
        if s == "auto":
            return None
        raise ValueError("always,never,auto")

    parser.register("type", "color", parse_color)
    parser.add_argument(
        "--color",
        type="color",
        metavar="WHEN",
        default=None,
        help=(
            "Use colors 'always', 'never', or only when output is a terminal"
            " ('auto'). Default is 'auto'."
        ),
    )
    argtype_group = parser.add_mutually_exclusive_group(required=False)
    argtype_group.add_argument(
        "-f,",
        "--file",
        action="store_true",
        default=True,
        help="treat FILE as regular source file",
    )
    argtype_group.add_argument(
        "--branch", nargs="+", help="treat args as GIT revision list"
    )
    argtype_group.add_argument(
        "--patch", action="store_true", help="treat FILE as patchfile"
    )
    args = parser.parse_args()

    if args.color is None:
        args.color = sys.stdout.isatty()

    if not args.no_tree:
        if args.root:
            root = pathlib.Path(args.root)
            if not top_of_kernel_tree(root):
                print("--root does not point at a valid tree")
        else:
            if top_of_kernel_tree(pathlib.Path(os.getcwd())):
                root = pathlib.Path(os.getcwd())
            else:
                root = (pathlib.Path(sys.argv[0]) / ".." / "..").resolve()
        if not top_of_kernel_tree(root):
            print("Must be run from the top-level dir. of a qemu tree")
            sys.exit(2)
    else:
        root = None

    configuration = Configuration(signoff=args.signoff, root=root)

    any_error = 0

    patches = []

    if args.branch:
        git_env = {
            "GIT_CONFIG_GLOBAL": "",
            "GIT_CONFIG_SYSTEM": "",
            "GIT_CONFIG_NOSYSTEM": "1",
        }
        if args.FILE:
            parser.error(
                "positional FILE argument cannot be used with --branch"
            )
        for revlist in args.branch:
            hashes = subprocess.run(
                ["git", "rev-list", "--reverse", revlist],
                capture_output=True,
                check=False,
                env=os.environ | git_env,
            )
            if hashes.returncode != 0:
                if hashes.stderr:
                    print("git-rev-list:", hashes.stderr.decode("utf-8"))
                parser.error(
                    f"Revision list {revlist} could not be parsed: git"
                    f" rev-list exited with {hashes.returncode}"
                )
            hashes = hashes.stdout.decode("utf-8").splitlines()
            for commit_sha in hashes:
                patch_text = subprocess.run(
                    [
                        "git",
                        "-c",
                        "diff.renamelimit=0",
                        "-c",
                        "diff.renames=True",
                        "-c",
                        "diff.algorithm=histogram",
                        "format-patch",
                        "--subject-prefix",
                        "",
                        "--patch-with-stat",
                        "--stdout",
                        f"{commit_sha}^..{commit_sha}",
                    ],
                    capture_output=True,
                    check=False,
                    env=os.environ | git_env,
                )
                if patch_text.returncode != 0:
                    if patch_text.stderr:
                        print(
                            "git-format-patch:",
                            patch_text.stderr.decode("utf-8"),
                        )
                    parser.error(
                        f"Revision {commit_sha} could not be parsed: git"
                        f" format-patch exited with {patch_text.returncode}"
                    )
                patches.append(
                    Patch(
                        configuration,
                        patch_text.stdout.decode("utf-8"),
                        sha=commit_sha,
                    )
                )
    else:
        for file in args.FILE:
            filename = file.name
            patches.append(
                Patch(configuration, file.read(), filename=filename)
            )

    for p in patches:
        output = []
        errors_no = 0
        warnings_no = 0
        lines_no = 0

        filename = p.filename or ""

        lines_no += len(p.raw_string.splitlines())
        with warnings.catch_warnings(record=True) as w:
            p.check()
            if args.strict:
                for i in w:
                    if isinstance(i.message, Warn):
                        i.message = i.message.into_error()
            output += [w.message for w in w]

        if args.test_only:
            output = [o for o in output if args.test_only in o.msg]

        for o in output:
            if isinstance(o, Warn):
                warnings_no += 1
            else:
                errors_no += 1
        any_error = errors_no
        for o in output:

            class Colors:
                WARNING = "\033[35m" if args.color else ""
                ERROR = "\033[91m" if args.color else ""
                ENDC = "\033[0m" if args.color else ""
                BOLD = "\033[1m" if args.color else ""

            if args.terse:
                print(
                    f"{Colors.BOLD}{filename}:"
                    f"{o.patch_line_no or ''}{Colors.ENDC}: ",
                    end="",
                )

            if isinstance(o, Warn):
                print(
                    f"{Colors.WARNING}{Colors.BOLD}WARNING:{Colors.ENDC} ",
                    end="",
                )
            else:
                print(
                    f"{Colors.ERROR}{Colors.BOLD}ERROR:{Colors.ENDC} ",
                    end="",
                )
            print(o.msg)
            if not args.terse and o.file_diff:
                print(
                    f"#{o.patch_line_no or ''}: FILE:"
                    f" {o.file_diff.filename_b}:{o.line_no or ''}"
                )
                if o.patch_line_no:
                    line = p.raw_string.splitlines()[o.patch_line_no - 1]

                    print(
                        line.translate(
                            str.maketrans(
                                {
                                    "\000": r"\0",
                                    "\011": r"^I",
                                }
                            )
                        )
                    )
            if not args.terse:
                print()

        if not (args.mailback and (errors_no, warnings_no) == (0, 0)):
            if args.summary_file:
                print(f"{filename} ", end="")
            print(
                f"total: {errors_no} error{'s'[:errors_no^1]},"
                f" {warnings_no} warning{'s'[:warnings_no^1]},"
                f" {lines_no} line{'s'[:lines_no^1]} checked"
            )

            if not args.no_summary and not args.terse:
                print()
                if errors_no == 0:
                    print(
                        filename,
                        "has no obvious style problems and is ready for"
                        " submission.",
                    )
                else:
                    print(
                        filename,
                        "has style problems, please review.  If any of these"
                        " errors\nare false positives report them to the"
                        " maintainer, see\nCHECKPATCH in MAINTAINERS.",
                    )
    return any_error


if __name__ == "__main__":
    sys.exit(main())
