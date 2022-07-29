# ---------------------------------------------------------------------------- #

from clang.cindex import Cursor, CursorKind  # type: ignore

from static_analyzer import (
    CheckContext,
    VisitorResult,
    check,
    is_annotation,
    is_annotated_with,
    visit,
)
from static_analyzer.coroutine_fn import is_coroutine_fn

# ---------------------------------------------------------------------------- #


@check("no_coroutine_fn")
def check_no_coroutine_fn(context: CheckContext) -> None:
    """Reports violations of no_coroutine_fn rules."""

    def visitor(node: Cursor) -> VisitorResult:

        validate_annotations(context, node)

        if node.kind == CursorKind.FUNCTION_DECL and node.is_definition():
            check_calls(context, node)
            return VisitorResult.CONTINUE

        return VisitorResult.RECURSE

    visit(context.translation_unit.cursor, visitor)


def validate_annotations(context: CheckContext, node: Cursor) -> None:

    # validate annotation usage

    if is_annotation(node, "no_coroutine_fn") and (
        node.parent is None or not is_valid_no_coroutine_fn_usage(node.parent)
    ):
        context.report(node, "invalid no_coroutine_fn usage")

    # reject re-declarations with inconsistent annotations

    if node.kind == CursorKind.FUNCTION_DECL and is_no_coroutine_fn(
        node
    ) != is_no_coroutine_fn(node.canonical):

        context.report(
            node,
            f"no_coroutine_fn annotation disagreement with"
            f" {context.format_location(node.canonical)}",
        )


def check_calls(context: CheckContext, caller: Cursor) -> None:
    """
    Reject calls from coroutine_fn to no_coroutine_fn.

    Assumes that `caller` is a function definition.
    """

    if is_coroutine_fn(caller):

        def visitor(node: Cursor) -> VisitorResult:

            # We can get some "calls" that are actually things like top-level
            # macro invocations for which `node.referenced` is None.

            if (
                node.kind == CursorKind.CALL_EXPR
                and node.referenced is not None
                and is_no_coroutine_fn(node.referenced.canonical)
            ):
                context.report(
                    node,
                    f"coroutine_fn calls no_coroutine_fn function"
                    f" {node.referenced.spelling}()",
                )

            return VisitorResult.RECURSE

        visit(caller, visitor)


# ---------------------------------------------------------------------------- #


def is_valid_no_coroutine_fn_usage(parent: Cursor) -> bool:
    """
    Checks if an occurrence of `no_coroutine_fn` represented by a node with
    parent `parent` appears at a valid point in the AST. This is the case if the
    parent is a function declaration/definition.
    """

    return parent.kind == CursorKind.FUNCTION_DECL


def is_no_coroutine_fn(node: Cursor) -> bool:
    """
    Checks whether the given `node` should be considered to be
    `no_coroutine_fn`.

    This assumes valid usage of `no_coroutine_fn`.
    """

    return is_annotated_with(node, "no_coroutine_fn")


# ---------------------------------------------------------------------------- #
