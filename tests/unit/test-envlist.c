/*
 * testenvlist unit-tests.
 */

#include "qemu/osdep.h"
#include "qemu/envlist.h"

static void envlist_create_free_test(void)
{
    envlist_t *testenvlist;

    testenvlist = envlist_create();
    g_assert(testenvlist != NULL);
    g_assert(testenvlist->el_count == 0);

    envlist_free(testenvlist);
}

static void envlist_set_unset_test(void)
{
    envlist_t *testenvlist;
    const char *env = "TEST=123";
    struct envlist_entry *entry;

    testenvlist = envlist_create();
    g_assert(envlist_setenv(testenvlist, env) == 0);
    g_assert(testenvlist->el_count == 1);
    entry = testenvlist->el_entries.lh_first;
    g_assert_cmpstr(entry->ev_var, ==, "TEST=123");
    g_assert(envlist_unsetenv(testenvlist, "TEST") == 0);
    g_assert(testenvlist->el_count == 0);

    envlist_free(testenvlist);
}

static void envlist_parse_set_unset_test(void)
{
    envlist_t *testenvlist;
    const char *env = "TEST1=123,TEST2=456";

    testenvlist = envlist_create();
    g_assert(envlist_parse_set(testenvlist, env) == 0);
    g_assert(testenvlist->el_count == 2);
    g_assert(envlist_parse_unset(testenvlist, "TEST1,TEST2") == 0);
    g_assert(testenvlist->el_count == 0);

    envlist_free(testenvlist);
}

static void envlist_appendenv_test(void)
{
    envlist_t *testenvlist;
    const char *env = "TEST=123";
    struct envlist_entry *entry;

    testenvlist = envlist_create();
    g_assert(envlist_setenv(testenvlist, env) == 0);
    g_assert(envlist_appendenv(testenvlist, "TEST=456", ";") == 0);
    entry = testenvlist->el_entries.lh_first;
    g_assert_cmpstr(entry->ev_var, ==, "TEST=123;456");

    envlist_free(testenvlist);
}

static void envlist_to_environ_test(void)
{
    envlist_t *testenvlist;
    const char *env = "TEST1=123,TEST2=456";
    size_t count;
    char **environ;

    testenvlist = envlist_create();
    g_assert(envlist_parse_set(testenvlist, env) == 0);
    g_assert(testenvlist->el_count == 2);
    environ = envlist_to_environ(testenvlist, &count);
    g_assert(count == 2);
    g_assert_cmpstr(environ[1], ==, "TEST1=123");
    g_assert_cmpstr(environ[0], ==, "TEST2=456");

    envlist_free(testenvlist);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/envlist/create_free", envlist_create_free_test);
    g_test_add_func("/envlist/set_unset", envlist_set_unset_test);
    g_test_add_func("/envlist/parse_set_unset", envlist_parse_set_unset_test);
    g_test_add_func("/envlist/appendenv", envlist_appendenv_test);
    g_test_add_func("/envlist/to_environ", envlist_to_environ_test);

    return g_test_run();
}
