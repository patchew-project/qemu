/*
 * String Output Visitor unit-tests.
 *
 * Copyright (C) 2012 Red Hat Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com> (based on test-qmp-output-visitor)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/text-output-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/types.h"


static void test_visitor_out_int(void)
{
    int64_t value = 42;
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    visit_type_int(v, NULL, &value, &error_abort);

    visit_complete(v, &str);
    g_assert_cmpstr(str, ==, "42\n");
    g_free(str);
    visit_free(v);
}


static void test_visitor_out_size(void)
{
    uint64_t value = 1729;
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    visit_type_size(v, NULL, &value, &error_abort);

    visit_complete(v, &str);
    g_assert_cmpstr(str, ==, "1729 (1.69 KiB)\n");
    g_free(str);
    visit_free(v);
}

static void test_visitor_out_intList(void)
{
    int64_t value[] = {0, 1, 9, 10, 16, 15, 14,
        3, 4, 5, 6, 11, 12, 13, 21, 22, INT64_MAX - 1, INT64_MAX};
    intList *list = NULL, **tmp = &list;
    int i;
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    for (i = 0; i < sizeof(value) / sizeof(value[0]); i++) {
        *tmp = g_malloc0(sizeof(**tmp));
        (*tmp)->value = value[i];
        tmp = &(*tmp)->next;
    }

    visit_type_intList(v, NULL, &list, &error_abort);

    visit_complete(v, &str);
    g_assert_cmpstr(str, ==,
                    "    [0]: 0\n"
                    "    [1]: 1\n"
                    "    [2]: 9\n"
                    "    [3]: 10\n"
                    "    [4]: 16\n"
                    "    [5]: 15\n"
                    "    [6]: 14\n"
                    "    [7]: 3\n"
                    "    [8]: 4\n"
                    "    [9]: 5\n"
                    "    [10]: 6\n"
                    "    [11]: 11\n"
                    "    [12]: 12\n"
                    "    [13]: 13\n"
                    "    [14]: 21\n"
                    "    [15]: 22\n"
                    "    [16]: 9223372036854775806\n"
                    "    [17]: 9223372036854775807\n");
    qapi_free_intList(list);
    g_free(str);
    visit_free(v);
}

static void test_visitor_out_bool(void)
{
    bool value = true;
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    visit_type_bool(v, NULL, &value, &error_abort);

    visit_complete(v, &str);
    g_assert_cmpstr(str, ==, "true\n");
    g_free(str);
    visit_free(v);
}

static void test_visitor_out_number(void)
{
    double value = 3.14;
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    visit_type_number(v, NULL, &value, &error_abort);

    visit_complete(v, &str);
    g_assert_cmpstr(str, ==, "3.140000\n");
    g_free(str);
    visit_free(v);
}

static void test_visitor_out_string(void)
{
    const char *string = "Q E M U";
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    visit_type_str(v, NULL, (char **)&string, &error_abort);

    visit_complete(v, &str);
    g_assert_cmpstr(str, ==, "Q E M U\n");
    g_free(str);
    visit_free(v);
}

static void test_visitor_out_no_string(void)
{
    char *string = NULL;
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    /* A null string should return "" */
    visit_type_str(v, NULL, &string, &error_abort);

    visit_complete(v, &str);
    g_assert_cmpstr(str, ==, "<null>\n");
    g_free(str);
    visit_free(v);
}

static void test_visitor_out_enum(void)
{
    char *actual, *expected;
    EnumOne i;
    Visitor *v;

    for (i = 0; i < ENUM_ONE__MAX; i++) {
        v = text_output_visitor_new(0, 0);
        g_assert(v);

        visit_type_EnumOne(v, "val", &i, &error_abort);

        visit_complete(v, &actual);
        expected = g_strdup_printf("val: %s\n", EnumOne_lookup[i]);

        g_assert_cmpstr(actual, ==, expected);
        g_free(expected);
        g_free(actual);
        visit_free(v);
    }
}

static void test_visitor_out_enum_errors(void)
{
    EnumOne i, bad_values[] = { ENUM_ONE__MAX, -1 };
    Visitor *v;

    for (i = 0; i < ARRAY_SIZE(bad_values) ; i++) {
        v = text_output_visitor_new(0, 0);
        g_assert(v);

        Error *err = NULL;
        visit_type_EnumOne(v, "unused", &bad_values[i], &err);
        error_free_or_abort(&err);
        visit_free(v);
    }
}


static void test_visitor_out_struct_named(void)
{
    const char *string = "hello";
    int64_t i = 1729;
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    visit_start_struct(v, NULL, NULL, 0, &error_abort);

    visit_type_str(v, "name", (char **)&string, &error_abort);

    visit_type_int(v, "num", &i, &error_abort);

    visit_end_struct(v, NULL);
    visit_complete(v, &str);
    g_assert_cmpstr(str, ==,
                    "    name: hello\n"
                    "    num: 1729\n");
    g_free(str);
    visit_free(v);
}


static void test_visitor_out_struct_anon(void)
{
    const char *string = "hello";
    int64_t i = 1729;
    char *str;
    Visitor *v;

    v = text_output_visitor_new(0, 1);
    g_assert(v);

    visit_start_struct(v, NULL, NULL, 0, &error_abort);

    visit_type_str(v, NULL, (char **)&string, &error_abort);

    visit_type_int(v, NULL, &i, &error_abort);

    visit_end_struct(v, NULL);
    visit_complete(v, &str);
    g_assert_cmpstr(str, ==,
                    "<anon>: hello\n"
                    "<anon>: 1729\n");
    g_free(str);
    visit_free(v);
}


static void test_visitor_out_complex(void)
{
    const char *string = "hello";
    const char *string2 = "world";
    int64_t n = 1729;
    char *str;
    Visitor *v;
    gsize i;

    v = text_output_visitor_new(0, 0);
    g_assert(v);

    visit_type_str(v, "full-name", (char **)&string, &error_abort);

    visit_type_int(v, "num", &n, &error_abort);

    visit_start_list(v, "accounts", NULL, 0, &error_abort);

    for (i = 0; i < 5; i++) {
        visit_start_struct(v, "account", NULL, 0, &error_abort);

        visit_type_int(v, "num", &n, &error_abort);
        visit_type_str(v, "name", (char **)&string, &error_abort);

        if (i == 2) {
            visit_start_struct(v, "info", NULL, 0, &error_abort);
            visit_type_str(v, "help", (char **)&string2, &error_abort);
            visit_end_struct(v, NULL);
        } else if (i == 4) {
            visit_start_list(v, "payment-info", NULL, 0, &error_abort);
            visit_type_int(v, "num", &n, &error_abort);
            visit_type_int(v, "num", &n, &error_abort);
            visit_type_int(v, "num", &n, &error_abort);
            visit_end_list(v, NULL);
        }

        visit_end_struct(v, NULL);
    }

    visit_end_list(v, NULL);

    visit_complete(v, &str);
    g_assert_cmpstr(str, ==,
                    "full name: hello\n"
                    "num: 1729\n"
                    "accounts:\n"
                    "    [0]:\n"
                    "        num: 1729\n"
                    "        name: hello\n"
                    "    [1]:\n"
                    "        num: 1729\n"
                    "        name: hello\n"
                    "    [2]:\n"
                    "        num: 1729\n"
                    "        name: hello\n"
                    "        info:\n"
                    "            help: world\n"
                    "    [3]:\n"
                    "        num: 1729\n"
                    "        name: hello\n"
                    "    [4]:\n"
                    "        num: 1729\n"
                    "        name: hello\n"
                    "        payment info:\n"
                    "            [0]: 1729\n"
                    "            [1]: 1729\n"
                    "            [2]: 1729\n");
    g_free(str);
    visit_free(v);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/string-visitor/output/int",
                    test_visitor_out_int);
    g_test_add_func("/string-visitor/output/size",
                    test_visitor_out_size);
    g_test_add_func("/string-visitor/output/bool",
                    test_visitor_out_bool);
    g_test_add_func("/string-visitor/output/number",
                    test_visitor_out_number);
    g_test_add_func("/string-visitor/output/string",
                    test_visitor_out_string);
    g_test_add_func("/string-visitor/output/no-string",
                    test_visitor_out_no_string);
    g_test_add_func("/string-visitor/output/enum",
                    test_visitor_out_enum);
    g_test_add_func("/string-visitor/output/enum-errors",
                    test_visitor_out_enum_errors);
    g_test_add_func("/string-visitor/output/intList",
                    test_visitor_out_intList);
    g_test_add_func("/string-visitor/output/struct-named",
                    test_visitor_out_struct_named);
    g_test_add_func("/string-visitor/output/struct-anon",
                    test_visitor_out_struct_anon);
    g_test_add_func("/string-visitor/output/complex",
                    test_visitor_out_complex);
    g_test_run();

    return 0;
}
