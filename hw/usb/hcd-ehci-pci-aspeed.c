/*
 * ASPEED PCIe EHCI
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/pci/msi.h"
#include "hw/usb/hcd-ehci.h"

#define TYPE_ASPEED_EHCI_PCI "aspeed-usb-ehci"
OBJECT_DECLARE_TYPE(AspeedEHCIPCIState, AspeedEHCIPCIClass, ASPEED_EHCI_PCI)

struct AspeedEHCIPCIState {
    EHCIPCIState parent_obj;
};

struct AspeedEHCIPCIClass {
    PCIDeviceClass parent_class;

    void (*parent_realize)(PCIDevice *dev, Error **errp);
    PCIUnregisterFunc *parent_exit;
};

#define ASPEED_EHCI_PCI_MSI_CAP 0x50

/* The EHCI core calls this function when its interrupt changes. */
static void aspeed_ehci_pci_set_irq(void *opaque, int n, int level)
{
    PCIDevice *dev = opaque;

    /* MSI */
    if (msi_enabled(dev)) {
        if (level) {
            msi_notify(dev, 0);
        }
        return;
    }

    /* INTx */
    pci_set_irq(dev, level);
}

static void aspeed_ehci_pci_init(Object *obj)
{
    qdev_prop_set_bit(DEVICE(obj), "caps-64bit-addr", true);
}

static void aspeed_ehci_pci_realize(PCIDevice *dev, Error **errp)
{
    AspeedEHCIPCIClass *ac = ASPEED_EHCI_PCI_GET_CLASS(dev);
    AspeedEHCIPCIState *s = ASPEED_EHCI_PCI(dev);
    EHCIState *ehci = &s->parent_obj.ehci;
    Error *err = NULL;
    int ret;

    ac->parent_realize(dev, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /*
     * INTx uses INTA. Send the EHCI interrupt to
     * aspeed_ehci_pci_set_irq(), which picks MSI or INTx.
     */
    pci_set_byte(&dev->config[PCI_INTERRUPT_PIN], 1);
    qemu_free_irq(ehci->irq);
    ehci->irq = qemu_allocate_irq(aspeed_ehci_pci_set_irq, dev, 0);

    /* If the board has no MSI, use INTx only. */
    ret = msi_init(dev, ASPEED_EHCI_PCI_MSI_CAP, 1, true, false, NULL);
    assert(!ret || ret == -ENOTSUP);
}

static void aspeed_ehci_pci_exit(PCIDevice *dev)
{
    AspeedEHCIPCIClass *ac = ASPEED_EHCI_PCI_GET_CLASS(dev);

    msi_uninit(dev);
    ac->parent_exit(dev);
}

static void aspeed_ehci_pci_class_init(ObjectClass *klass, const void *data)
{
    AspeedEHCIPCIClass *ac = ASPEED_EHCI_PCI_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    ac->parent_realize = k->realize;
    ac->parent_exit = k->exit;
    k->realize = aspeed_ehci_pci_realize;
    k->exit = aspeed_ehci_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_ASPEED;
    k->device_id = 0x2603;
    k->revision = 0x72;
    k->subsystem_vendor_id = PCI_VENDOR_ID_ASPEED;
    k->subsystem_id = 0x2603;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo aspeed_ehci_pci_types[] = {
    {
        .name          = TYPE_ASPEED_EHCI_PCI,
        .parent        = TYPE_PCI_EHCI,
        .instance_size = sizeof(AspeedEHCIPCIState),
        .instance_init = aspeed_ehci_pci_init,
        .class_size    = sizeof(AspeedEHCIPCIClass),
        .class_init    = aspeed_ehci_pci_class_init,
    },
};

DEFINE_TYPES(aspeed_ehci_pci_types)
