# ---------------------------------------------------------------------------- #

from ctypes import CFUNCTYPE, c_int, py_object
from dataclasses import dataclass
from enum import Enum
import inspect
import os
import os.path
from pathlib import Path
from importlib import import_module
import textwrap
from typing import (
    Any,
    Callable,
    Dict,
    Iterable,
    List,
    Optional,
    Sequence,
    Union,
)

from clang.cindex import (  # type: ignore
    Cursor,
    CursorKind,
    SourceLocation,
    SourceRange,
    TokenGroup,
    TranslationUnit,
    TypeKind,
    conf,
)

# ---------------------------------------------------------------------------- #
# Monkeypatch clang.cindex

Cursor.__hash__ = lambda self: self.hash  # so `Cursor`s can be dict keys

# ---------------------------------------------------------------------------- #
# Traversal


class VisitorResult(Enum):

    BREAK = 0
    """Terminates the cursor traversal."""

    CONTINUE = 1
    """Continues the cursor traversal with the next sibling of the cursor just
    visited, without visiting its children."""

    RECURSE = 2
    """Recursively traverse the children of this cursor."""


def visit(root: Cursor, visitor: Callable[[Cursor], VisitorResult]) -> bool:
    """
    A simple wrapper around `clang_visitChildren()`.

    The `visitor` callback is called for each visited node, with that node as
    its argument. `root` is NOT visited.

    Unlike a standard `Cursor`, the callback argument will have a `parent` field
    that points to its parent in the AST. The `parent` will also have its own
    `parent` field, and so on, unless it is `root`, in which case its `parent`
    field is `None`. We add this because libclang's `lexical_parent` field is
    almost always `None` for some reason.

    Returns `false` if the visitation was aborted by the callback returning
    `VisitorResult.BREAK`. Returns `true` otherwise.
    """

    tu = root._tu
    root.parent = None

    # Stores the path from `root` to the node being visited. We need this to set
    # `node.parent`.
    path: List[Cursor] = [root]

    exception: List[BaseException] = []

    @CFUNCTYPE(c_int, Cursor, Cursor, py_object)
    def actual_visitor(node: Cursor, parent: Cursor, client_data: None) -> int:

        try:

            # The `node` and `parent` `Cursor` objects are NOT reused in between
            # invocations of this visitor callback, so we can't assume that
            # `parent.parent` is set.

            while path[-1] != parent:
                path.pop()

            node.parent = path[-1]
            path.append(node)

            # several clang.cindex methods need Cursor._tu to be set
            node._tu = tu

            return visitor(node).value

        except BaseException as e:

            # Exceptions can't cross into C. Stash it, abort the visitation, and
            # reraise it.

            exception.append(e)
            return VisitorResult.BREAK.value

    result = conf.lib.clang_visitChildren(root, actual_visitor, None)

    if exception:
        raise exception[0]

    return result == 0


# ---------------------------------------------------------------------------- #
# Node predicates


def is_binary_operator(node: Cursor, operator: str) -> bool:
    return (
        node.kind == CursorKind.BINARY_OPERATOR
        and get_binary_operator_spelling(node) == operator
    )


def get_binary_operator_spelling(node: Cursor) -> Optional[str]:

    assert node.kind == CursorKind.BINARY_OPERATOR

    [left, right] = node.get_children()

    op_range = SourceRange.from_locations(left.extent.end, right.extent.start)

    tokens = list(TokenGroup.get_tokens(node.translation_unit, op_range))
    if not tokens:
        # Can occur when left and right children extents overlap due to
        # misparsing.
        return None

    [op_token, *_] = tokens
    return op_token.spelling


def might_have_attribute(node: Cursor, attr: Union[CursorKind, str]) -> bool:
    """
    Check whether any of `node`'s children are an attribute of the given kind,
    or an attribute of kind `UNEXPOSED_ATTR` with the given `str` spelling.

    This check is best-effort and may erroneously return `True`.
    """

    if isinstance(attr, CursorKind):

        assert attr.is_attribute()

        def matcher(n: Cursor) -> bool:
            return n.kind == attr

    else:

        def matcher(n: Cursor) -> bool:
            if n.kind != CursorKind.UNEXPOSED_ATTR:
                return False
            tokens = list(n.get_tokens())
            # `tokens` can have 0 or more than 1 element when the attribute
            # comes from a macro expansion. AFAICT, in that case we can't do
            # better here than tell callers that this might be the attribute
            # that they're looking for.
            return len(tokens) != 1 or tokens[0].spelling == attr

    return any(map(matcher, node.get_children()))


def is_annotated_with(node: Cursor, annotation: str) -> bool:
    return any(is_annotation(c, annotation) for c in node.get_children())


def is_annotation(node: Cursor, annotation: str) -> bool:
    return node.kind == CursorKind.ANNOTATE_ATTR and node.spelling == annotation


def is_comma_wrapper(node: Cursor, literal: str) -> bool:
    """
    Check if `node` is a "comma-wrapper" with the given string literal.

    A "comma-wrapper" is the pattern `((void)string_literal, expr)`. The `expr`
    is said to be "comma-wrapped".
    """

    # TODO: Do we need to check that the operator is `,`? Is there another
    # operator that can combine void and an expr?

    if node.kind != CursorKind.BINARY_OPERATOR:
        return False

    [left, _right] = node.get_children()

    if (
        left.kind != CursorKind.CSTYLE_CAST_EXPR
        or left.type.kind != TypeKind.VOID
    ):
        return False

    [unexposed_expr] = left.get_children()

    if unexposed_expr.kind != CursorKind.UNEXPOSED_EXPR:
        return False

    [string_literal] = unexposed_expr.get_children()

    return (
        string_literal.kind == CursorKind.STRING_LITERAL
        and string_literal.spelling == f'"{literal}"'
    )


# ---------------------------------------------------------------------------- #
# Checks


@dataclass
class CheckContext:

    translation_unit: TranslationUnit
    translation_unit_path: str  # exactly as reported by libclang

    _rel_path: str
    _build_working_dir: Path
    _problems_found: bool

    _printer: Callable[[str], None]

    def format_location(self, obj: Any) -> str:
        """obj must have a location field/property that is a
        `SourceLocation`."""
        return self._format_location(obj.location)

    def _format_location(self, loc: SourceLocation) -> str:

        if loc.file is None:
            return self._rel_path
        else:
            abs_path = (self._build_working_dir / loc.file.name).resolve()
            rel_path = os.path.relpath(abs_path)
            return f"{rel_path}:{loc.line}:{loc.column}"

    def report(self, node: Cursor, message: str) -> None:
        self._problems_found = True
        self._printer(f"{self.format_location(node)}: {message}")

    def print_node(self, node: Cursor) -> None:
        """This can be handy when developing checks."""

        print(f"{self.format_location(node)}: kind = {node.kind.name}", end="")

        if node.spelling:
            print(f", spelling = {node.spelling!r}", end="")

        if node.type is not None:
            print(f", type = {node.type.get_canonical().spelling!r}", end="")

        if node.referenced is not None:
            print(f", referenced = {node.referenced.spelling!r}", end="")

        start = self._format_location(node.extent.start)
        end = self._format_location(node.extent.end)
        print(f", extent = {start}--{end}")

    def print_tree(
        self,
        node: Cursor,
        *,
        max_depth: Optional[int] = None,
        indentation_level: int = 0,
    ) -> None:
        """This can be handy when developing checks."""

        if max_depth is None or max_depth >= 0:

            print("  " * indentation_level, end="")
            self.print_node(node)

            for child in node.get_children():
                self.print_tree(
                    child,
                    max_depth=None if max_depth is None else max_depth - 1,
                    indentation_level=indentation_level + 1,
                )


Checker = Callable[[CheckContext], None]


class CheckTestGroup:

    inputs: Sequence[str]
    expected_output: str

    location: str

    def __init__(self, inputs: Iterable[str], expected_output: str) -> None:
        def reformat_string(s: str) -> str:
            return "".join(
                line + "\n" for line in textwrap.dedent(s).strip().splitlines()
            )

        self.inputs = [reformat_string(input) for input in inputs]
        self.expected_output = reformat_string(expected_output)

        caller = inspect.stack()[1]
        self.location = f"{os.path.relpath(caller.filename)}:{caller.lineno}"


@dataclass
class Check:
    checker: Checker
    test_groups: List[CheckTestGroup]


CHECKS: Dict[str, Check] = {}


def check(name: str) -> Callable[[Checker], Checker]:
    def decorator(checker: Checker) -> Checker:
        assert name not in CHECKS
        CHECKS[name] = Check(checker=checker, test_groups=[])
        return checker

    return decorator


def add_check_tests(check_name: str, *groups: CheckTestGroup) -> None:
    CHECKS[check_name].test_groups.extend(groups)


# ---------------------------------------------------------------------------- #
# Import all checks

for path in Path(__file__).parent.glob("**/*.py"):
    if path.name != "__init__.py":
        rel_path = path.relative_to(Path(__file__).parent)
        module = "." + ".".join([*rel_path.parts[:-1], rel_path.stem])
        import_module(module, __package__)

# ---------------------------------------------------------------------------- #
