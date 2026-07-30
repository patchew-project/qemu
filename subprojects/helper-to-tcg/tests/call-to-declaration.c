/*
 * Test translation of calls to declration
 *
 * (experimental)
 */

#define ANNOTATE(str) __attribute__((annotate(str)))

int fdecl0(int a);

ANNOTATE("helper-to-tcg")
int f0(int cond, int b) {
    return fdecl0(b);
}

ANNOTATE("immediate: 0")
int fdecl1(int a);

ANNOTATE("helper-to-tcg")
ANNOTATE("immediate: 1")
int f1(int cond, int b) {
    return fdecl1(b);
}

ANNOTATE("returns-immediate")
ANNOTATE("immediate: 0")
int fdecl2(int a);

ANNOTATE("helper-to-tcg")
ANNOTATE("immediate: 0")
int f2(int b) {
    return fdecl2(b);
}
