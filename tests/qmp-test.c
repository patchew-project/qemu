/*
 * QTest testcase for QMP
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * This program tests QMP commands maintained with the QMP core.
 * These are defined in qmp.c.  Tests for QMP commands defined in
 * another subsystem should go into a test program maintained with
 * that subsystem.
 *
 * TODO Actually cover the commands.  The tests we got so far only
 * demonstrate specific bugs we've fixed.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

static void test_object_add_without_props(void)
{
    QDict *ret, *error;
    const gchar *klass, *desc;

    ret = qmp("{'execute': 'object-add',"
              " 'arguments': { 'qom-type': 'memory-backend-ram', 'id': 'ram1' } }");
    g_assert_nonnull(ret);

    error = qdict_get_qdict(ret, "error");
    klass = qdict_get_try_str(error, "class");
    desc = qdict_get_try_str(error, "desc");

    g_assert_cmpstr(klass, ==, "GenericError");
    g_assert_cmpstr(desc, ==, "can't create backend with size 0");

    QDECREF(ret);
}

static void test_qom_set_without_value(void)
{
    QDict *ret, *error;
    const gchar *klass, *desc;

    ret = qmp("{'execute': 'qom-set',"
              " 'arguments': { 'path': '/machine', 'property': 'rtc-time' } }");
    g_assert_nonnull(ret);

    error = qdict_get_qdict(ret, "error");
    klass = qdict_get_try_str(error, "class");
    desc = qdict_get_try_str(error, "desc");

    g_assert_cmpstr(klass, ==, "GenericError");
    g_assert_cmpstr(desc, ==, "Parameter 'value' is missing");

    QDECREF(ret);
}

static void test_no_async(void)
{
    QDict *ret;
    int64_t id;

    /* check that only one async command is being processed */
    qmp_async("{'execute': 'qtest-timeout', 'id': 42, "
              " 'arguments': { 'duration': 1 } }");
    qmp_async("{'execute': 'qtest-timeout', 'id': 43, "
              " 'arguments': { 'duration': 0 } }");

    /* check that the second command didn't execute immediately */
    ret = qtest_qmp_receive(global_qtest);
    g_assert_nonnull(ret);
    id = qdict_get_try_int(ret, "id", -1);
    g_assert_cmpint(id, ==, 42);
    QDECREF(ret);

    /* check that the second command executes after */
    ret = qtest_qmp_receive(global_qtest);
    g_assert_nonnull(ret);
    id = qdict_get_try_int(ret, "id", -1);
    g_assert_cmpint(id, ==, 43);
    QDECREF(ret);
}

static void test_async(void)
{
    QDict *ret;
    int64_t id;
    QTestState *qtest;

    qtest = qtest_init_qmp_caps("-machine none", "'async'");

    /* check that async are concurrent */
    qtest_async_qmp(qtest, "{'execute': 'qtest-timeout', 'id': 42, "
              " 'arguments': { 'duration': 1 } }");
    qtest_async_qmp(qtest, "{'execute': 'qtest-timeout', 'id': 43, "
              " 'arguments': { 'duration': 0 } }");

    ret = qtest_qmp_receive(qtest);
    g_assert_nonnull(ret);
    id = qdict_get_try_int(ret, "id", -1);
    g_assert_cmpint(id, ==, 43);
    QDECREF(ret);

    ret = qtest_qmp_receive(qtest);
    g_assert_nonnull(ret);
    id = qdict_get_try_int(ret, "id", -1);
    g_assert_cmpint(id, ==, 42);
    QDECREF(ret);

    qtest_quit(qtest);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine none");

    qtest_add_func("/qemu-qmp/object-add-without-props",
                   test_object_add_without_props);
    qtest_add_func("/qemu-qmp/qom-set-without-value",
                   test_qom_set_without_value);
    qtest_add_func("/qemu-qmp/no-async",
                   test_no_async);
    qtest_add_func("/qemu-qmp/async",
                   test_async);

    ret = g_test_run();

    qtest_end();

    return ret;
}
