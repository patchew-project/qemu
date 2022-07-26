/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/util.h"
#include "qapi/qapi-builtin-types.h"

static strList *make_list(int length)
{
    strList *head = 0, *list, **prev = &head;

    while (length--) {
        list = *prev = g_new0(strList, 1);
        list->value = g_strdup("aaa");
        prev = &list->next;
    }
    return head;
}

static void test_length(void)
{
    strList *list;
    int i;

    for (i = 0; i < 5; i++) {
        list = make_list(i);
        g_assert_cmpint(i, ==, QAPI_LIST_LENGTH(list));
        qapi_free_strList(list);
    }
}

struct {
    const char *string;
    char delim;
    const char *args[5];
} list_data[] = {
    { 0, ',', { 0 } },
    { "", ',', { 0 } },
    { "a", ',', { "a", 0 } },
    { "a,b", ',', { "a", "b", 0 } },
    { "a,b,c", ',', { "a", "b", "c", 0 } },
    { "first last", ' ', { "first", "last", 0 } },
    { "a:", ':', { "a", 0 } },
    { "a::b", ':', { "a", "", "b", 0 } },
    { ":", ':', { "", 0 } },
    { ":a", ':', { "", "a", 0 } },
    { "::a", ':', { "", "", "a", 0 } },
};

static void test_strv(void)
{
    int i, j;
    const char **expect;
    strList *list;
    GStrv args;

    for (i = 0; i < ARRAY_SIZE(list_data); i++) {
        expect = list_data[i].args;
        list = strList_from_string(list_data[i].string, list_data[i].delim);
        args = strv_from_strList(list);
        qapi_free_strList(list);
        for (j = 0; expect[j] && args[j]; j++) {
            g_assert_cmpstr(expect[j], ==, args[j]);
        }
        g_assert_null(expect[j]);
        g_assert_null(args[j]);
        g_strfreev(args);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/test-string/length", test_length);
    g_test_add_func("/test-string/strv", test_strv);
    return g_test_run();
}
