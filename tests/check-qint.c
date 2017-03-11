/*
 * QInt unit-tests.
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

#include "qapi/qmp/qint.h"
#include "qapi/qmp/quint.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qint_from_int_test(void)
{
    QInt *qi;
    const int value = -42;

    qi = qint_from_int(value);
    g_assert(qi != NULL);
    g_assert(qi->value == value);
    g_assert(qi->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qi)) == QTYPE_QINT);

    // destroy doesn't exit yet
    g_free(qi);
}

static void qint_destroy_test(void)
{
    QInt *qi = qint_from_int(0);
    QDECREF(qi);
}

static void qint_from_int64_test(void)
{
    QInt *qi;
    const int64_t value = 0x1234567890abcdefLL;

    qi = qint_from_int(value);
    g_assert((int64_t) qi->value == value);

    QDECREF(qi);
}

static void qint_get_int_test(void)
{
    QInt *qi;
    const int value = 123456;

    qi = qint_from_int(value);
    g_assert(qint_get_int(qi) == value);

    QDECREF(qi);
}

static void qobject_to_qint_test(void)
{
    QInt *qi;

    qi = qint_from_int(0);
    g_assert(qobject_to_qint(QOBJECT(qi)) == qi);

    QDECREF(qi);
}

static void quint_from_uint_test(void)
{
    QUInt *qu;
    const unsigned value = -42;

    qu = quint_from_uint(value);
    g_assert(qu != NULL);
    g_assert(qu->value == value);
    g_assert(qu->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qu)) == QTYPE_QUINT);

    g_free(qu);
}

static void quint_destroy_test(void)
{
    QUInt *qu = quint_from_uint(0);
    QDECREF(qu);
}

static void quint_from_uint64_test(void)
{
    QUInt *qu;
    const uint64_t value = 0x1234567890abcdefLL;

    qu = quint_from_uint(value);
    g_assert((uint64_t) qu->value == value);

    QDECREF(qu);
}

static void quint_get_uint_test(void)
{
    QUInt *qu;
    const unsigned value = 123456;

    qu = quint_from_uint(value);
    g_assert(quint_get_uint(qu) == value);

    QDECREF(qu);
}

static void qobject_to_quint_test(void)
{
    QUInt *qu;

    qu = quint_from_uint(0);
    g_assert(qobject_to_quint(QOBJECT(qu)) == qu);

    QDECREF(qu);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/from_int", qint_from_int_test);
    g_test_add_func("/public/destroy", qint_destroy_test);
    g_test_add_func("/public/from_int64", qint_from_int64_test);
    g_test_add_func("/public/get_int", qint_get_int_test);
    g_test_add_func("/public/to_qint", qobject_to_qint_test);

    g_test_add_func("/public/from_uint", quint_from_uint_test);
    g_test_add_func("/public/uint_destroy", quint_destroy_test);
    g_test_add_func("/public/from_uint64", quint_from_uint64_test);
    g_test_add_func("/public/get_uint", quint_get_uint_test);
    g_test_add_func("/public/to_quint", qobject_to_quint_test);

    return g_test_run();
}
