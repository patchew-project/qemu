/*
 * QEMU device_del handling
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

static void device_del_request(const char *id)
{
    QDict *resp;

    resp = qmp("{'execute': 'device_del', 'arguments': { 'id': %s } }", id);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
}

static void system_reset(void)
{
    QDict *resp;

    resp = qmp("{'execute': 'system_reset'}");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
}

static void wait_device_deleted_event(const char *id)
{
    QDict *resp, *data;
    QObject *device;
    QString *qstr;

    /*
     * Other devices might get removed along with the removed device. Skip
     * these.
     */
    for (;;) {
        resp = qtest_qmp_eventwait_ref(global_qtest, "DEVICE_DELETED");
        data = qdict_get_qdict(resp, "data");
        if (!data) {
            qobject_unref(resp);
            continue;
        }
        device = qdict_get(data, "device");
        if (!device) {
            qobject_unref(resp);
            continue;
        }
        qstr = qobject_to(QString, device);
        g_assert(qstr);
        if (!strcmp(qstring_get_str(qstr), id)) {
            qobject_unref(data);
            qobject_unref(resp);
            break;
        }
        qobject_unref(data);
        qobject_unref(resp);
    }
}

static void test_pci_device_del_request(void)
{
    char *args;

    args = g_strdup_printf("-device virtio-mouse-pci,id=dev0");
    qtest_start(args);

    /*
     * Request device removal. As the guest is not running, the request won't
     * be processed. However during system reset, the removal will be
     * handled, removing the device.
     */
    device_del_request("dev0");
    system_reset();
    wait_device_deleted_event("dev0");

    qtest_end();
    g_free(args);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /*
     * We need a system that will process unplug requests during system resets
     * and does not do PCI surprise removal. This holds for x86 ACPI,
     * s390x and spapr.
     */
    qtest_add_func("/device_del/pci_device_del_request",
                   test_pci_device_del_request);

    return g_test_run();
}
