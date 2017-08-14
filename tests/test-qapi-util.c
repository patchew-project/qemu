/*
 * Unit tests for QAPI utility functions
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/util.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qjson.h"
#include "test-qapi-types.h"

static void test_qapi_enum_parse(void)
{
    Error *err = NULL;
    int ret;

    ret = qapi_enum_parse(QType_lookup, NULL, QTYPE__MAX, QTYPE_NONE,
                          &error_abort);
    g_assert_cmpint(ret, ==, QTYPE_NONE);

    ret = qapi_enum_parse(QType_lookup, "junk", QTYPE__MAX, -1,
                          NULL);
    g_assert_cmpint(ret, ==, -1);

    ret = qapi_enum_parse(QType_lookup, "junk", QTYPE__MAX, -1,
                          &err);
    error_free_or_abort(&err);

    ret = qapi_enum_parse(QType_lookup, "none", QTYPE__MAX, -1,
                          &error_abort);
    g_assert_cmpint(ret, ==, QTYPE_NONE);

    ret = qapi_enum_parse(QType_lookup, QType_lookup[QTYPE__MAX - 1],
                          QTYPE__MAX, QTYPE__MAX - 1,
                          &error_abort);
    g_assert_cmpint(ret, ==, QTYPE__MAX - 1);
}

static void test_parse_qapi_name(void)
{
    int ret;

    /* Must start with a letter */
    ret = parse_qapi_name("a", true);
    g_assert(ret == 1);
    ret = parse_qapi_name("a$", false);
    g_assert(ret == 1);
    ret = parse_qapi_name("", false);
    g_assert(ret == -1);
    ret = parse_qapi_name("1", false);
    g_assert(ret == -1);

    /* Only letters, digits, hyphen, underscore */
    ret = parse_qapi_name("A-Za-z0-9_", true);
    g_assert(ret == 10);
    ret = parse_qapi_name("A-Za-z0-9_$", false);
    g_assert(ret == 10);
    ret = parse_qapi_name("A-Za-z0-9_$", true);
    g_assert(ret == -1);

    /* __RFQDN_ */
    ret = parse_qapi_name("__com.redhat_supports", true);
    g_assert(ret == 21);
    ret = parse_qapi_name("_com.example_", false);
    g_assert(ret == -1);
    ret = parse_qapi_name("__com.example", false);
    g_assert(ret == -1);
    ret = parse_qapi_name("__com.example_", false);
    g_assert(ret == -1);
}

static void test_qobject_compare(void)
{
    QString *a1 = qstring_from_str("abc");
    QString *a2 = qstring_from_str("abc");
    QString *b = qstring_from_str("bcd");
    QNum *i1 = qnum_from_int(100);
    QNum *i2 = qnum_from_int(100);
    QNum *j = qnum_from_int(200);
    QList *l1 = qlist_new();
    QList *l2 = qlist_new();
    QList *m = qlist_new();

    qlist_append_int(l1, 100);
    qlist_append_int(l1, 200);
    qlist_append_int(l2, 100);
    qlist_append_int(l2, 200);

    qlist_append_int(m, 100);
    qlist_append_int(m, 300);

    g_assert_cmpint(qobject_compare(QOBJECT(a1), QOBJECT(a2)), ==, 0);
    g_assert_cmpint(qobject_compare(QOBJECT(i1), QOBJECT(i2)), ==, 0);
    g_assert_cmpint(qobject_compare(QOBJECT(l1), QOBJECT(l2)), ==, 0);

    g_assert_cmpint(qobject_compare(QOBJECT(a1), QOBJECT(b)), <, 0);
    g_assert_cmpint(qobject_compare(QOBJECT(b), QOBJECT(a1)), >, 0);

    g_assert_cmpint(qobject_compare(QOBJECT(i1), QOBJECT(j)), <, 0);
    g_assert_cmpint(qobject_compare(QOBJECT(j), QOBJECT(i1)), >, 0);

    g_assert_cmpint(qobject_compare(QOBJECT(l1), QOBJECT(m)), <, 0);
    g_assert_cmpint(qobject_compare(QOBJECT(m), QOBJECT(l1)), >, 0);

    g_assert_cmpint(qobject_compare(QOBJECT(a1), QOBJECT(i1)), !=, 0);
    g_assert_cmpint(qobject_compare(QOBJECT(a1), QOBJECT(l1)), !=, 0);
    g_assert_cmpint(qobject_compare(QOBJECT(l1), QOBJECT(i1)), !=, 0);

    QDECREF(a1);
    QDECREF(a2);
    QDECREF(b);
    QDECREF(i1);
    QDECREF(i2);
    QDECREF(j);
    QDECREF(l1);
    QDECREF(l2);
    QDECREF(m);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qapi/util/qapi_enum_parse", test_qapi_enum_parse);
    g_test_add_func("/qapi/util/parse_qapi_name", test_parse_qapi_name);
    g_test_add_func("/qapi/util/qobject_compare", test_qobject_compare);
    g_test_run();
    return 0;
}
