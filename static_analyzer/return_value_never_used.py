# ---------------------------------------------------------------------------- #

from typing import Dict

from clang.cindex import (  # type: ignore
    Cursor,
    CursorKind,
    StorageClass,
    TypeKind,
)

from static_analyzer import (
    CheckContext,
    CheckTestGroup,
    VisitorResult,
    add_check_tests,
    check,
    might_have_attribute,
    visit,
)

# ---------------------------------------------------------------------------- #


@check("return-value-never-used")
def check_return_value_never_used(context: CheckContext) -> None:
    """Report static functions with a non-void return value that no caller ever
    uses."""

    # Maps canonical function `Cursor`s to whether we found a place that maybe
    # uses their return value. Only includes static functions that don't return
    # void, don't have __attribute__((unused)), and belong to the translation
    # unit's root file (i.e., were not brought in by an #include).
    funcs: Dict[Cursor, bool] = {}

    def visitor(node: Cursor) -> VisitorResult:

        if (
            node.kind == CursorKind.FUNCTION_DECL
            and node.storage_class == StorageClass.STATIC
            and node.location.file.name == context.translation_unit_path
            and node.type.get_result().get_canonical().kind != TypeKind.VOID
            and not might_have_attribute(node, "unused")
        ):
            funcs.setdefault(node.canonical, False)

        if (
            node.kind == CursorKind.DECL_REF_EXPR
            and node.referenced.kind == CursorKind.FUNCTION_DECL
            and node.referenced.canonical in funcs
            and function_occurrence_might_use_return_value(node)
        ):
            funcs[node.referenced.canonical] = True

        return VisitorResult.RECURSE

    visit(context.translation_unit.cursor, visitor)

    for (cursor, return_value_maybe_used) in funcs.items():
        if not return_value_maybe_used:
            context.report(
                cursor, f"{cursor.spelling}() return value is never used"
            )


def function_occurrence_might_use_return_value(node: Cursor) -> bool:

    parent = get_parent_if_unexposed_expr(node.parent)

    if parent.kind.is_statement():

        return False

    elif (
        parent.kind == CursorKind.CALL_EXPR
        and parent.referenced == node.referenced
    ):

        grandparent = get_parent_if_unexposed_expr(parent.parent)

        if not grandparent.kind.is_statement():
            return True

        if grandparent.kind in [
            CursorKind.IF_STMT,
            CursorKind.SWITCH_STMT,
            CursorKind.WHILE_STMT,
            CursorKind.DO_STMT,
            CursorKind.RETURN_STMT,
        ]:
            return True

        if grandparent.kind == CursorKind.FOR_STMT:

            [*for_parts, for_body] = grandparent.get_children()
            if len(for_parts) == 0:
                return False
            elif len(for_parts) in [1, 2]:
                return True  # call may be in condition part of for loop
            elif len(for_parts) == 3:
                # Comparison doesn't work properly with `Cursor`s originating
                # from nested visitations, so we compare the extent instead.
                return parent.extent == for_parts[1].extent
            else:
                assert False

        return False

    else:

        # might be doing something with a pointer to the function
        return True


def get_parent_if_unexposed_expr(node: Cursor) -> Cursor:
    return node.parent if node.kind == CursorKind.UNEXPOSED_EXPR else node


# ---------------------------------------------------------------------------- #

add_check_tests(
    "return-value-never-used",
    CheckTestGroup(
        inputs=[
            R"""
                static void f(void) { }
                """,
            R"""
                static __attribute__((unused)) int f(void) { }
                """,
            R"""
                #define ATTR __attribute__((unused))
                static __attribute__((unused)) int f(void) { }
                """,
            R"""
                #define FUNC static __attribute__((unused)) int f(void) { }
                FUNC
                """,
            R"""
                static __attribute__((unused, constructor)) int f(void) { }
                """,
            R"""
                static __attribute__((constructor, unused)) int f(void) { }
                """,
            R"""
                static int f(void) { return 42; }
                static void g(void) {
                    int x = f();
                }
                """,
            R"""
                static int f(void) { return 42; }
                static void g(void) {
                    for (0; f(); 0) { }
                }
                """,
            R"""
                static int f(void) { return 42; }
                static void g(void) {
                    for (f(); ; ) { }
                }
                """,
            R"""
                static int f(void) { return 42; }
                static void g(void) {
                    for ( ; f(); ) { }
                }
                """,
            R"""
                static int f(void) { return 42; }
                static void g(void) {
                    for ( ; ; f()) { }
                }
                """,
            R"""
                static int f(void) { return 42; }
                static void g(void) {
                    int (*ptr)(void) = 0;
                    __atomic_store_n(&ptr, f, __ATOMIC_RELAXED);
                }
                """,
        ],
        expected_output=R"""
            """,
    ),
    CheckTestGroup(
        inputs=[
            R"""
                static int f(void) { return 42; }
                """,
            R"""
                static int f(void) { return 42; }
                static void g(void) {
                    f();
                }
                """,
            R"""
                static int f(void) { return 42; }
                static void g(void) {
                    for (f(); 0; f()) { }
                }
                """,
        ],
        expected_output=R"""
            file.c:1:12: f() return value is never used
            """,
    ),
    CheckTestGroup(
        inputs=[
            R"""
                static __attribute__((constructor)) int f(void) { }
                """,
        ],
        expected_output=R"""
            file.c:1:41: f() return value is never used
            """,
    ),
)

# ---------------------------------------------------------------------------- #
