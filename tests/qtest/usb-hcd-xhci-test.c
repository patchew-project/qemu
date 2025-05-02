/*
 * QTest testcase for USB xHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "libqos/libqos.h"
#include "libqos/libqos-pc.h"
#include "libqos/usb.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"

typedef struct TestData {
    const char *device;
    uint32_t fingerprint;
} TestData;

/*** Test Setup & Teardown ***/
typedef struct XHCIQState {
    /* QEMU PCI variables */
    QOSState *parent;
    QPCIDevice *dev;
    QPCIBar bar;
    uint64_t barsize;
    uint32_t fingerprint;
} XHCIQState;

#define XHCI_QEMU_ID (PCI_DEVICE_ID_REDHAT_XHCI << 16 | \
                      PCI_VENDOR_ID_REDHAT)
#define XHCI_NEC_ID (PCI_DEVICE_ID_NEC_UPD720200 << 16 | \
                     PCI_VENDOR_ID_NEC)

/**
 * Locate, verify, and return a handle to the XHCI device.
 */
static QPCIDevice *get_xhci_device(QTestState *qts)
{
    QPCIDevice *xhci;
    QPCIBus *pcibus;

    pcibus = qpci_new_pc(qts, NULL);

    /* Find the XHCI PCI device and verify it's the right one. */
    xhci = qpci_device_find(pcibus, QPCI_DEVFN(0x1D, 0x0));
    g_assert(xhci != NULL);

    return xhci;
}

static void free_xhci_device(QPCIDevice *dev)
{
    QPCIBus *pcibus = dev ? dev->bus : NULL;

    /* libqos doesn't have a function for this, so free it manually */
    g_free(dev);
    qpci_free_pc(pcibus);
}

/**
 * Start a Q35 machine and bookmark a handle to the XHCI device.
 */
G_GNUC_PRINTF(1, 0)
static XHCIQState *xhci_vboot(const char *cli, va_list ap)
{
    XHCIQState *s;

    s = g_new0(XHCIQState, 1);
    s->parent = qtest_pc_vboot(cli, ap);
    alloc_set_flags(&s->parent->alloc, ALLOC_LEAK_ASSERT);

    /* Verify that we have an XHCI device present. */
    s->dev = get_xhci_device(s->parent->qts);
    s->fingerprint = qpci_config_readl(s->dev, PCI_VENDOR_ID);
    s->bar = qpci_iomap(s->dev, 0, &s->barsize);
    /* turns on pci.cmd.iose, pci.cmd.mse and pci.cmd.bme */
    qpci_device_enable(s->dev);

    return s;
}

/**
 * Start a Q35 machine and bookmark a handle to the XHCI device.
 */
G_GNUC_PRINTF(1, 2)
static XHCIQState *xhci_boot(const char *cli, ...)
{
    XHCIQState *s;
    va_list ap;

    va_start(ap, cli);
    s = xhci_vboot(cli, ap);
    va_end(ap);

    return s;
}

static XHCIQState *xhci_boot_dev(const char *device, uint32_t fingerprint)
{
    XHCIQState *s;

    s = xhci_boot("-M q35 "
                  "-device %s,id=xhci,bus=pcie.0,addr=1d.0 "
                  "-drive id=drive0,if=none,file=null-co://,"
                         "file.read-zeroes=on,format=raw", device);
    g_assert_cmphex(s->fingerprint, ==, fingerprint);

    return s;
}

/**
 * Clean up the PCI device, then terminate the QEMU instance.
 */
static void xhci_shutdown(XHCIQState *xhci)
{
    QOSState *qs = xhci->parent;

    free_xhci_device(xhci->dev);
    g_free(xhci);
    qtest_shutdown(qs);
}

/*** tests ***/

static void test_xhci_hotplug(const void *arg)
{
    const TestData *td = arg;
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot_dev(td->device, td->fingerprint);
    qts = s->parent->qts;

    usb_test_hotplug(qts, "xhci", "1", NULL);

    xhci_shutdown(s);
}

static void test_usb_uas_hotplug(const void *arg)
{
    const TestData *td = arg;
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot_dev(td->device, td->fingerprint);
    qts = s->parent->qts;

    qtest_qmp_device_add(qts, "usb-uas", "uas", "{}");
    qtest_qmp_device_add(qts, "scsi-hd", "scsihd", "{'drive': 'drive0'}");

    /* TODO:
        UAS HBA driver in libqos, to check that
        added disk is visible after BUS rescan
    */

    qtest_qmp_device_del(qts, "scsihd");
    qtest_qmp_device_del(qts, "uas");
}

static void test_usb_ccid_hotplug(const void *arg)
{
    const TestData *td = arg;
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot_dev(td->device, td->fingerprint);
    qts = s->parent->qts;

    qtest_qmp_device_add(qts, "usb-ccid", "ccid", "{}");
    qtest_qmp_device_del(qts, "ccid");
    /* check the device can be added again */
    qtest_qmp_device_add(qts, "usb-ccid", "ccid", "{}");
    qtest_qmp_device_del(qts, "ccid");
}

static void add_test(const char *name, TestData *td, void (*fn)(const void *))
{
    g_autofree char *full_name = g_strdup_printf(
            "/xhci/pci/%s/%s", td->device, name);
    qtest_add_data_func(full_name, td, fn);
}

static void add_tests(TestData *td)
{
    add_test("hotplug", td, test_xhci_hotplug);
    if (qtest_has_device("usb-uas")) {
        add_test("usb-uas", td, test_usb_uas_hotplug);
    }
    if (qtest_has_device("usb-ccid")) {
        add_test("usb-ccid", td, test_usb_ccid_hotplug);
    }
}

/* tests */
int main(int argc, char **argv)
{
    int ret;
    const char *arch;
    int i;
    TestData td[] = {
        { .device = "qemu-xhci", .fingerprint = XHCI_QEMU_ID, },
        { .device = "nec-usb-xhci", .fingerprint = XHCI_NEC_ID, },
    };

    g_test_init(&argc, &argv, NULL);

    /* Check architecture */
    arch = qtest_get_arch();
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86");
        return 0;
    }

    for (i = 0; i < ARRAY_SIZE(td); i++) {
        if (qtest_has_device(td[i].device)) {
            add_tests(&td[i]);
        }
    }

    ret = g_test_run();
    qtest_end();

    return ret;
}
