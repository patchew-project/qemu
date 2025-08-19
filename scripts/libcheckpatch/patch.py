# SPDX-License-Identifier: GPL-2.0-or-later

from collections.abc import Callable
from email import message_from_string
from functools import cached_property
import enum
import itertools
import pathlib
import re

from . import checks
from .output import Output, Error

type PatchTy = "Patch"
type FileDiffTy = "FileDiff"


class Configuration:
    def __init__(
        self, /, signoff: bool = True, root: pathlib.Path | None = None
    ):
        self.signoff = signoff
        self.root = root


class Hunk:
    """
    A single diff hunk
    """

    def __init__(self, offset: int, line_no: int, contents: str):
        self.offset = offset
        self.line_no = line_no
        self.contents = contents

    def __repr__(self):
        return self.contents

    def find_match(
        self,
        regex: str,
        file_diff: FileDiffTy,
        category: type[Output],
        cb: Callable[[re.Match], None],
    ) -> bool:
        assert regex.startswith(r"^\+")
        match = re.search(regex, self.contents, re.MULTILINE)
        if match:
            category(
                cb(match),
                file_diff=file_diff,
                match=match,
                hunk=self,
            )
        return match is not None

    def find_matches(
        self,
        regex: str,
        file_diff: FileDiffTy,
        category: type[Output],
        cb: Callable[[re.Match], None],
    ):
        assert regex.startswith(r"^\+")
        for match in re.finditer(regex, self.contents, re.MULTILINE):
            category(
                cb(match),
                file_diff=file_diff,
                match=match,
                hunk=self,
            )

    def find_line(self, match: str | re.Match) -> int:
        # skip control line ("@@")
        control_line, contents = self.contents.split("\n", 1)
        if isinstance(match, str):
            offset = contents.find(match)
        elif isinstance(match, re.Match):
            offset = match.start() - len(control_line) - 1
        line_no = self.line_no + len(
            [
                l
                for l in contents[:offset].splitlines()
                if not l.startswith("-")
            ]
        )

        return line_no


class FileAction(enum.Enum):
    MODIFIED = 1
    NEW = enum.auto()
    DELETED = enum.auto()
    RENAMED = enum.auto()


class FileDiff:
    """
    Representation of a batch of diff hunks for a single file in a patch/diff
    """

    def __init__(
        self,
        patch_offset: int,
        patch: PatchTy,
        filename_a: str,
        filename_b: str,
        hunks: list[Hunk],
        mode: int | None = None,
        action: FileAction = FileAction.MODIFIED,
    ):
        self.patch_offset = patch_offset
        self.patch = patch
        self.filename_a = filename_a
        self.filename_b = filename_b
        self.hunks = hunks
        self.mode = mode
        self.action = action

    def find_match(
        self,
        regex: str,
        category: type[Output],
        cb: Callable[[re.Match], None],
    ) -> bool:
        for h in self.hunks:
            if h.find_match(regex, self, category, cb):
                return True
        return False

    def find_matches(
        self,
        regex: str,
        category: type[Output],
        cb: Callable[[re.Match], None],
    ):
        for h in self.hunks:
            h.find_matches(regex, self, category, cb)

    def __repr__(self):
        return f"{self.filename_a} {len(self.hunks)} hunks"

    @cached_property
    def format(self) -> checks.FileFormat:
        """
        Returns the detected file format for this file diff
        """

        # Hack(?): discover all subclasses of FileFormat by calling the
        # __subclasses__ method. Classes that might have not been
        # imported/parsed will not appear, but we assume that this code is
        # called after everything has been loaded.
        for subclass in checks.FileFormat.__subclasses__():
            if subclass.is_of(self.filename_b):
                return subclass()
        return checks.FileFormat()


class Patch:
    """
    Representation of a patch/diff
    """

    def __init__(
        self,
        configuration: Configuration,
        raw_string: str,
        filename: str | None = None,
        sha: str | None = None,
    ):
        """Attempt to parse `raw_string` as a patch"""

        self.raw_string = raw_string
        self.configuration = configuration
        self.filename = filename
        self.sha = sha
        self.msg = None
        self.description = None
        self.body = None
        self.file_diffs = []
        self.parse_exception = None

        self.msg = message_from_string(raw_string)
        if self.msg.is_multipart():
            self.parse_exception = ValueError("multipart")
            return
        try:
            split = self.msg.get_payload().split("\n---\n", maxsplit=1)
            if len(split) == 2:
                self.description, self.body = split
            else:
                self.description = split[0]
                self.body = ""
        except ValueError as exc:
            self.parse_exception = ValueError(
                "Does not appear to be a unified-diff format patch"
            ).with_traceback(exc.__traceback__)
            return
        if not self.filename and self.sha:
            try:
                subject = self.msg.get_all("Subject")[0]
                self.filename = f"Commit {self.sha} ({subject})"
            except TypeError:
                self.filename = f"Commit {self.sha}"

        body_offset = raw_string.find(self.body)
        files = []
        prev = None
        for match in re.finditer(r"^diff --git ", self.body, re.MULTILINE):
            if prev is not None:
                files.append(
                    (body_offset + prev, self.body[prev : match.start()])
                )
            prev = match.start()

        if prev is not None:
            files.append((body_offset + prev, self.body[prev:]))

        self.file_diffs = []
        for offset, f in files:
            matches = re.search(
                r"^diff --git a\/(?P<filename_a>[^ ]+) b\/(?P<filename_b>[^"
                r" ]+)$",
                f,
                re.MULTILINE,
            )
            if not matches:
                self.parse_exception = ValueError(
                    "Does not appear to be a unified-diff format patch"
                )
                return
            filename_a = matches.groups("filename_a")[0]
            filename_b = matches.groups("filename_b")[0]
            hunks: list[Hunk] = []
            prev = None
            for match in re.finditer(
                r"^@@ [-]\d+(?:,\d+)? [+](?P<line_no>\d+)(?:,\d+)? @@",
                f,
                re.MULTILINE,
            ):
                if prev is not None:
                    hunks.append(
                        Hunk(
                            offset + prev[0],
                            prev[1],
                            f[prev[0] : match.start()],
                        )
                    )
                prev = (match.start(), int(match.group("line_no")))

            if prev is not None:
                hunks.append(Hunk(offset + prev[0], prev[1], f[prev[0] :]))

            action = None
            matches = re.search(
                r"^new (?:file )?mode\s+([0-7]+)$",
                f[: hunks[0].offset],
                re.MULTILINE,
            )
            if matches:
                mode = int(matches.group(1), 8)
                action = FileAction.NEW
            else:
                mode = None
            self.file_diffs.append(
                FileDiff(
                    offset,
                    self,
                    filename_a,
                    filename_b,
                    hunks,
                    mode=mode,
                    action=action,
                )
            )

    def check_author_address(self):
        """Check for invalid author address"""
        if self.parse_exception:
            return
        regex = r".*? via .*?<qemu-\w+@nongnu\.org>"

        authors = itertools.chain(
            self.msg.get_all("From") or [], self.msg.get_all("Author") or []
        )
        for val in authors:
            if re.search(regex, val):
                Error(
                    "Author email address is mangled by the mailing list",
                )

    def check_signoff(self):
        """Check patch for valid signoff (DCO)"""
        if self.parse_exception:
            return
        for match in re.finditer(
            r"^\s*signed-off-by",
            self.description,
            re.MULTILINE | re.IGNORECASE,
        ):
            match_start = self.description[match.start() :]
            if not re.search(
                r"^\s*Signed-off-by:.*$", match_start, re.MULTILINE
            ):
                Error(
                    'The correct form is "Signed-off-by" found'
                    f" {match_start=}",
                )
            if re.search(r"\s*signed-off-by:\S", match_start):
                Error(
                    "Space required after Signed-off-by:",
                )
            break
        else:
            Error(
                "Missing Signed-off-by: line(s)",
            )

    def check(self):
        """Check patch and all files in patch according to their file format"""
        if self.parse_exception:
            Error(str(self.parse_exception))
            return
        self.check_author_address()
        if self.configuration.signoff:
            self.check_signoff()
        for f in self.file_diffs:
            for v in f.format.checks.values():
                v(f)
