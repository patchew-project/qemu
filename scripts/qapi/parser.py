# -*- coding: utf-8 -*-
#
# QAPI schema parser
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2019 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#  Marc-André Lureau <marcandre.lureau@redhat.com>
#  Kevin Wolf <kwolf@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from collections import OrderedDict
import os
from typing import (
    Dict,
    List,
    Optional,
    Set,
    Union,
)

from .common import must_match
from .error import QAPISemError, QAPISourceError
from .qapidoc import QAPIDoc
from .source import QAPISourceInfo


# Return value alias for get_expr().
_ExprValue = Union[List[object], Dict[str, object], str, bool]


class QAPIParseError(QAPISourceError):
    """Error class for all QAPI schema parsing errors."""
    def __init__(self, parser: 'QAPISchemaParser', msg: str):
        col = 1
        for ch in parser.src[parser.line_pos:parser.pos]:
            if ch == '\t':
                col = (col + 7) % 8 + 1
            else:
                col += 1
        super().__init__(parser.info, msg, col)


class QAPISchemaParser:
    """
    Performs syntactic parsing of a QAPI schema source file.

    Parses a JSON-esque schema file, See qapi-code-gen.txt section
    "Schema Syntax" for more information. Grammatical validation
    is handled later by `expr.check_exprs()`.

    :param fname: Source filename.
    :param previously_included:
        The absolute pathnames of previously included source files,
        if being invoked from another parser.
    :param incl_info:
       `QAPISourceInfo` belonging to the parent module.
       ``None`` implies this is the root module.

    :ivar exprs: Resulting parsed expressions.
    :ivar docs: Resulting parsed documentation blocks.

    :raise OSError: For problems opening the root schema document.
    :raise QAPIError: For syntactic or semantic parsing errors.
    """
    def __init__(self,
                 fname: str,
                 previously_included: Optional[Set[str]] = None,
                 incl_info: Optional[QAPISourceInfo] = None):
        self._fname = fname
        self._included = previously_included or set()
        self._included.add(os.path.abspath(self._fname))
        self.src = ''

        # Lexer state (see `accept` for details):
        self.info = QAPISourceInfo(self._fname, incl_info)
        self.tok: Union[None, str] = None
        self.pos = 0
        self.cursor = 0
        self.val: Optional[Union[bool, str]] = None
        self.line_pos = 0

        # Parser output:
        self.exprs: List[Dict[str, object]] = []
        self.docs: List[QAPIDoc] = []

        # Showtime!
        self._parse()

    def _parse(self) -> None:
        """
        Parse the QAPI schema document.

        :return: None. Results are stored in ``.exprs`` and ``.docs``.
        """
        cur_doc = None

        # May raise OSError; allow the caller to handle it.
        with open(self._fname, 'r', encoding='utf-8') as fp:
            self.src = fp.read()
        if self.src == '' or self.src[-1] != '\n':
            self.src += '\n'

        # Prime the lexer:
        self.accept()

        # Parse until done:
        while self.tok is not None:
            info = self.info
            if self.tok == '#':
                self.reject_expr_doc(cur_doc)
                for cur_doc in self.get_doc(info):
                    self.docs.append(cur_doc)
                continue

            expr = self.get_expr()
            if not isinstance(expr, dict):
                raise QAPISemError(
                    info, "top-level expression must be an object")

            if 'include' in expr:
                self.reject_expr_doc(cur_doc)
                if len(expr) != 1:
                    raise QAPISemError(info, "invalid 'include' directive")
                include = expr['include']
                if not isinstance(include, str):
                    raise QAPISemError(info,
                                       "value of 'include' must be a string")
                incl_fname = os.path.join(os.path.dirname(self._fname),
                                          include)
                self.exprs.append({'expr': {'include': incl_fname},
                                   'info': info})
                exprs_include = self._include(include, info, incl_fname,
                                              self._included)
                if exprs_include:
                    self.exprs.extend(exprs_include.exprs)
                    self.docs.extend(exprs_include.docs)
            elif "pragma" in expr:
                self.reject_expr_doc(cur_doc)
                if len(expr) != 1:
                    raise QAPISemError(info, "invalid 'pragma' directive")
                pragma = expr['pragma']
                if not isinstance(pragma, dict):
                    raise QAPISemError(
                        info, "value of 'pragma' must be an object")
                for name, value in pragma.items():
                    self._pragma(name, value, info)
            else:
                expr_elem = {'expr': expr,
                             'info': info}
                if cur_doc:
                    if not cur_doc.symbol:
                        raise QAPISemError(
                            cur_doc.info, "definition documentation required")
                    expr_elem['doc'] = cur_doc
                self.exprs.append(expr_elem)
            cur_doc = None
        self.reject_expr_doc(cur_doc)

    @staticmethod
    def reject_expr_doc(doc: Optional['QAPIDoc']) -> None:
        if doc and doc.symbol:
            raise QAPISemError(
                doc.info,
                "documentation for '%s' is not followed by the definition"
                % doc.symbol)

    @staticmethod
    def _include(include: str,
                 info: QAPISourceInfo,
                 incl_fname: str,
                 previously_included: Set[str]
                 ) -> Optional['QAPISchemaParser']:
        incl_abs_fname = os.path.abspath(incl_fname)
        # catch inclusion cycle
        inf: Optional[QAPISourceInfo] = info
        while inf:
            if incl_abs_fname == os.path.abspath(inf.fname):
                raise QAPISemError(info, "inclusion loop for %s" % include)
            inf = inf.parent

        # skip multiple include of the same file
        if incl_abs_fname in previously_included:
            return None

        try:
            return QAPISchemaParser(incl_fname, previously_included, info)
        except OSError as err:
            raise QAPISemError(
                info,
                f"can't read include file '{incl_fname}': {err.strerror}"
            ) from err

    @staticmethod
    def _pragma(name: str, value: object, info: QAPISourceInfo) -> None:

        def check_list_str(name: str, value: object) -> List[str]:
            if (not isinstance(value, list) or
                    any(not isinstance(elt, str) for elt in value)):
                raise QAPISemError(
                    info,
                    "pragma %s must be a list of strings" % name)
            return value

        pragma = info.pragma

        if name == 'doc-required':
            if not isinstance(value, bool):
                raise QAPISemError(info,
                                   "pragma 'doc-required' must be boolean")
            pragma.doc_required = value
        elif name == 'command-name-exceptions':
            pragma.command_name_exceptions = check_list_str(name, value)
        elif name == 'command-returns-exceptions':
            pragma.command_returns_exceptions = check_list_str(name, value)
        elif name == 'member-name-exceptions':
            pragma.member_name_exceptions = check_list_str(name, value)
        else:
            raise QAPISemError(info, "unknown pragma '%s'" % name)

    def accept(self, skip_comment: bool = True) -> None:
        """Read and store the next token.

        :param skip_comment:
            When false, return COMMENT tokens ("#").
            This is used when reading documentation blocks.

        :return:
            None. Several instance attributes are updated instead:

            - ``.tok`` represents the token type. See below for values.
            - ``.info`` describes the token's source location.
            - ``.val`` is the token's value, if any. See below.
            - ``.pos`` is the buffer index of the first character of
              the token.

        * Single-character tokens:

            These are "{", "}", ":", ",", "[", and "]". ``.tok`` holds
            the single character and ``.val`` is None.

        * Multi-character tokens:

          * COMMENT:

            This token is not normally returned by the lexer, but it can
            be when ``skip_comment`` is False. ``.tok`` is "#", and
            ``.val`` is a string including all chars until end-of-line,
            including the "#" itself.

          * STRING:

            ``.tok`` is "'", the single quote. ``.val`` contains the
            string, excluding the surrounding quotes.

          * TRUE and FALSE:

            ``.tok`` is either "t" or "f", ``.val`` will be the
            corresponding bool value.

          * EOF:

            ``.tok`` and ``.val`` will both be None at EOF.
        """
        while True:
            self.tok = self.src[self.cursor]
            self.pos = self.cursor
            self.cursor += 1
            self.val = None

            if self.tok == '#':
                if self.src[self.cursor] == '#':
                    # Start of doc comment
                    skip_comment = False
                self.cursor = self.src.find('\n', self.cursor)
                if not skip_comment:
                    self.val = self.src[self.pos:self.cursor]
                    return
            elif self.tok in '{}:,[]':
                return
            elif self.tok == "'":
                # Note: we accept only printable ASCII
                string = ''
                esc = False
                while True:
                    ch = self.src[self.cursor]
                    self.cursor += 1
                    if ch == '\n':
                        raise QAPIParseError(self, "missing terminating \"'\"")
                    if esc:
                        # Note: we recognize only \\ because we have
                        # no use for funny characters in strings
                        if ch != '\\':
                            raise QAPIParseError(self,
                                                 "unknown escape \\%s" % ch)
                        esc = False
                    elif ch == '\\':
                        esc = True
                        continue
                    elif ch == "'":
                        self.val = string
                        return
                    if ord(ch) < 32 or ord(ch) >= 127:
                        raise QAPIParseError(
                            self, "funny character in string")
                    string += ch
            elif self.src.startswith('true', self.pos):
                self.val = True
                self.cursor += 3
                return
            elif self.src.startswith('false', self.pos):
                self.val = False
                self.cursor += 4
                return
            elif self.tok == '\n':
                if self.cursor == len(self.src):
                    self.tok = None
                    return
                self.info = self.info.next_line()
                self.line_pos = self.cursor
            elif not self.tok.isspace():
                # Show up to next structural, whitespace or quote
                # character
                match = must_match('[^[\\]{}:,\\s\'"]+',
                                   self.src[self.cursor-1:])
                raise QAPIParseError(self, "stray '%s'" % match.group(0))

    def get_members(self) -> Dict[str, object]:
        expr: Dict[str, object] = OrderedDict()
        if self.tok == '}':
            self.accept()
            return expr
        if self.tok != "'":
            raise QAPIParseError(self, "expected string or '}'")
        while True:
            key = self.val
            assert isinstance(key, str)  # Guaranteed by tok == "'"

            self.accept()
            if self.tok != ':':
                raise QAPIParseError(self, "expected ':'")
            self.accept()
            if key in expr:
                raise QAPIParseError(self, "duplicate key '%s'" % key)
            expr[key] = self.get_expr()
            if self.tok == '}':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPIParseError(self, "expected ',' or '}'")
            self.accept()
            if self.tok != "'":
                raise QAPIParseError(self, "expected string")

    def get_values(self) -> List[object]:
        expr: List[object] = []
        if self.tok == ']':
            self.accept()
            return expr
        if self.tok not in tuple("{['tf"):
            raise QAPIParseError(
                self, "expected '{', '[', ']', string, or boolean")
        while True:
            expr.append(self.get_expr())
            if self.tok == ']':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPIParseError(self, "expected ',' or ']'")
            self.accept()

    def get_expr(self) -> _ExprValue:
        expr: _ExprValue
        if self.tok == '{':
            self.accept()
            expr = self.get_members()
        elif self.tok == '[':
            self.accept()
            expr = self.get_values()
        elif self.tok in tuple("'tf"):
            assert isinstance(self.val, (str, bool))
            expr = self.val
            self.accept()
        else:
            raise QAPIParseError(
                self, "expected '{', '[', string, or boolean")
        return expr

    def get_doc(self, info: QAPISourceInfo) -> List['QAPIDoc']:
        if self.val != '##':
            raise QAPIParseError(
                self, "junk after '##' at start of documentation comment")

        docs = []
        cur_doc = QAPIDoc(self, info)
        self.accept(False)
        while self.tok == '#':
            assert isinstance(self.val, str)  # comment token MUST have str val
            if self.val.startswith('##'):
                # End of doc comment
                if self.val != '##':
                    raise QAPIParseError(
                        self,
                        "junk after '##' at end of documentation comment")
                cur_doc.end_comment()
                docs.append(cur_doc)
                self.accept()
                return docs
            if self.val.startswith('# ='):
                if cur_doc.symbol:
                    raise QAPIParseError(
                        self,
                        "unexpected '=' markup in definition documentation")
                if cur_doc.body.text:
                    cur_doc.end_comment()
                    docs.append(cur_doc)
                    cur_doc = QAPIDoc(self, info)
            cur_doc.append(self.val)
            self.accept(False)

        raise QAPIParseError(self, "documentation comment must end with '##'")
