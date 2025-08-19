# SPDX-License-Identifier: GPL-2.0-or-later

import re
import warnings

type FileDiffTy = "FileDiff"
type HunkTy = "Hunk"


class Output(UserWarning):
    """
    Base class for checkpatch output items (error or warn)
    """

    # FIXME: Receive patch, hunk in constructor to calculate line numbers right
    # away?
    def __init__(
        self,
        msg: str,
        /,
        *args,
        file_diff: FileDiffTy | None = None,
        match: str | re.Match | None = None,
        hunk: HunkTy | None = None,
        **kwargs,
    ):

        super().__init__(*args, **kwargs)
        self.msg = msg
        self.line_no = 0
        self.file_diff = file_diff
        self.match = match
        self.hunk = hunk
        if file_diff and hunk and match:
            if isinstance(match, str):
                hunk_offset = hunk.contents.find(match)
            elif isinstance(match, re.Match):
                hunk_offset = match.start()
            self.line_no = hunk.find_line(match)
            patch_offset = hunk.offset + hunk_offset
            self.patch_line_no = 1 + file_diff.patch.raw_string.count(
                "\n", 0, patch_offset
            )
        else:
            self.line_no = None
            self.patch_line_no = None
        warnings.warn(self)

    def __str__(self):
        # Needs a unique __str__ value otherwise warnings will be deduplicated
        ret = self.msg
        if self.patch_line_no:
            ret = f"{self.patch_line_no} {ret}"
        if self.file_diff:
            ret = f"{ret} {self.file_diff.filename_b}"
            if self.line_no:
                ret = f"{ret}:{self.line_no}"
        return ret

    def __repr__(self):
        ret = f"{repr(self.msg)}"
        ret += f":{repr(self.patch_line_no)}"
        ret += f":{repr(self.file_diff.filename_b)}"
        ret += f":{repr(self.line_no)}"
        ret += f":{repr(isinstance(self, Warn))}"
        return ret


class Error(Output):
    """
    A checkpatch error
    """


class Warn(Output):
    """
    A checkpatch warning
    """

    def into_error(self) -> Error:
        """
        Convert warning into error
        """
        return Error(
            self.msg,
            file_diff=self.file_diff,
            hunk=self.hunk,
            match=self.match,
        )
