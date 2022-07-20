/*
 * QTest testcase for generic PCIe root port
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
static const char *common_args = "-device pcie-root-port,id=s0"
                                 ",port=1,chassis=1,multifunction=on";

static void test_slot0(void)
{
    QTestState *qts;
    QDict *resp;

    /* attach a PCIe device into slot0 of the root port */
    qts = qtest_init(common_args);
    /* PCIe root port is known to be supported, use it as a leaf device too */
    resp = qtest_qmp(qts, "{'execute': 'device_add', 'arguments': {"
                     "'driver': 'pcie-root-port', "
                     "'id': 'port1', "
                     "'bus': 's0', "
                     "'chassis': 5, "
                     "'addr': '0'"
                     "} }");
    g_assert_nonnull(resp);
    g_assert(!qdict_haskey(resp, "event"));
    g_assert(!qdict_haskey(resp, "error"));
    qobject_unref(resp);

    qtest_quit(qts);
}

static void test_slot4(void)
{
    QTestState *qts;
    QDict *resp;

    /* attach a PCIe device into slot4 of the root port should be rejected */
    qts = qtest_init(common_args);
    /* PCIe root port is known to be supported, use it as a leaf device too */
    resp = qtest_qmp(qts, "{'execute': 'device_add', 'arguments': {"
                     "'driver': 'pcie-root-port', "
                     "'id': 'port1', "
                     "'bus': 's0', "
                     "'chassis': 5, "
                     "'addr': '4'"
                     "} }");
    qmp_expect_error_and_unref(resp, "GenericError");

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/pcie-root-port/slot0", test_slot0);
    qtest_add_func("/pcie-root-port/slot4", test_slot4);

    ret = g_test_run();

    return ret;
}
