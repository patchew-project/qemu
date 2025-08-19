# SPDX-License-Identifier: GPL-2.0-or-later

import difflib
import unittest
import warnings

from . import patch
from .output import Warn, Output


class ExpectedOutput:
    def __init__(
        self,
        /,
        msg: str,
        patch_line_no: int,
        filename: str,
        line_no: int,
        is_warning: bool = False,
    ):
        self.msg = msg
        self.patch_line_no = patch_line_no
        self.filename = filename
        self.line_no = line_no
        self.is_warning = is_warning

    def __repr__(self):
        ret = f"{repr(self.msg)}"
        ret += f":{repr(self.patch_line_no)}"
        ret += f":{repr(self.filename)}"
        ret += f":{repr(self.line_no)}"
        ret += f":{repr(self.is_warning)}"
        return ret

    def __eq__(self, other) -> bool:
        if isinstance(other, Output):
            return (
                other.msg,
                other.patch_line_no,
                other.file_diff.filename_b,
                other.line_no,
                isinstance(other, Warn),
            ) == (
                self.msg,
                self.patch_line_no,
                self.filename,
                self.line_no,
                self.is_warning,
            )
        raise ValueError(f"unexpected type {type(other)=}")


class PatchBuilder:
    """
    Helper class to generate a patch for testing
    """

    def __init__(self):
        self.files = {}

    def add_file(self, fromfile: str, tofile: str | None = None):
        """
        Pre-declare a file to be included in the patch
        """
        if tofile is None:
            tofile = fromfile
        self.files[fromfile] = (tofile, [])

    def add_change(self, file: str, change: list[(str, str)]):
        if file not in self.files:
            self.add_file(file)
        for before, after in change:
            self.files[file][1].append(
                (
                    before.splitlines(keepends=True),
                    after.splitlines(keepends=True),
                )
            )

    def __str__(self):
        ret = (
            "From 83eb8eddd14847d7b7555d8594b256be350910ec "
            "Mon Sep 17 00:00:00 2001\n"
            + """From: Developer <developer@example.com>
Date: Tue, 12 Aug 2025 14:08:44 +0300
Subject: [PATCH] Subject

Signed-off-by: Developer <developer@example.com>
---
"""
        )
        for f, v in self.files.items():
            fromname = f
            toname = v[0]
            ret += f"diff --git a/{fromname} b/{toname}\n"
            ret += "index b120a1f69e..6150a95f2e 100644\n"
            for a, b in v[1]:
                for l in difflib.unified_diff(
                    a, b, f"a/{fromname}", f"b/{toname}", lineterm="\n"
                ):
                    ret += l
        return ret


class TestCheckpatch(unittest.TestCase):
    configuration = patch.Configuration(signoff=True)

    def assertOutput(
        self, ptext: PatchBuilder, expected: list[ExpectedOutput]
    ):
        p = patch.Patch(self.configuration, str(ptext))
        with warnings.catch_warnings(record=True) as w:
            p.check()
            w = [w.message for w in w]
            self.assertEqual(len(w), len(expected))
            for w, e in zip(w, expected):
                self.assertEqual(w, e)

    def test_trailing_ws(self):
        ptext = PatchBuilder()
        ptext.add_change(
            "README.rst",
            [
                (
                    "For version history and release notes, please visit\n",
                    "For version history and release notes, please visit\x20\n",
                )
            ],
        )
        ptext.add_change(
            "README.rst",
            [
                (
                    (
                        "QEMU is capable of emulating a complete machine in"
                        " software without any\n"
                    ),
                    (
                        "QEMU is capable of emulating a complete machine in"
                        " software without any\r\n"
                    ),
                )
            ],
        )
        self.assertOutput(
            ptext,
            [
                ExpectedOutput(
                    msg="trailing whitespace",
                    patch_line_no=14,
                    filename="README.rst",
                    line_no=1,
                ),
                ExpectedOutput(
                    msg="DOS line endings",
                    patch_line_no=19,
                    filename="README.rst",
                    line_no=1,
                ),
            ],
        )

    def test_tabs(self):
        ptext = PatchBuilder()
        ptext.add_change(
            "README.rst",
            [
                (
                    "For version history and release notes, please visit\n",
                    "\tFor version history and release notes, please visit\n",
                )
            ],
        )
        self.assertOutput(
            ptext,
            [
                ExpectedOutput(
                    msg="code indent should never use tabs",
                    patch_line_no=14,
                    filename="README.rst",
                    line_no=1,
                ),
            ],
        )

    def test_column_limit(self):
        # non-source files are not checked for column limits
        ptext = PatchBuilder()
        ptext.add_change(
            "README.rst",
            [
                (
                    (
                        "QEMU is capable of emulating a complete machine in"
                        " software without any\n"
                    ),
                    (
                        "QEMU is capable of emulating a complete machine in"
                        " software without any need for hardware virtualization"
                        " support. By using dynamic translation,\n"
                    ),
                )
            ],
        )
        self.assertOutput(
            ptext,
            [],
        )
        # Line over 80 chars produces a warning
        ptext = PatchBuilder()
        ptext.add_change(
            "file.c",
            [
                (
                    "static int long_function_name(const char *arg)\n",
                    (
                        "static int long_function_name(const char *many, const"
                        " char *more, const char *args)\n"
                    ),
                )
            ],
        )
        self.assertOutput(
            ptext,
            [
                ExpectedOutput(
                    msg="line over 80 characters",
                    patch_line_no=14,
                    filename="file.c",
                    line_no=1,
                    is_warning=True,
                ),
            ],
        )
        # Line over 90 chars produces an error
        ptext = PatchBuilder()
        ptext.add_change(
            "file.c",
            [
                (
                    "static int long_function_name(const char *arg)\n",
                    (
                        "static int long_function_name(const char *many1, const"
                        " char *many2, const char *more, const char *args)\n"
                    ),
                )
            ],
        )
        self.assertOutput(
            ptext,
            [
                ExpectedOutput(
                    msg="line over 90 characters",
                    patch_line_no=14,
                    filename="file.c",
                    line_no=1,
                ),
            ],
        )

    def test_python(self):
        ptext = PatchBuilder()
        ptext.add_change(
            "script.py",
            [("", "#!/usr/bin/env python\n")],
        )
        self.assertOutput(
            ptext,
            [
                ExpectedOutput(
                    msg="please use python3 interpreter",
                    patch_line_no=13,
                    filename="script.py",
                    line_no=1,
                ),
            ],
        )
