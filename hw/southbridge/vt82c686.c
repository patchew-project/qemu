/*
 * VT82C686B south bridge emulation
 *
 * Copyright (c) 2008 yajin (yajin@vm-kernel.org)
 * Copyright (c) 2009 chenming (chenming@rdc.faw.com.cn)
 * Copyright (c) 2010 Huacai Chen (zltjiangshi@gmail.com)
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 * This code is licensed under the GNU GPL v2.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/ide/pci.h"
#include "hw/isa/vt82c686.h"

#define TYPE_VT82C686B_SOUTHBRIDGE "vt82c686b-southbridge"
OBJECT_DECLARE_SIMPLE_TYPE(ViaSouthBridgeState, VT82C686B_SOUTHBRIDGE)

struct ViaSouthBridgeState {
    /* <private> */
    SysBusDevice parent_obj;
    /* <public> */

    uint8_t pci_slot;
    PCIBus *pci_bus;
    PCIDevice *isa;
    PCIDevice *ide;
    PCIDevice *usb[2];
    PCIDevice *apm;
    PCIDevice *audio;
    PCIDevice *modem;
};

static void via_southbridge_realize(DeviceState *dev, Error **errp)
{
    ViaSouthBridgeState *s = VT82C686B_SOUTHBRIDGE(dev);

    if (!s->pci_bus) {
        error_setg(errp, "SMMU is not attached to any PCI bus!");
        return;
    }

    s->isa = pci_create_simple_multifunction(s->pci_bus,
                                             PCI_DEVFN(s->pci_slot, 0),
                                             true, TYPE_VT82C686B_ISA);
    qdev_pass_gpios(DEVICE(s->isa), dev, "intr");

    s->ide = pci_create_simple(s->pci_bus,
                               PCI_DEVFN(s->pci_slot, 1), "via-ide");
    for (unsigned i = 0; i < 2; i++) {
        qdev_connect_gpio_out_named(DEVICE(s->ide), "ide-irq", i,
                            qdev_get_gpio_in_named(DEVICE(s->isa),
                                                   "isa-irq", 14 + i));
    }
    pci_ide_create_devs(s->ide);

    s->usb[0] = pci_create_simple(s->pci_bus,
                                  PCI_DEVFN(s->pci_slot, 2),
                                  "vt82c686b-usb-uhci");
    s->usb[1] = pci_create_simple(s->pci_bus,
                                  PCI_DEVFN(s->pci_slot, 3),
                                  "vt82c686b-usb-uhci");

    s->apm = pci_create_simple(s->pci_bus,
                               PCI_DEVFN(s->pci_slot, 4),
                               TYPE_VT82C686B_PM);
    object_property_add_alias(OBJECT(s), "i2c",
                              OBJECT(s->apm), "i2c");

    s->audio = pci_create_simple(s->pci_bus,
                                 PCI_DEVFN(s->pci_slot, 5),
                                 TYPE_VIA_AC97);
    s->modem = pci_create_simple(s->pci_bus,
                                 PCI_DEVFN(s->pci_slot, 6),
                                 TYPE_VIA_MC97);
}

static Property via_southbridge_properties[] = {
    DEFINE_PROP_UINT8("pci-slot", ViaSouthBridgeState, pci_slot, 0),
    DEFINE_PROP_LINK("pci-bus", ViaSouthBridgeState, pci_bus, "PCI", PCIBus *),
    DEFINE_PROP_END_OF_LIST(),
};

static void via_southbridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = via_southbridge_realize;
    device_class_set_props(dc, via_southbridge_properties);
}

static const TypeInfo via_southbridge_info = {
    .name          = TYPE_VT82C686B_SOUTHBRIDGE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ViaSouthBridgeState),
    .class_init    = via_southbridge_class_init,
};

static void via_southbridge_register_types(void)
{
    type_register_static(&via_southbridge_info);
}

type_init(via_southbridge_register_types)
