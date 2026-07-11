/*
 * QTest testcase for vga cards
 *
 * Copyright (c) 2014 Red Hat, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"

static void assert_qom_uint(QTestState *qts, const char *path,
                            const char *property, uint64_t expected)
{
    QDict *rsp;

    rsp = qtest_qmp(qts, "{ 'execute': 'qom-get', 'arguments': {"
                          "'path': %s,"
                          "'property': %s"
                          "} }", path, property);
    g_assert(qdict_haskey(rsp, "return"));
    g_assert_cmpuint(qdict_get_uint(rsp, "return"), ==, expected);
    qobject_unref(rsp);
}

static void pci_multihead(void)
{
    QTestState *qts;

    qts = qtest_init("-vga none -device VGA -device secondary-vga");
    qtest_quit(qts);
}

static void thunderbolt_vga_hotplug(void)
{
    QTestState *qts;
    QDict *rsp;
    QDict *err;

    qts = qtest_init("-machine q35 -display none -nodefaults -vga none "
                     "-device VGA,id=bootvga,bus=pcie.0,addr=0x02 "
                     "-device thunderbolt-root-port,id=tbrp0,bus=pcie.0,"
                     "chassis=1,slot=1,addr=0x05");

    rsp = qtest_qmp(qts, "{ 'execute': 'device_add', "
                          "'arguments': {"
                          "'driver': 'VGA',"
                          "'id': 'badstd',"
                          "'bus': 'tbrp0',"
                          "'addr': '0x01'"
                          "} }");
    g_assert(qdict_haskey(rsp, "error"));
    err = qdict_get_qdict(rsp, "error");
    g_assert_cmpstr(qdict_get_str(err, "desc"), ==,
                    "Device 'VGA' does not support hotplugging");
    qobject_unref(rsp);

    rsp = qtest_qmp(qts, "{ 'execute': 'device_add', "
                          "'arguments': {"
                          "'driver': 'thunderbolt-vga',"
                          "'id': 'tbvga0',"
                          "'bus': 'tbrp0',"
                          "'addr': '0x00'"
                          "} }");
    g_assert(qdict_haskey(rsp, "return"));
    g_assert(!qdict_haskey(rsp, "error"));
    qobject_unref(rsp);

    assert_qom_uint(qts, "/machine/peripheral/tbvga0", "vgamem_mb", 4);
    assert_qom_uint(qts, "/machine/peripheral/tbvga0", "xres", 1280);
    assert_qom_uint(qts, "/machine/peripheral/tbvga0", "yres", 800);
    assert_qom_uint(qts, "/machine/peripheral/tbvga0", "xmax", 1280);
    assert_qom_uint(qts, "/machine/peripheral/tbvga0", "ymax", 800);
    assert_qom_uint(qts, "/machine/peripheral/tbvga0", "refresh_rate", 60000);

    qtest_quit(qts);
}

static void test_vga(gconstpointer data)
{
    QTestState *qts;

    qts = qtest_initf("-vga none -device %s", (const char *)data);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    static const char *devices[] = {
        "cirrus-vga",
        "VGA",
        "thunderbolt-vga",
        "secondary-vga",
        "virtio-gpu-pci",
        "virtio-vga"
    };

    g_test_init(&argc, &argv, NULL);

    for (int i = 0; i < ARRAY_SIZE(devices); i++) {
        if (qtest_has_device(devices[i])) {
            char *testpath = g_strdup_printf("/display/pci/%s", devices[i]);
            qtest_add_data_func(testpath, devices[i], test_vga);
            g_free(testpath);
        }
    }

    if (qtest_has_device("secondary-vga")) {
        qtest_add_func("/display/pci/multihead", pci_multihead);
    }

    if (qtest_has_machine("q35") &&
        qtest_has_device("thunderbolt-root-port") &&
        qtest_has_device("thunderbolt-vga")) {
        qtest_add_func("/display/pci/thunderbolt-vga-hotplug",
                       thunderbolt_vga_hotplug);
    }

    return g_test_run();
}
