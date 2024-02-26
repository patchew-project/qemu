/*
 * Copyright (c) 2022 - 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/strList.h"

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
    const char *delim;
    const char *argv[5];
} list_data[] = {
    { NULL, ",", { NULL } },
    { "", ",", { NULL } },
    { "a", ",", { "a", NULL } },
    { "a,b", ",", { "a", "b", NULL } },
    { "a,b,c", ",", { "a", "b", "c", NULL } },
    { "first last", " ", { "first", "last", NULL } },
    { "a:", ":", { "a", "", NULL } },
    { "a::b", ":", { "a", "", "b", NULL } },
    { ":", ":", { "", "", NULL } },
    { ":a", ":", { "", "a", NULL } },
    { "::a", ":", { "", "", "a", NULL } },
};

static void test_strv(void)
{
    int i, j;
    const char **expect;
    strList *list;
    char **argv;

    for (i = 0; i < ARRAY_SIZE(list_data); i++) {
        expect = list_data[i].argv;
        list = str_split(list_data[i].string, list_data[i].delim);
        argv = strv_from_strList(list);
        qapi_free_strList(list);
        for (j = 0; expect[j] && argv[j]; j++) {
            g_assert_cmpstr(expect[j], ==, argv[j]);
        }
        g_assert_null(expect[j]);
        g_assert_null(argv[j]);
        g_strfreev(argv);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/test-string/length", test_length);
    g_test_add_func("/test-string/strv", test_strv);
    return g_test_run();
}
