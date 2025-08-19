# SPDX-License-Identifier: GPL-2.0-or-later

from collections.abc import Callable
import re

from . import patch
from .output import Error, Warn

type FileDiffTy = "FileDiff"
type Check = Callable[
    [
        FileDiffTy,
    ],
    None,
]


class FileFormat:
    """
    Base class for a file format and appropriate checks

    All @staticmethods that start with `check_` are collected as tests
    applicable for this format.

    If a file format is not detectable by filename suffix, its class should
    override the `is_of` classmethod.
    """

    suffixes: list[str]
    checks: dict[str, Check]
    is_source_file: bool = False
    is_executable_source_file: bool = False

    def __new__(cls):
        checks = {}
        suffixes = []
        for c in set([FileFormat, cls]):
            for k, v in c.__dict__.items():
                if isinstance(v, staticmethod) and k.startswith("check_"):
                    checks[k] = v
                elif k == "suffixes":
                    suffixes += v
        val = super().__new__(cls)
        val.checks = checks
        val.suffixes = suffixes
        return val

    @classmethod
    def is_of(cls, path: str) -> bool:
        """
        Returns `True` if path suffix matches this format
        """
        for suf in cls.suffixes:
            if path.endswith(f".{suf}"):
                return True
        return False

    @staticmethod
    def check_trailing_whitespace(file_diff: FileDiffTy):
        """
        Checks newly added lines for trailing whitespace
        """
        # ignore files that are being periodically imported from Linux
        if file_diff.filename_b.startswith(
            "linux-headers"
        ) or file_diff.filename_b.startswith("include/standard-headers"):
            return

        if re.search(
            r"^docs\/.+\.(?:(?:txt)|(?:md)|(?:rst))", file_diff.filename_a
        ):
            # TODO
            # "code blocks in documentation should have empty lines with
            # exactly 4 columns of whitespace
            pass

        for hunk in file_diff.hunks:
            hunk.find_matches(
                r"^\+.*\015", file_diff, Error, lambda _: "DOS line endings"
            )
            hunk.find_matches(
                r"^\+.*\S+[ ]+$",
                file_diff,
                Error,
                lambda _: "trailing whitespace",
            )

    @staticmethod
    def check_column_limit(file_diff: FileDiffTy):
        """
        Checks column widths
        """
        if not file_diff.format.is_source_file:
            return
        # FIXME: exempt URLs
        for hunk in file_diff.hunks:
            for line in hunk.contents.splitlines():
                if not line.startswith("+"):
                    continue
                if len(line) > 91:
                    Error(
                        "line over 90 characters",
                        file_diff=file_diff,
                        match=line,
                        hunk=hunk,
                    )
                elif len(line) > 81:
                    Warn(
                        "line over 80 characters",
                        file_diff=file_diff,
                        match=line,
                        hunk=hunk,
                    )

    @staticmethod
    def check_eof_newline(file_diff: FileDiffTy):
        """
        Require newline at end of file
        """
        # TODO: adding a line without newline at end of file

    @staticmethod
    def check_tabs(file_diff: FileDiffTy):
        """
        Reject indentation with tab character
        """
        # tabs are only allowed in assembly source code, and in
        # some scripts we imported from other projects.
        if isinstance(
            file_diff.format, (AssemblyFileFormat | PerlFileFormat)
        ) or file_diff.filename_b.startswith("target/hexagon/imported"):
            return

        file_diff.find_matches(
            r"^\+.*\t",
            Error,
            lambda _: "code indent should never use tabs",
        )

    @staticmethod
    def check_spdx_header(file_diff: FileDiffTy):
        """
        Check SPDX-License-Identifier exists and references a permitted license
        """
        # TODO: Check for spdx header

        # Imported Linux headers probably have SPDX tags, but if they
        # don't we're not requiring contributors to fix this, as these
        # files are not expected to be modified locally in QEMU.
        # Also don't accidentally detect own checking code.
        if file_diff.filename_b.startswith(
            "include/standard-headers"
        ) or file_diff.filename_b.startswith("linux-headers"):
            return

    @staticmethod
    def check_license_boilerplates(file_diff: FileDiffTy):
        """
        Checks for new files with license boilerplate
        """

        if (
            not file_diff.action is patch.FileAction.NEW
            or file_diff.filename_b.startswith("scripts/libcheckpatch")
        ):
            return

        boilerplate_re = r"^\+.*" + "|".join(
            [
                "licensed under the terms of the GNU GPL",
                "under the terms of the GNU General Public License",
                "under the terms of the GNU Lesser General Public",
                "Permission is hereby granted, free of charge",
                "GNU GPL, version 2 or later",
                "See the COPYING file",
            ]
        )
        # FIXME: shows only first match for compatibility with checkpatch.pl
        file_diff.find_match(
            boilerplate_re,
            Error,
            lambda _: (
                f"New file '{file_diff.filename_b}' must "
                "not have license boilerplate header text, only "
                "the SPDX-License-Identifier, unless this file was "
                "copied from existing code already having such text."
            ),
        )

    @staticmethod
    def check_qemu(file_diff: FileDiffTy):
        """
        QEMU specific tests
        """
        # FIXME: check only C files for compatibility with checkpatch.pl
        if not isinstance(file_diff.format, CFileFormat):
            return
        file_diff.find_matches(
            r"^\+.*\b(?:Qemu|QEmu)\b",
            Error,
            lambda _: "use QEMU instead of Qemu or QEmu",
        )

    @staticmethod
    def check_file_permissions(fd: FileDiffTy):
        """
        Check for incorrect file permissions
        """
        if (
            fd.format.is_source_file
            and not fd.format.is_executable_source_file
            and fd.mode
            and fd.mode & 0o0111 > 0
        ):
            Error("do not set execute permissions for source files")

    @staticmethod
    def check_maintainers(file_diff: FileDiffTy):
        """
        Checks if MAINTAINERS must be updated when adding, moving or deleting
        files
        """

        if file_diff.action is patch.FileAction.MODIFIED:
            return

        # TODO: WARN("added, moved or deleted file(s):"


class PythonFileFormat(FileFormat):
    """
    Python file format
    """

    is_source_file = True
    is_executable_source_file = True
    suffixes = ["py"]

    @staticmethod
    def check_python_interp(file_diff: FileDiffTy):
        """
        Only allow Python 3 interpreter
        """
        interp_re = r"^\+#![ ]*[/]usr[/]bin[/](?:env )?python\n"
        for h in file_diff.hunks:
            if h.line_no == 1 and re.search(
                interp_re, h.contents.partition("\n")[2]
            ):
                h.find_match(
                    interp_re,
                    file_diff,
                    Error,
                    lambda _: "please use python3 interpreter",
                )


class AssemblyFileFormat(FileFormat):
    """
    Assembly file format
    """

    is_source_file = True
    suffixes = ["s", "S"]


class PerlFileFormat(FileFormat):
    """
    Perl file format
    """

    is_source_file = True
    is_executable_source_file = True
    suffixes = ["pl"]


class MesonFileFormat(FileFormat):
    """
    Meson build file format
    """

    is_source_file = False
    suffixes = ["build"]


class ShellFileFormat(FileFormat):
    """
    Shell script file format
    """

    is_source_file = True
    is_executable_source_file = True
    suffixes = ["sh"]


class TraceEventFileFormat(FileFormat):
    """
    trace-events file format
    """

    suffixes = []

    @classmethod
    def is_of(cls, path: str) -> bool:
        return path.endswith("trace-events")

    @staticmethod
    def check_hex_specifier(file_diff: FileDiffTy):
        """
        Reject %# format specifier
        """
        # TODO: Don't use '#' flag of printf format ('%#') in trace-events, use
        # '0x' prefix instead

    @staticmethod
    def check_hex_prefix(file_diff: FileDiffTy):
        """
        Require 0x prefix for hex numbers
        """
        # TODO: Hex numbers must be prefixed with '0x'


class CFileFormat(FileFormat):
    """
    C file format
    """

    is_source_file = True
    suffixes = ["c", "h", "c.inc"]

    @staticmethod
    def check_non_portable_libc_calls(file_diff: FileDiffTy):
        """
        Check for non-portable libc calls that have portable alternatives in
        QEMU
        """
        replacements = {
            r"\bffs\(": "ctz32",
            r"\bffsl\(": "ctz32() or ctz64",
            r"\bffsll\(": "ctz64",
            r"\bbzero\(": "memset",
            r"\bsysconf\(_SC_PAGESIZE\)": "qemu_real_host_page_size",
            r"\b(?:g_)?assert\(0\)": "g_assert_not_reached",
            r"\b(:?g_)?assert\(false\)": "g_assert_not_reached",
            r"\bstrerrorname_np\(": "strerror",
        }
        non_exit_glib_asserts_re = r"^\+.*" + (
            r"g_assert_cmpstr"
            r"|g_assert_cmpint|g_assert_cmpuint"
            r"|g_assert_cmphex|g_assert_cmpfloat"
            r"|g_assert_true|g_assert_false|g_assert_nonnull"
            r"|g_assert_null|g_assert_no_error|g_assert_error"
            r"|g_test_assert_expected_messages|g_test_trap_assert_passed"
            r"|g_test_trap_assert_stdout|g_test_trap_assert_stdout_unmatched"
            r"|g_test_trap_assert_stderr|g_test_trap_assert_stderr_unmatched"
        )

        for hunk in file_diff.hunks:
            for r, w in replacements.items():
                hunk.find_matches(
                    r"^\+.*" + r,
                    file_diff,
                    Error,
                    lambda match: f"use {w}() instead of {match.group()}",
                )
            hunk.find_matches(
                non_exit_glib_asserts_re,
                file_diff,
                Error,
                lambda m: (
                    "Use g_assert or g_assert_not_reached instead of"
                    f" {m.group()}"
                ),
            )

    @staticmethod
    def check_qemu_error_functions(_: FileDiffTy):
        """
        QEMU error function tests
        """
        # TODO: Find newlines in error messages
        error_funcs_re = (
            r"error_setg|"
            r"error_setg_errno|"
            r"error_setg_win32|"
            r"error_setg_file_open|"
            r"error_set|"
            r"error_prepend|"
            r"warn_reportf_err|"
            r"error_reportf_err|"
            r"error_vreport|"
            r"warn_vreport|"
            r"info_vreport|"
            r"error_report|"
            r"warn_report|"
            r"info_report|"
            r"g_test_message"
        )

    @staticmethod
    def check_ops_structs_are_const(file_diff: FileDiffTy):
        """check for various ops structs, ensure they are const."""
        # TODO

    @staticmethod
    def check_comments(file_diff: FileDiffTy):
        for hunk in file_diff.hunks:
            hunk.find_matches(
                r"^\+.*?[/][/](?! SPDX-License-Identifier:)",
                file_diff,
                Error,
                lambda _: "do not use C99 // comments",
            )

        for hunk in file_diff.hunks:
            hunk.find_matches(
                r"^\+\s*[/]\s*[*][ \t]*\S+",
                file_diff,
                Warn,
                lambda _: "Block comments use a leading /* on a separate line",
            )
            # TODO: WARN("Block comments use * on subsequent lines
            # FIXME: Check comment context for trailing */
            hunk.find_matches(
                r"^\+\s*[*][ \t]*\S+\s*[*][/]$",
                file_diff,
                Warn,
                lambda _: (
                    "Block comments use a trailing */ on a separate line"
                ),
            )
        # TODO: WARN("Block comments should align the * on each line

    # unimplemented:

    # TODO: switch and case should be at the same indent
    # TODO: that open brace { should be on the previous line
    # TODO: trailing semicolon indicates no statements, indent implies
    # otherwise
    # TODO: suspicious ; after while (0)
    # TODO: superfluous trailing semicolon
    # TODO: suspect code indent for conditional statements ($indent, $sindent)
    # TODO: \"(foo$from)\" should be \"(foo$to)\"
    # TODO: \"foo${from}bar\" should be \"foo${to}bar\"
    # TODO: open brace '{' following function declarations go on the next line
    # TODO: missing space after $1 definition
    # TODO: check for malformed paths in #include statements
    # TODO: check for global initialisers.
    # TODO: check for static initialisers.
    # TODO: * goes on variable not on type
    # TODO: function brace can't be on same line, except for #defines of do
    # while, or if closed on same line
    # TODO: open braces for enum, union and struct go on the same line.
    # TODO: missing space after union, struct or enum definition
    # TODO: check for spacing round square brackets; allowed:
    #  1. with a type on the left -- int [] a;
    #  2. at the beginning of a line for slice initialisers -- [0...10] = 5,
    #  3. inside a curly brace -- = { [0...10] = 5 }
    #  4. after a comma -- [1] = 5, [2] = 6
    #  5. in a macro definition -- #define abc(x) [x] = y
    # TODO: check for spaces between functions and their parentheses.
    # TODO: Check operator spacing.
    # TODO: need space before brace following if, while, etc
    # TODO: closing brace should have a space following it when it has anything
    # on the line
    # TODO: check spacing on square brackets
    # TODO: check spacing on parentheses
    # TODO: Return is not a function.
    # TODO: Return of what appears to be an errno should normally be -'ve
    # TODO: Need a space before open parenthesis after if, while etc
    # TODO: Check for illegal assignment in if conditional -- and check for
    # trailing statements after the conditional.
    # TODO: Check for bitwise tests written as boolean
    # TODO: if and else should not have general statements after it
    # TODO: if should not continue a brace
    # case and default should not have general statements after them
    # TODO: no spaces allowed after \ in define
    # TODO: multi-statement macros should be enclosed in a do while loop, grab
    # the first statement and ensure its the whole macro if its not enclosed
    # in a known good container
    # TODO: check for missing bracing around if etc
    # TODO: no volatiles please
    # TODO: warn about #if 0
    # TODO: check for needless g_free() checks
    # TODO: warn about spacing in #ifdefs
    # TODO: check for memory barriers without a comment.
    # TODO: check of hardware specific defines
    # we have e.g. CONFIG_LINUX and CONFIG_WIN32 for common cases
    # where they might be necessary.
    # TODO: Check that the storage class is at the beginning of a declaration
    # TODO: check the location of the inline attribute, that it is between
    # storage class and type.
    # TODO: check for sizeof(&)
    # TODO: check for new externs in .c files.
    # TODO: check for pointless casting of g_malloc return
    @staticmethod
    def check_misc_recommends(file_diff: FileDiffTy):
        # check for gcc specific __FUNCTION__
        file_diff.find_matches(
            r"^\+.*__FUNCTION__",
            Error,
            lambda _: (
                "__func__ should be used instead of gcc specific __FUNCTION__"
            ),
        )

        # recommend g_path_get_* over g_strdup(basename/dirname(...))
        file_diff.find_matches(
            r"^\+.*\bg_strdup\s*\(\s*(basename|dirname)\s*\(",
            Warn,
            lambda m: (
                "consider using g_path_get_{m.group(1)}() in preference to"
                " g_strdup({m.group(1)}())"
            ),
        )
        # enforce g_memdup2() over g_memdup()
        file_diff.find_matches(
            r"^\+.*\bg_memdup\s*\(",
            Error,
            lambda _: "use g_memdup2() instead of unsafe g_memdup()",
        )
        # TODO: recommend qemu_strto* over strto* for numeric conversions
        # TODO: recommend sigaction over signal for portability, when
        # establishing a handler
        # TODO: recommend qemu_bh_new_guarded instead of qemu_bh_new
        # TODO: recommend aio_bh_new_guarded instead of aio_bh_new
        # check for module_init(), use category-specific init macros
        # explicitly please
        file_diff.find_matches(
            r"^\+.*\bmodule_init\(",
            Error,
            lambda _: (
                "please use block_init(), type_init() etc. instead of"
                " module_init()"
            ),
        )
