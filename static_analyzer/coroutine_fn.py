# ---------------------------------------------------------------------------- #

from clang.cindex import Cursor, CursorKind, TypeKind  # type: ignore

from static_analyzer import (
    CheckContext,
    VisitorResult,
    check,
    is_annotated_with,
    is_annotation,
    is_comma_wrapper,
    visit,
)

# ---------------------------------------------------------------------------- #


@check("coroutine_fn")
def check_coroutine_fn(context: CheckContext) -> None:
    """Reports violations of coroutine_fn rules."""

    def visitor(node: Cursor) -> VisitorResult:

        validate_annotations(context, node)

        if node.kind == CursorKind.FUNCTION_DECL and node.is_definition():
            check_direct_calls(context, node)
            return VisitorResult.CONTINUE

        return VisitorResult.RECURSE

    visit(context.translation_unit.cursor, visitor)


def validate_annotations(context: CheckContext, node: Cursor) -> None:

    # validate annotation usage

    if is_annotation(node, "coroutine_fn") and (
        node.parent is None or not is_valid_coroutine_fn_usage(node.parent)
    ):
        context.report(node, "invalid coroutine_fn usage")

    if is_comma_wrapper(
        node, "__allow_coroutine_fn_call"
    ) and not is_valid_allow_coroutine_fn_call_usage(node):
        context.report(node, "invalid __allow_coroutine_fn_call usage")

    # reject re-declarations with inconsistent annotations

    if node.kind == CursorKind.FUNCTION_DECL and is_coroutine_fn(
        node
    ) != is_coroutine_fn(node.canonical):
        context.report(
            node,
            f"coroutine_fn annotation disagreement with"
            f" {context.format_location(node.canonical)}",
        )


def check_direct_calls(context: CheckContext, caller: Cursor) -> None:
    """
    Reject calls from non-coroutine_fn to coroutine_fn.

    Assumes that `caller` is a function definition.
    """

    if not is_coroutine_fn(caller):

        def visitor(node: Cursor) -> VisitorResult:

            # We can get "calls" that are actually things like top-level macro
            # invocations for which `node.referenced` is None.

            if (
                node.kind == CursorKind.CALL_EXPR
                and node.referenced is not None
                and is_coroutine_fn(node.referenced.canonical)
                and not is_comma_wrapper(
                    node.parent, "__allow_coroutine_fn_call"
                )
            ):
                context.report(
                    node,
                    f"non-coroutine_fn function calls coroutine_fn"
                    f" {node.referenced.spelling}()",
                )

            return VisitorResult.RECURSE

        visit(caller, visitor)


# ---------------------------------------------------------------------------- #


def is_valid_coroutine_fn_usage(parent: Cursor) -> bool:
    """
    Check if an occurrence of `coroutine_fn` represented by a node with parent
    `parent` appears at a valid point in the AST. This is the case if `parent`
    is:

      - A function declaration/definition, OR
      - A field/variable/parameter declaration with a function pointer type, OR
      - A typedef of a function type or function pointer type.
    """

    if parent.kind == CursorKind.FUNCTION_DECL:
        return True

    canonical_type = parent.type.get_canonical()

    def parent_type_is_function() -> bool:
        return canonical_type.kind == TypeKind.FUNCTIONPROTO

    def parent_type_is_function_pointer() -> bool:
        return (
            canonical_type.kind == TypeKind.POINTER
            and canonical_type.get_pointee().kind == TypeKind.FUNCTIONPROTO
        )

    if parent.kind in [
        CursorKind.FIELD_DECL,
        CursorKind.VAR_DECL,
        CursorKind.PARM_DECL,
    ]:
        return parent_type_is_function_pointer()

    if parent.kind == CursorKind.TYPEDEF_DECL:
        return parent_type_is_function() or parent_type_is_function_pointer()

    return False


def is_valid_allow_coroutine_fn_call_usage(node: Cursor) -> bool:
    """
    Check if an occurrence of `__allow_coroutine_fn_call()` represented by node
    `node` appears at a valid point in the AST. This is the case if its right
    operand is a call to:

      - A function declared with the `coroutine_fn` annotation.

    TODO: Ensure that `__allow_coroutine_fn_call()` is in the body of a
    non-`coroutine_fn` function.
    """

    [_, call] = node.get_children()

    return call.kind == CursorKind.CALL_EXPR and is_coroutine_fn(
        call.referenced
    )


def is_coroutine_fn(node: Cursor) -> bool:
    """
    Check whether the given `node` should be considered to be `coroutine_fn`.

    This assumes valid usage of `coroutine_fn`.
    """

    while node.kind in [CursorKind.PAREN_EXPR, CursorKind.UNEXPOSED_EXPR]:
        children = list(node.get_children())
        if len(children) == 1:
            node = children[0]
        else:
            break

    return node.kind == CursorKind.FUNCTION_DECL and is_annotated_with(
        node, "coroutine_fn"
    )


# ---------------------------------------------------------------------------- #
