/*
 * Which USB host controller -usb creates on the pc and q35 machines
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"

typedef struct {
    int slot, func;
    uint16_t vendor, device;
} Controller;

typedef struct {
    const char *machine;
    bool usb;
    const char *const *devices;
    const Controller *ctrl;
    size_t n;
} TestCase;

static const Controller piix3_uhci[] = {
    { 1, 2, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_2 },
};

static const Controller xhci[] = {
    { 0x1d, 0, PCI_VENDOR_ID_REDHAT, PCI_DEVICE_ID_REDHAT_XHCI },
};

static const Controller ich9_ehci[] = {
    { 0x1d, 0, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801I_UHCI1 },
    { 0x1d, 1, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801I_UHCI2 },
    { 0x1d, 2, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801I_UHCI3 },
    { 0x1d, 7, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801I_EHCI1 },
};

/* host controller types a case needs, in case the build left them out */
static const char *const xhci_dev[] = { "qemu-xhci", NULL };
static const char *const piix3_dev[] = { "piix3-usb-uhci", NULL };
static const char *const ich9_dev[] = { "ich9-usb-ehci1", "ich9-usb-uhci1",
                                        NULL };

static const TestCase cases[] = {
    { "pc", true, xhci_dev, xhci, ARRAY_SIZE(xhci) },
    { "q35", true, xhci_dev, xhci, ARRAY_SIZE(xhci) },
    { "pc-i440fx-12.0", true, xhci_dev, xhci, ARRAY_SIZE(xhci) },
    { "pc-q35-12.0", true, xhci_dev, xhci, ARRAY_SIZE(xhci) },
    { "pc-i440fx-11.2", true, piix3_dev, piix3_uhci, ARRAY_SIZE(piix3_uhci) },
    { "pc-q35-11.2", true, ich9_dev, ich9_ehci, ARRAY_SIZE(ich9_ehci) },
    { "pc", false, NULL, NULL, 0 },
    { "q35", false, NULL, NULL, 0 },
};

static bool has_devices(const char *const *devices)
{
    for (; devices && *devices; devices++) {
        if (!qtest_has_device(*devices)) {
            return false;
        }
    }
    return true;
}

static void test_usb_controllers(const void *data)
{
    const TestCase *tc = data;
    QTestState *qts = qtest_initf(tc->usb ? "-M %s -usb" : "-M %s",
                                  tc->machine);
    QDict *resp = qtest_qmp(qts, "{ 'execute': 'query-pci' }");
    QList *buses = qdict_get_qlist(resp, "return");
    QListEntry *be, *de;
    size_t found = 0;

    QLIST_FOREACH_ENTRY(buses, be) {
        QDict *bus = qobject_to(QDict, qlist_entry_obj(be));
        QList *devices = qdict_get_qlist(bus, "devices");

        QLIST_FOREACH_ENTRY(devices, de) {
            QDict *dev = qobject_to(QDict, qlist_entry_obj(de));
            QDict *id = qdict_get_qdict(dev, "id");
            int slot = qdict_get_int(dev, "slot");
            int func = qdict_get_int(dev, "function");
            int vendor = qdict_get_int(id, "vendor");
            int device = qdict_get_int(id, "device");
            size_t i;

            if (qdict_get_int(qdict_get_qdict(dev, "class_info"), "class") !=
                PCI_CLASS_SERIAL_USB) {
                continue;
            }
            for (i = 0; i < tc->n; i++) {
                if (tc->ctrl[i].slot == slot && tc->ctrl[i].func == func) {
                    break;
                }
            }
            if (i == tc->n) {
                g_test_message("unexpected USB controller %02x.%x %04x:%04x",
                               slot, func, vendor, device);
                g_assert_not_reached();
            }
            g_assert_cmpint(vendor, ==, tc->ctrl[i].vendor);
            g_assert_cmpint(device, ==, tc->ctrl[i].device);
            found++;
        }
    }
    g_assert_cmpuint(found, ==, tc->n);

    qobject_unref(resp);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);
    for (i = 0; i < ARRAY_SIZE(cases); i++) {
        static const char *const kind[] = { "no-usb", "usb" };
        g_autofree char *path = NULL;

        if (!qtest_has_machine(cases[i].machine) ||
            !has_devices(cases[i].devices)) {
            continue;
        }
        path = g_strdup_printf("/usb/pc-default/%s/%s", kind[cases[i].usb],
                               cases[i].machine);
        qtest_add_data_func(path, &cases[i], test_usb_controllers);
    }
    return g_test_run();
}
