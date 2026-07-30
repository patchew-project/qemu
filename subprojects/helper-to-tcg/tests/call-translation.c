/*
 * Test translation of dependent functions.
 *
 * This test will be ran without --translate-all-helpers, meaning we have to
 * manually mark them for translation.
 */

#define ANNOTATE(str) __attribute__((annotate(str)))

/*
 * Only inner0 is marked for translation, outer0 will be skipped.
 */
ANNOTATE("helper-to-tcg")
int inner0(int a) { return a + 5; }

int outer0(int cond, int b) {
    if (cond) {
        return inner0(b);
    }
    return 0;
}

/*
 * Both inner1 and outer1 will be translated since outer1 is marked for
 * translation and depends on inner1.
 */
int inner1(int a) { return a + 5; }

ANNOTATE("helper-to-tcg")
int outer1(int cond, int b) {
    if (cond) {
        return inner1(b);
    }
    return 0;
}
