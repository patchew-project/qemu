/*
 * Test translation of calls to declration
 *
 * This test will be ran without --translate-all-helpers, meaning we have to
 * manually mark them for translation.
 */

#define ANNOTATE(str) __attribute__((annotate(str)))

ANNOTATE("immediate: 0")
int fdecl0(int a);

ANNOTATE("helper-to-tcg")
int f0(int a, int off) {
    return a + fdecl0(off);
}

ANNOTATE("helper-to-tcg")
int f1(int a) {
    return f0(a,a) + f0(-a,-a);
}
