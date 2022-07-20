/*
 * QTest testcase for TI X3130 PCIe switch
 *
 * Copyright (c) 2022 Yandex N.V.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/*
 * Let QEMU choose the bus and slot for the device under test.  It may even be
 * a non-PCIe bus but it's ok for the purpose of the test.
 */
static const char *common_args = "-device x3130-upstream,id=s0";

static void test_slot4(void)
{
    QTestState *qts;
    QDict *resp;

    /* attach a downstream port into slot4 of the upstream port */
    qts = qtest_init(common_args);
    resp = qtest_qmp(qts, "{'execute': 'device_add', 'arguments': {"
                     "'driver': 'xio3130-downstream', "
                     "'id': 'port1', "
                     "'bus': 's0', "
                     "'chassis': 5, "
                     "'addr': '4'"
                     "} }");
    g_assert_nonnull(resp);
    g_assert(!qdict_haskey(resp, "event"));
    g_assert(!qdict_haskey(resp, "error"));
    qobject_unref(resp);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/pcie-root-port/slot4", test_slot4);

    ret = g_test_run();

    return ret;
}
