/*
 * QLit unit-tests.
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/qmp/qlit.h"

static void qobject_from_qlit_test(void)
{
    char *str;
    QObject *qobj = NULL;
    QLitObject qlit = QLIT_QDICT((
        (QLitDictEntry[]) {
            { "foo", QLIT_QNUM(42) },
            { "bar", QLIT_QSTR("hello world") },
            { "baz", QLIT_QNULL },
            { "bee", QLIT_QLIST((
                (QLitObject[]) {
                    QLIT_QNUM(43),
                    QLIT_QNUM(44),
                    QLIT_QBOOL(true),
                    { },
                }))
            },
            { },
        }));

    qobj = qobject_from_qlit(&qlit);

    str = qobject_to_string(qobj);
    g_assert_cmpstr(str, ==,
                    "bee:\n    [0]: 43\n    [1]: 44\n    [2]: true\n"   \
                    "baz: null\nbar: hello world\nfoo: 42\n");

    g_free(str);
    qobject_decref(qobj);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/qlit/qobject_from_qlit", qobject_from_qlit_test);

    return g_test_run();
}
