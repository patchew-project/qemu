/*
 * QNum unit-tests.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "qapi/qmp/qnum.h"
#include "qapi/error.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qnum_from_int_test(void)
{
    QNum *qi;
    const int value = -42;

    qi = qnum_from_int(value);
    g_assert(qi != NULL);
    g_assert_cmpint(qi->u.i64, ==, value);
    g_assert_cmpint(qi->base.refcnt, ==, 1);
    g_assert_cmpint(qobject_type(QOBJECT(qi)), ==, QTYPE_QNUM);

    // destroy doesn't exit yet
    g_free(qi);
}

static void qnum_from_uint_test(void)
{
    QNum *qu;
    const int value = UINT_MAX;

    qu = qnum_from_int(value);
    g_assert(qu != NULL);
    g_assert(qu->u.u64 == value);
    g_assert(qu->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qu)) == QTYPE_QNUM);

    // destroy doesn't exit yet
    g_free(qu);
}

static void qnum_from_double_test(void)
{
    QNum *qf;
    const double value = -42.23423;

    qf = qnum_from_double(value);
    g_assert(qf != NULL);
    g_assert_cmpfloat(qf->u.dbl, ==, value);
    g_assert_cmpint(qf->base.refcnt, ==, 1);
    g_assert_cmpint(qobject_type(QOBJECT(qf)), ==, QTYPE_QNUM);

    // destroy doesn't exit yet
    g_free(qf);
}

static void qnum_from_int64_test(void)
{
    QNum *qi;
    const int64_t value = 0x1234567890abcdefLL;

    qi = qnum_from_int(value);
    g_assert_cmpint((int64_t) qi->u.i64, ==, value);

    QDECREF(qi);
}

static void qnum_get_int_test(void)
{
    QNum *qi;
    const int value = 123456;

    qi = qnum_from_int(value);
    g_assert_cmpint(qnum_get_int(qi, &error_abort), ==, value);

    QDECREF(qi);
}

static void qnum_get_uint_test(void)
{
    QNum *qn;
    const int value = 123456;
    Error *err = NULL;

    qn = qnum_from_uint(value);
    g_assert(qnum_get_uint(qn, &error_abort) == value);
    QDECREF(qn);

    qn = qnum_from_int(value);
    g_assert(qnum_get_uint(qn, &error_abort) == value);
    QDECREF(qn);

    qn = qnum_from_int(-1);
    qnum_get_uint(qn, &err);
    error_free_or_abort(&err);
    QDECREF(qn);

    /* temporarily disabled until visitor is switched */
    /* qn = qnum_from_uint(-1ULL); */
    /* qnum_get_int(qn, &err); */
    /* error_free_or_abort(&err); */
    /* QDECREF(qn); */

    /* invalid case */
    qn = qnum_from_double(0.42);
    qnum_get_uint(qn, &err);
    error_free_or_abort(&err);
    QDECREF(qn);
}

static void qobject_to_qnum_test(void)
{
    QNum *qn;

    qn = qnum_from_int(0);
    g_assert(qobject_to_qnum(QOBJECT(qn)) == qn);
    QDECREF(qn);

    qn = qnum_from_double(0);
    g_assert(qobject_to_qnum(QOBJECT(qn)) == qn);
    QDECREF(qn);
}

static void qnum_to_string_test(void)
{
    QNum *qn;
    char *tmp;

    qn = qnum_from_int(123456);
    tmp = qnum_to_string(qn);
    g_assert_cmpstr(tmp, ==, "123456");
    g_free(tmp);
    QDECREF(qn);

    qn = qnum_from_double(0.42);
    tmp = qnum_to_string(qn);
    g_assert_cmpstr(tmp, ==, "0.42");
    g_free(tmp);
    QDECREF(qn);
}

static void qnum_destroy_test(void)
{
    QNum *qn;

    qn = qnum_from_int(0);
    QDECREF(qn);

    qn = qnum_from_uint(0);
    QDECREF(qn);

    qn = qnum_from_double(0.42);
    QDECREF(qn);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/qnum/from_int", qnum_from_int_test);
    g_test_add_func("/qnum/from_uint", qnum_from_uint_test);
    g_test_add_func("/qnum/from_double", qnum_from_double_test);
    g_test_add_func("/qnum/destroy", qnum_destroy_test);
    g_test_add_func("/qnum/from_int64", qnum_from_int64_test);
    g_test_add_func("/qnum/get_int", qnum_get_int_test);
    g_test_add_func("/qnum/get_uint", qnum_get_uint_test);
    g_test_add_func("/qnum/to_qnum", qobject_to_qnum_test);
    g_test_add_func("/qnum/to_string", qnum_to_string_test);

    return g_test_run();
}
